/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

// #define IGRAPHICS_DISABLE_VSYNC

#ifndef IPLUG_LOGGING_ALWAYS_ON
#define IPLUG_LOGGING_ALWAYS_ON 1
#endif

#if defined IGRAPHICS_VULKAN
  #ifndef IGRAPHICS_VULKAN_LOG_VERBOSITY
    #define IGRAPHICS_VULKAN_LOG_VERBOSITY 2
  #endif
#endif

#include <Shlobj.h>
#include <commctrl.h>

#include "heapbuf.h"

#include "IGraphicsWin.h"
#include "IGraphicsWin_dnd.h"
#include "IPlugParameter.h"
#include "IPlugPaths.h"
#include "IPopupMenuControl.h"
#include "SchedulerLogging.h"
#if defined IGRAPHICS_VULKAN
  #include "VulkanLogging.h"
#endif

#include <VersionHelpers.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <limits>
#include <thread>
#include <utility>
#include <vector>
#include <wininet.h>

#if defined __clang__
  #undef CCSIZEOF_STRUCT
  #define CCSIZEOF_STRUCT(structname, member) (__builtin_offsetof(structname, member) + sizeof(((structname*)0)->member))
#endif

#pragma warning(disable : 4244) // Pointer size cast mismatch.
#pragma warning(disable : 4312) // Pointer size cast mismatch.
#pragma warning(disable : 4311) // Pointer size cast mismatch.

static int nWndClassReg = 0;
static const wchar_t* wndClassName = L"IPlugWndClass";
static double sFPS = 0.0;

#define PARAM_EDIT_ID 99
#define IPLUG_TIMER_ID 2
#define IPLUG_VBLANK_HEALTH_TIMER_ID 3

#define TOOLTIPWND_MAXWIDTH 250

#define WM_VBLANK (WM_USER + 1)
#define WM_VBLANK_TICK WM_VBLANK

namespace iplug::igraphics
{

struct VBlankSubscription
{
  IGraphicsWin* owner = nullptr;
  HWND window = nullptr;
  std::atomic<bool> active{false};
};

namespace
{
constexpr uint32_t kVBlankQueueDepthWarningMultiplier = 2;

void RecordVBlankQueueDepthSample(uint32_t depth);
void IncrementVBlankQueueWarnCount();
void RecordVBlankDispatchSuccess();
void RecordVBlankLatencySample(uint32_t latencyMicros);
void UpdateVBlankDropTotal(uint32_t total);
uint64_t SteadyClockMicros(const std::chrono::steady_clock::time_point& tp);
template <typename T>
void AtomicMax(std::atomic<T>& target, T value);

#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
void RecordParamQueueTelemetry(int outstanding, schedulerlog::Severity severity);
void RecordSchedulerSample(const IGraphicsWin::InstancePaintBudget::Snapshot& snapshot,
                           const IGraphicsWin::SchedulerState* scheduler,
                           uint32_t droppedVBlank,
                           bool forgivenessActive);
#endif
} // namespace

class VBlankDispatchWorker
{
public:
  static VBlankDispatchWorker& Instance()
  {
    static VBlankDispatchWorker sInstance;
    return sInstance;
  }

  std::shared_ptr<VBlankSubscription> Subscribe(IGraphicsWin* owner, HWND window)
  {
    if (!owner || !window)
      return {};

    auto subscription = std::make_shared<VBlankSubscription>();
    subscription->owner = owner;
    subscription->window = window;
    subscription->active.store(true, std::memory_order_release);

    {
      std::lock_guard<std::mutex> lock(mMutex);
      EnsureThreadLocked();
      ++mActiveSubscriptions;
    }

    mCond.notify_all();
    return subscription;
  }

  void Unsubscribe(std::shared_ptr<VBlankSubscription> subscription)
  {
    if (!subscription)
      return;

    subscription->active.store(false, std::memory_order_release);

    bool joinNeeded = false;

    {
      std::lock_guard<std::mutex> lock(mMutex);
      if (mActiveSubscriptions > 0)
        --mActiveSubscriptions;

      if (mActiveSubscriptions == 0)
      {
        RequestStopLocked();
        joinNeeded = true;
      }
    }

    mCond.notify_all();

    if (joinNeeded && mThread.joinable())
    {
      mThread.join();
    }
  }

  bool QueueDispatch(const std::shared_ptr<VBlankSubscription>& subscription, DWORD dispatchCount)
  {
    if (!subscription)
      return false;

    DispatchRequest request;
    request.subscription = subscription;
    request.dispatchCount = dispatchCount;
    request.attempt = 0;
    request.due = std::chrono::steady_clock::now();
    request.enqueuedAt = request.due;

    uint32_t depth = 0;
    uint32_t highWater = 0;
    uint32_t activeSubscriptions = 0;
    uint32_t threshold = 0;
    bool emitWarn = false;

    {
      std::lock_guard<std::mutex> lock(mMutex);
      if (!subscription->active.load(std::memory_order_acquire) || mShutdown)
        return false;

      mQueue.push_back(std::move(request));
      depth = static_cast<uint32_t>(mQueue.size());
      auto& stored = mQueue.back();
      stored.queueDepthAtEnqueue = depth;
      stored.activeSubscriptions = mActiveSubscriptions;
      activeSubscriptions = std::max<uint32_t>(mActiveSubscriptions, 1);
      threshold = activeSubscriptions * kVBlankQueueDepthWarningMultiplier;
      if (depth > mQueueHighWater)
      {
        mQueueHighWater = depth;
      }
      highWater = mQueueHighWater;
      if (depth > threshold && depth > mLastQueueWarnDepth)
      {
        emitWarn = true;
        mLastQueueWarnDepth = depth;
      }
      mCond.notify_all();
    }

    RecordVBlankQueueDepthSample(depth);

    if (emitWarn)
    {
      IncrementVBlankQueueWarnCount();
      schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                             "worker",
                             schedulerlog::Severity::kWarn,
                             {schedulerlog::MakeField("event", "queue_depth"),
                              schedulerlog::MakeField("depth", depth),
                              schedulerlog::MakeField("threshold", threshold),
                              schedulerlog::MakeField("activeEditors", activeSubscriptions),
                              schedulerlog::MakeField("highWater", highWater)});
    }

    return true;
  }

private:
  struct DispatchRequest
  {
    std::shared_ptr<VBlankSubscription> subscription;
    DWORD dispatchCount = 0;
    uint32_t attempt = 0;
    std::chrono::steady_clock::time_point due{};
    std::chrono::steady_clock::time_point enqueuedAt{};
    uint32_t queueDepthAtEnqueue = 0;
    uint32_t queueDepthAtDequeue = 0;
    uint32_t activeSubscriptions = 0;
  };

  VBlankDispatchWorker()
    : mRng(static_cast<uint32_t>(GetTickCount64()))
  {
  }

  void EnsureThreadLocked()
  {
    if (!mThread.joinable())
    {
      mShutdown = false;
      mThread = std::thread(&VBlankDispatchWorker::DispatchLoop, this);
    }
    else if (mShutdown)
    {
      mShutdown = false;
    }
  }

  void RequestStopLocked()
  {
    mShutdown = true;
  }

  void DispatchLoop()
  {
    std::unique_lock<std::mutex> lock(mMutex);

    while (true)
    {
      if (mShutdown && mQueue.empty())
        break;

      if (mQueue.empty())
      {
        mCond.wait(lock);
        continue;
      }

      auto now = std::chrono::steady_clock::now();
      DispatchRequest& next = mQueue.front();

      if (next.due > now)
      {
        mCond.wait_until(lock, next.due);
        continue;
      }

      DispatchRequest request = std::move(next);
      request.queueDepthAtDequeue = static_cast<uint32_t>(mQueue.size());
      mQueue.pop_front();

      auto subscription = request.subscription;

      lock.unlock();

      bool shouldRetry = false;
      if (subscription && subscription->active.load(std::memory_order_acquire))
      {
        shouldRetry = HandleDispatch(request);
      }

      lock.lock();

      if (shouldRetry && !mShutdown)
      {
        uint32_t depth = 0;
        uint32_t highWater = 0;
        uint32_t activeSubscriptions = 0;
        uint32_t threshold = 0;
        bool emitWarn = false;

        mQueue.push_back(std::move(request));
        depth = static_cast<uint32_t>(mQueue.size());
        auto& stored = mQueue.back();
        if (stored.queueDepthAtEnqueue == 0)
        {
          stored.queueDepthAtEnqueue = depth;
        }
        if (stored.activeSubscriptions == 0)
        {
          stored.activeSubscriptions = mActiveSubscriptions;
        }
        activeSubscriptions = std::max<uint32_t>(mActiveSubscriptions, 1);
        threshold = activeSubscriptions * kVBlankQueueDepthWarningMultiplier;
        if (depth > mQueueHighWater)
        {
          mQueueHighWater = depth;
        }
        highWater = mQueueHighWater;
        if (depth > threshold && depth > mLastQueueWarnDepth)
        {
          emitWarn = true;
          mLastQueueWarnDepth = depth;
        }
        mCond.notify_all();

        RecordVBlankQueueDepthSample(depth);

        if (emitWarn)
        {
          IncrementVBlankQueueWarnCount();
          schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                                 "worker",
                                 schedulerlog::Severity::kWarn,
                                 {schedulerlog::MakeField("event", "queue_depth"),
                                  schedulerlog::MakeField("depth", depth),
                                  schedulerlog::MakeField("threshold", threshold),
                                  schedulerlog::MakeField("activeEditors", activeSubscriptions),
                                  schedulerlog::MakeField("highWater", highWater)});
        }
      }
    }
  }

  bool HandleDispatch(DispatchRequest& request)
  {
    auto subscription = request.subscription;
    IGraphicsWin* owner = subscription ? subscription->owner : nullptr;
    if (!owner || subscription->window == nullptr || owner->mVBlankShutdown)
      return false;

    DWORD dispatchCount = owner->mQueuedVBlank.load(std::memory_order_acquire);
    if (dispatchCount == 0)
      return false;
    request.dispatchCount = dispatchCount;

    if (::PostMessageW(subscription->window, WM_VBLANK_TICK, dispatchCount, 0))
    {
      owner->mPendingSyncVBlank.store(0, std::memory_order_release);
      RecordVBlankDispatchSuccess();

      const auto dispatchTime = std::chrono::steady_clock::now();
      double sinceLastMs = 0.0;
      if (mHaveLastDispatchTimestamp)
      {
        sinceLastMs = std::chrono::duration<double, std::milli>(dispatchTime - mLastDispatchTimestamp).count();
      }
      mLastDispatchTimestamp = dispatchTime;
      mHaveLastDispatchTimestamp = true;

      owner->RecordVBlankDispatchPosted(dispatchCount, SteadyClockMicros(request.enqueuedAt));

      schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                             "worker",
                             schedulerlog::Severity::kInfo,
                             {schedulerlog::MakeField("event", "dispatch"),
                              schedulerlog::MakeField("count", request.dispatchCount),
                              schedulerlog::MakeField("retryCount", request.attempt),
                              schedulerlog::MakeField("queueDepth", request.queueDepthAtDequeue),
                              schedulerlog::MakeField("enqueueDepth", request.queueDepthAtEnqueue),
                              schedulerlog::MakeField(
                                "activeEditors", std::max<uint32_t>(request.activeSubscriptions, 1)),
                              schedulerlog::MakeField("sinceLastMs", sinceLastMs)});
      return false;
    }

    const DWORD error = GetLastError();
    ++request.attempt;

    owner->mPendingSyncVBlank.store(dispatchCount, std::memory_order_release);

    if (request.attempt == 1)
    {
      request.due = std::chrono::steady_clock::now();
      return true;
    }

    if (request.attempt == 2)
    {
      std::uniform_int_distribution<int> jitterDist(0, 1);
      request.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(2 + jitterDist(mRng));
      return true;
    }

    if (request.attempt == 3)
    {
      request.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
      return true;
    }

    const uint32_t dropTotal = owner->mDroppedVBlank.fetch_add(1, std::memory_order_acq_rel) + 1;
    UpdateVBlankDropTotal(dropTotal);
    owner->mVBlankMessagePending.store(false, std::memory_order_release);
    owner->mPendingSyncVBlank.store(0, std::memory_order_release);

    schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                           "worker",
                           schedulerlog::Severity::kWarn,
                           {schedulerlog::MakeField("event", "drop"),
                            schedulerlog::MakeField("count", request.dispatchCount),
                            schedulerlog::MakeField("error", error),
                            schedulerlog::MakeField("droppedTotal", dropTotal)});

    owner->EnterVBlankPaused(request.dispatchCount, error);

    return false;
  }

  std::mutex mMutex;
  std::condition_variable mCond;
  std::deque<DispatchRequest> mQueue;
  std::thread mThread;
  bool mShutdown = false;
  uint32_t mActiveSubscriptions = 0;
  std::mt19937 mRng;
  uint32_t mQueueHighWater = 0;
  uint32_t mLastQueueWarnDepth = 0;
  std::chrono::steady_clock::time_point mLastDispatchTimestamp{};
  bool mHaveLastDispatchTimestamp = false;
};

} // namespace iplug::igraphics

#ifdef IGRAPHICS_GL3
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int* attribList);
  #define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
  #define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
  #define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
  #define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif

#ifdef IGRAPHICS_GL
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int interval);
#endif

#pragma mark - Static storage

namespace iplug::igraphics
{
StaticStorage<IGraphicsWin::InstalledFont> IGraphicsWin::sPlatformFontCache;
StaticStorage<HFontHolder> IGraphicsWin::sHFontCache;
} // namespace iplug::igraphics

extern float GetScaleForHWND(HWND hWnd);

namespace
{
using GetDpiForWindowProc = UINT(WINAPI*)(HWND);

GetDpiForWindowProc ResolveGetDpiForWindow()
{
  static GetDpiForWindowProc sGetDpiForWindow = nullptr;
  static bool sAttemptedLoad = false;

  if (!sAttemptedLoad)
  {
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32)
    {
      sGetDpiForWindow = reinterpret_cast<GetDpiForWindowProc>(GetProcAddress(user32, "GetDpiForWindow"));
    }
    sAttemptedLoad = true;
  }

  return sGetDpiForWindow;
}

float ComputeWindowDpiScale(HWND hWnd)
{
  if (hWnd)
  {
    if (const GetDpiForWindowProc dpiProc = ResolveGetDpiForWindow())
    {
      const UINT dpi = dpiProc(hWnd);
      if (dpi > 0)
        return static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    }
  }

  HWND dcWindow = hWnd ? hWnd : nullptr;
  HDC screenDC = GetDC(dcWindow);

  if (screenDC)
  {
    const int logPixels = GetDeviceCaps(screenDC, LOGPIXELSX);
    ReleaseDC(dcWindow, screenDC);

    if (logPixels > 0)
      return static_cast<float>(logPixels) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
  }

  return 1.f;
}
} // namespace

namespace iplug::igraphics
{
#pragma mark - Mouse and tablet helpers

namespace
{
constexpr ULONGLONG kBurstCoolingWindowMs = 32ULL; // ~2 VSYNC intervals at 60Hz
constexpr ULONGLONG kStaleDrainThresholdMs = 24ULL; // 1.5 VSYNC intervals
constexpr UINT kVBlankHealthCheckIntervalMs = 15U;
constexpr ULONGLONG kVBlankPauseSoftResetThresholdMs = 250ULL;
constexpr uint32_t kVBlankHealthAlertAttemptThreshold = 6U;
constexpr ULONGLONG kVBlankHealthAlertDurationMs = 180ULL;

std::string TrimCopy(const std::string& value)
{
  size_t begin = 0;
  size_t end = value.size();

  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
    ++begin;

  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
    --end;

  return value.substr(begin, end - begin);
}

std::string ToLowerCopy(const std::string& value)
{
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowered;
}

const char* IdlePacingConfigStatusToString(IGraphicsWin::IdlePacingConfigStatus status)
{
  switch (status)
  {
    case IGraphicsWin::IdlePacingConfigStatus::kOk: return "ok";
    case IGraphicsWin::IdlePacingConfigStatus::kFileMissing: return "file_missing";
    case IGraphicsWin::IdlePacingConfigStatus::kMissingKey: return "missing_key";
    case IGraphicsWin::IdlePacingConfigStatus::kInvalidValue: return "invalid_value";
  }
  return "unknown";
}

bool ExtractJsonStringForKey(const std::string& json, const std::string& key, std::string& valueOut)
{
  const std::string loweredJson = ToLowerCopy(json);
  const std::string loweredKey = ToLowerCopy("\"" + key + "\"");

  size_t keyPos = loweredJson.find(loweredKey);
  if (keyPos == std::string::npos)
    return false;

  size_t colonPos = loweredJson.find(':', keyPos + loweredKey.size());
  if (colonPos == std::string::npos)
    return false;

  size_t valueBegin = loweredJson.find_first_of("\"'", colonPos + 1);
  if (valueBegin == std::string::npos)
    return false;

  const char quote = loweredJson[valueBegin];
  size_t valueEnd = loweredJson.find(quote, valueBegin + 1);
  if (valueEnd == std::string::npos)
    return false;

  valueOut = json.substr(valueBegin + 1, valueEnd - valueBegin - 1);
  return true;
}

bool ParseIdlePacingModeStringInternal(const std::string& value, EIdlePacingMode& modeOut)
{
  const std::string trimmed = TrimCopy(value);
  if (trimmed.empty())
    return false;

  const std::string lowered = ToLowerCopy(trimmed);

  if (lowered == "legacy")
  {
    modeOut = EIdlePacingMode::Legacy;
    return true;
  }

  if (lowered == "adaptive")
  {
    modeOut = EIdlePacingMode::Adaptive;
    return true;
  }

  if (lowered == "locked60" || lowered == "locked60hz" || lowered == "locked_60hz")
  {
    modeOut = EIdlePacingMode::Locked60Hz;
    return true;
  }

  return false;
}

struct PaintBudgetTelemetryAccumulator
{
  std::atomic<int> maxInflight{0};
  std::atomic<int> maxQueued{0};
  std::atomic<uint64_t> histogram[4];
  std::atomic<uint64_t> samples{0};
  std::atomic<uint32_t> vblankQueueHighWater{0};
  std::atomic<uint32_t> vblankQueueWarnCount{0};
  std::atomic<uint64_t> vblankDispatches{0};
  std::atomic<uint32_t> vblankLatencyIndex{0};
  std::atomic<uint32_t> vblankLatencySamples{0};
  std::array<std::atomic<uint32_t>, IGraphicsWin::kVBlankLatencySampleCount> vblankLatencyUs;
  std::atomic<uint32_t> schedulerSampleCursor{0};
  std::atomic<uint32_t> schedulerSampleCount{0};
  std::array<std::atomic<uint32_t>, IGraphicsWin::kSchedulerSampleWindow> queuedInvalidatesWindow;
  std::array<std::atomic<uint32_t>, IGraphicsWin::kSchedulerSampleWindow> pendingPaintsWindow;
  std::array<std::atomic<uint32_t>, IGraphicsWin::kSchedulerSampleWindow> pendingFlushWindow;
  std::array<std::atomic<uint32_t>, IGraphicsWin::kSchedulerSampleWindow> idleStretchHundredthsWindow;
  std::array<std::atomic<uint32_t>, IGraphicsWin::kSchedulerSampleWindow> paramQueueOutstandingWindow;
  std::array<std::atomic<uint32_t>, IGraphicsWin::kSchedulerSampleWindow> droppedVBlankWindow;
  std::array<std::atomic<uint32_t>, IGraphicsWin::kSchedulerSampleWindow> idleForgivenessWindow;
  std::array<std::atomic<uint32_t>, IGraphicsWin::kSchedulerSampleWindow> idleTimerBehindWindow;
  std::atomic<uint32_t> pendingFlushHighWater{0};
  std::atomic<uint32_t> idleStretchHighWaterHundredths{100};
  std::atomic<uint32_t> paramQueueHighWater{0};
  std::atomic<uint32_t> paramQueueSampleCount{0};
  std::atomic<uint32_t> paramQueueErrorSamples{0};
  std::atomic<uint32_t> droppedVBlankTotal{0};

  PaintBudgetTelemetryAccumulator()
  {
    for (auto& bucket : histogram)
    {
      bucket.store(0, std::memory_order_relaxed);
    }

    for (auto& sample : vblankLatencyUs)
    {
      sample.store(0, std::memory_order_relaxed);
    }

    for (auto& sample : queuedInvalidatesWindow)
    {
      sample.store(0, std::memory_order_relaxed);
    }

    for (auto& sample : pendingPaintsWindow)
    {
      sample.store(0, std::memory_order_relaxed);
    }

    for (auto& sample : pendingFlushWindow)
    {
      sample.store(0, std::memory_order_relaxed);
    }

    for (auto& sample : idleStretchHundredthsWindow)
    {
      sample.store(100, std::memory_order_relaxed);
    }

    for (auto& sample : paramQueueOutstandingWindow)
    {
      sample.store(0, std::memory_order_relaxed);
    }

    for (auto& sample : droppedVBlankWindow)
    {
      sample.store(0, std::memory_order_relaxed);
    }

    for (auto& sample : idleForgivenessWindow)
    {
      sample.store(0, std::memory_order_relaxed);
    }

    for (auto& sample : idleTimerBehindWindow)
    {
      sample.store(0, std::memory_order_relaxed);
    }
  }
};

PaintBudgetTelemetryAccumulator& PaintBudgetTelemetry()
{
  static PaintBudgetTelemetryAccumulator accumulator;
  return accumulator;
}

void RecordVBlankQueueDepthSample(uint32_t depth)
{
  auto& telemetry = PaintBudgetTelemetry();
  AtomicMax(telemetry.vblankQueueHighWater, depth);
}

void IncrementVBlankQueueWarnCount()
{
  PaintBudgetTelemetry().vblankQueueWarnCount.fetch_add(1, std::memory_order_acq_rel);
}

void RecordVBlankDispatchSuccess()
{
  PaintBudgetTelemetry().vblankDispatches.fetch_add(1, std::memory_order_acq_rel);
}

void RecordVBlankLatencySample(uint32_t latencyMicros)
{
  auto& telemetry = PaintBudgetTelemetry();
  const uint32_t index = telemetry.vblankLatencyIndex.fetch_add(1, std::memory_order_acq_rel);
  telemetry.vblankLatencyUs[index % IGraphicsWin::kVBlankLatencySampleCount].store(latencyMicros, std::memory_order_release);
  const uint32_t sampleCount = std::min<uint32_t>(index + 1, static_cast<uint32_t>(IGraphicsWin::kVBlankLatencySampleCount));
  AtomicMax(telemetry.vblankLatencySamples, sampleCount);
}

void UpdateVBlankDropTotal(uint32_t total)
{
  PaintBudgetTelemetry().droppedVBlankTotal.store(total, std::memory_order_release);
}

#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
void RecordParamQueueTelemetry(int outstanding, schedulerlog::Severity severity)
{
  auto& telemetry = PaintBudgetTelemetry();
  const uint32_t depth = static_cast<uint32_t>(std::max(outstanding, 0));
  AtomicMax(telemetry.paramQueueHighWater, depth);
  telemetry.paramQueueSampleCount.fetch_add(1, std::memory_order_acq_rel);
  if (severity == schedulerlog::Severity::kError)
  {
    telemetry.paramQueueErrorSamples.fetch_add(1, std::memory_order_acq_rel);
  }
}

void RecordSchedulerSample(const IGraphicsWin::InstancePaintBudget::Snapshot& snapshot,
                           const IGraphicsWin::SchedulerState* scheduler,
                           uint32_t droppedVBlank,
                           bool forgivenessActive)
{
  auto& telemetry = PaintBudgetTelemetry();
  const uint32_t cursor = telemetry.schedulerSampleCursor.fetch_add(1, std::memory_order_acq_rel);
  const size_t slot = cursor % IGraphicsWin::kSchedulerSampleWindow;
  const uint32_t sampleCount = std::min<uint32_t>(cursor + 1, static_cast<uint32_t>(IGraphicsWin::kSchedulerSampleWindow));
  telemetry.schedulerSampleCount.store(sampleCount, std::memory_order_release);

  telemetry.queuedInvalidatesWindow[slot].store(static_cast<uint32_t>(std::max(snapshot.queuedInvalidates, 0)),
                                                std::memory_order_release);
  telemetry.pendingPaintsWindow[slot].store(static_cast<uint32_t>(std::max(snapshot.pendingPaints, 0)),
                                            std::memory_order_release);
  telemetry.droppedVBlankWindow[slot].store(droppedVBlank, std::memory_order_release);
  telemetry.idleForgivenessWindow[slot].store(forgivenessActive ? 1u : 0u, std::memory_order_release);

  uint32_t pendingFlush = 0;
  uint32_t idleStretchHundredths = 100;
  uint32_t paramOutstanding = 0;
  uint32_t timerBehind = 0;

  if (scheduler)
  {
    pendingFlush = static_cast<uint32_t>(std::max(scheduler->pendingParamFlush, 0));
    const double stretch = std::max(scheduler->idleStretchFactor, 0.0);
    idleStretchHundredths = static_cast<uint32_t>(std::lround(stretch * 100.0));
    paramOutstanding = static_cast<uint32_t>(std::max(scheduler->lastIdleOutstanding, 0));
    timerBehind = scheduler->lastIdleTimerBehind ? 1u : 0u;
    AtomicMax(telemetry.pendingFlushHighWater, pendingFlush);
    AtomicMax(telemetry.idleStretchHighWaterHundredths, idleStretchHundredths);
    AtomicMax(telemetry.paramQueueHighWater, paramOutstanding);
  }

  telemetry.pendingFlushWindow[slot].store(pendingFlush, std::memory_order_release);
  telemetry.idleStretchHundredthsWindow[slot].store(idleStretchHundredths, std::memory_order_release);
  telemetry.paramQueueOutstandingWindow[slot].store(paramOutstanding, std::memory_order_release);
  telemetry.idleTimerBehindWindow[slot].store(timerBehind, std::memory_order_release);
  telemetry.droppedVBlankTotal.store(droppedVBlank, std::memory_order_release);
}
#else
void RecordParamQueueTelemetry(int, schedulerlog::Severity)
{
}

void RecordSchedulerSample(const IGraphicsWin::InstancePaintBudget::Snapshot& snapshot,
                           const void*,
                           uint32_t droppedVBlank,
                           bool forgivenessActive)
{
  auto& telemetry = PaintBudgetTelemetry();
  const uint32_t cursor = telemetry.schedulerSampleCursor.fetch_add(1, std::memory_order_acq_rel);
  const size_t slot = cursor % IGraphicsWin::kSchedulerSampleWindow;
  const uint32_t sampleCount = std::min<uint32_t>(cursor + 1, static_cast<uint32_t>(IGraphicsWin::kSchedulerSampleWindow));
  telemetry.schedulerSampleCount.store(sampleCount, std::memory_order_release);
  telemetry.queuedInvalidatesWindow[slot].store(static_cast<uint32_t>(std::max(snapshot.queuedInvalidates, 0)),
                                                std::memory_order_release);
  telemetry.pendingPaintsWindow[slot].store(static_cast<uint32_t>(std::max(snapshot.pendingPaints, 0)),
                                            std::memory_order_release);
  telemetry.pendingFlushWindow[slot].store(0, std::memory_order_release);
  telemetry.idleStretchHundredthsWindow[slot].store(100, std::memory_order_release);
  telemetry.paramQueueOutstandingWindow[slot].store(0, std::memory_order_release);
  telemetry.droppedVBlankWindow[slot].store(droppedVBlank, std::memory_order_release);
  telemetry.idleForgivenessWindow[slot].store(forgivenessActive ? 1u : 0u, std::memory_order_release);
  telemetry.idleTimerBehindWindow[slot].store(0, std::memory_order_release);
  telemetry.droppedVBlankTotal.store(droppedVBlank, std::memory_order_release);
}
#endif
uint64_t SteadyClockMicros(const std::chrono::steady_clock::time_point& tp)
{
  return static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count());
}

template <typename T>
void AtomicMax(std::atomic<T>& target, T value)
{
  T current = target.load(std::memory_order_relaxed);
  while (current < value
         && !target.compare_exchange_weak(current, value, std::memory_order_release, std::memory_order_relaxed))
  {
  }
}

int HistogramBucketForCount(int count)
{
  if (count <= 0)
    return -1;
  if (count <= 2)
    return 0;
  if (count <= 5)
    return 1;
  if (count <= 8)
    return 2;
  return 3;
}

RECT UnionRects(const std::vector<RECT>& rects)
{
  RECT result{0, 0, 0, 0};
  if (rects.empty())
  {
    return result;
  }

  result = rects[0];
  for (size_t idx = 1; idx < rects.size(); ++idx)
  {
    result.left = std::min(result.left, rects[idx].left);
    result.top = std::min(result.top, rects[idx].top);
    result.right = std::max(result.right, rects[idx].right);
    result.bottom = std::max(result.bottom, rects[idx].bottom);
  }
  return result;
}

const char* DecisionKindLabel(IGraphicsWin::InstancePaintBudget::DecisionKind kind)
{
  using DecisionKind = IGraphicsWin::InstancePaintBudget::DecisionKind;
  switch (kind)
  {
    case DecisionKind::kNeedsDrain:     return "NeedsDrain";
    case DecisionKind::kBurstCooling:   return "BurstCooling";
    case DecisionKind::kBudgetExceeded: return "BudgetExceeded";
    case DecisionKind::kStaleDrain:     return "StaleDrain";
    case DecisionKind::kTierEscalation: return "TierEscalation";
    case DecisionKind::kDeferredFlush:  return "DeferredFlush";
    case DecisionKind::kDrainComplete:  return "DrainComplete";
    case DecisionKind::kNone:
    default:
      return "Idle";
  }
}

#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
const char* IdleStateLabel(IGraphicsWin::SchedulerState::ThrottleState state)
{
  using ThrottleState = IGraphicsWin::SchedulerState::ThrottleState;
  switch (state)
  {
    case ThrottleState::kBurstCooling: return "BurstCooling";
    case ThrottleState::kIdleCatchUp:  return "IdleCatchUp";
    case ThrottleState::kNormal:
    default:
      return "Normal";
  }
}
#endif

schedulerlog::Severity SeverityForSnapshot(const IGraphicsWin::InstancePaintBudget::Snapshot& snapshot)
{
  const int budget = std::max(snapshot.budget, 1);
  const int queued = snapshot.queuedInvalidates;
  const bool overBudget = queued > (budget * 3) / 2;
  if (overBudget && snapshot.overBudgetConsecutive > 2)
  {
    return schedulerlog::Severity::kWarn;
  }

  if (queued > budget * 2)
  {
    return schedulerlog::Severity::kWarn;
  }

  if (snapshot.needsDrain)
  {
    return schedulerlog::Severity::kInfo;
  }

  return schedulerlog::Severity::kDebug;
}

void RecordPaintBudgetHistogram(const IGraphicsWin::InstancePaintBudget::Snapshot& snapshot)
{
  auto& telemetry = PaintBudgetTelemetry();
  const int bucket = HistogramBucketForCount(snapshot.lastDecisionRegionCount);
  if (bucket >= 0 && bucket < 4)
  {
    telemetry.histogram[bucket].fetch_add(1, std::memory_order_relaxed);
  }
}

void UpdatePaintBudgetCounters(const IGraphicsWin::InstancePaintBudget::Snapshot& snapshot)
{
  auto& telemetry = PaintBudgetTelemetry();
  telemetry.samples.fetch_add(1, std::memory_order_relaxed);
  AtomicMax(telemetry.maxInflight, snapshot.pendingPaints);
  AtomicMax(telemetry.maxQueued, snapshot.queuedInvalidates);
}
} // namespace

} // namespace iplug::igraphics

namespace iplug::igraphics
{
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
void IGraphicsWin::SchedulerState::Reset(EIdlePacingMode mode, ULONGLONG nowTick)
{
  throttleState = ThrottleState::kNormal;
  idleStretchFactor = 1.0;
  pendingParamFlush = 0;
  forgivenessDeadlineTick = 0;
  lastIdleTick = nowTick;
  stateEnteredTick = nowTick;
  lastIdleOutstanding = 0;
  lastIdleParamDepthBefore = 0;
  lastIdleParamDepthAfter = 0;
  lastIdleProcessed = 0;
  lastIdleElapsedMs = 0.0;
  lastIdleTimerBehind = false;
  lastIdleSampleTick = nowTick;
  paramQueueAboveThresholdSince = 0;
  paramQueueHighWater = 0;

  switch (mode)
  {
    case EIdlePacingMode::Locked60Hz:
      baseCadenceMs = kIdleCadenceLocked60Ms;
      break;
    case EIdlePacingMode::Adaptive:
    case EIdlePacingMode::Legacy:
    default:
      baseCadenceMs = kIdleCadenceLegacyMs;
      break;
  }

  idleCadenceTargetMs = baseCadenceMs;
}

void IGraphicsWin::InitializeIdleSchedulerState()
{
  ResetIdleSchedulerState(GetIdlePacingMode(), GetTickCount64());
}

void IGraphicsWin::ResetIdleSchedulerState(EIdlePacingMode mode, ULONGLONG nowTick)
{
  mSchedulerState.Reset(mode, nowTick);

  schedulerlog::LogEvent(schedulerlog::kCategoryIdleState, "mode_reset", schedulerlog::Severity::kInfo,
    {
      schedulerlog::MakeStringField("mode", IdlePacingModeToString(mode)),
      schedulerlog::MakeStringField("state", IdleStateLabel(mSchedulerState.throttleState)),
      schedulerlog::MakeField("targetMs", mSchedulerState.idleCadenceTargetMs),
      schedulerlog::MakeField("baseMs", mSchedulerState.baseCadenceMs)
    });
}

int IGraphicsWin::ComputeIdleCadenceMs(double stretchFactor) const
{
  const double rawCadence = static_cast<double>(mSchedulerState.baseCadenceMs) * stretchFactor;
  int target = static_cast<int>(std::lround(rawCadence));
  const int minCadence = std::max(kIdleCadenceLocked60Ms, mSchedulerState.baseCadenceMs / 2);
  const int maxCadence = std::max(mSchedulerState.baseCadenceMs * 3, mSchedulerState.baseCadenceMs);
  target = std::clamp(target, minCadence, maxCadence);
  return target;
}

void IGraphicsWin::EnterIdleState(SchedulerState::ThrottleState newState,
                                  double stretchFactor,
                                  const char* stage,
                                  ULONGLONG nowTick,
                                  std::initializer_list<schedulerlog::Field> extraFields)
{
  const SchedulerState::ThrottleState previousState = mSchedulerState.throttleState;
  const double previousStretch = mSchedulerState.idleStretchFactor;

  mSchedulerState.throttleState = newState;
  mSchedulerState.idleStretchFactor = stretchFactor;
  mSchedulerState.idleCadenceTargetMs = ComputeIdleCadenceMs(stretchFactor);
  mSchedulerState.stateEnteredTick = nowTick;

  const bool stateChanged = (previousState != newState) || (std::abs(previousStretch - stretchFactor) > 0.01);

  if (!stateChanged && extraFields.size() == 0)
  {
    return;
  }

  std::vector<schedulerlog::Field> fields;
  fields.reserve(8 + extraFields.size());
  fields.push_back(schedulerlog::MakeStringField("mode", IdlePacingModeToString(GetIdlePacingMode())));
  fields.push_back(schedulerlog::MakeStringField("state", IdleStateLabel(newState)));
  fields.push_back(schedulerlog::MakeField("stretch", stretchFactor));
  fields.push_back(schedulerlog::MakeField("targetMs", mSchedulerState.idleCadenceTargetMs));
  fields.push_back(schedulerlog::MakeField("baseMs", mSchedulerState.baseCadenceMs));
  fields.push_back(schedulerlog::MakeField("queuedInvalidates", mInstancePaintBudget.QueuedInvalidates()));
  fields.push_back(schedulerlog::MakeField("pendingPaints", mInstancePaintBudget.PendingPaints()));
  if (mSchedulerState.pendingParamFlush > 0)
  {
    fields.push_back(schedulerlog::MakeField("pendingParamFlush", mSchedulerState.pendingParamFlush));
  }

  for (const auto& extra : extraFields)
  {
    if (extra.key && extra.key[0] != '\0')
    {
      fields.push_back(extra);
    }
  }

  schedulerlog::Severity severity = schedulerlog::Severity::kInfo;
  if (newState == SchedulerState::ThrottleState::kBurstCooling)
  {
    severity = schedulerlog::Severity::kWarn;
  }

  schedulerlog::LogEvent(schedulerlog::kCategoryIdleState, stage ? stage : "state_change", severity, fields);
}

void IGraphicsWin::OnIdleThrottleTriggered(ULONGLONG nowTick, InstancePaintBudget::DecisionKind reason, int regionCount)
{
  if (GetIdlePacingMode() != EIdlePacingMode::Adaptive)
  {
    return;
  }

  mSchedulerState.pendingParamFlush = std::max(mSchedulerState.pendingParamFlush, 1);
  mSchedulerState.forgivenessDeadlineTick = 0;

  EnterIdleState(SchedulerState::ThrottleState::kBurstCooling,
                 kBurstCoolingStretch,
                 "burst_cooling",
                 nowTick,
                 {
                   schedulerlog::MakeStringField("reason", DecisionKindLabel(reason)),
                   schedulerlog::MakeField("regions", regionCount)
                 });
}

void IGraphicsWin::OnIdleDrainComplete(ULONGLONG nowTick, int drainedRegions)
{
  if (GetIdlePacingMode() != EIdlePacingMode::Adaptive)
  {
    return;
  }

  const int pendingPaints = mInstancePaintBudget.PendingPaints();
  const int queued = mInstancePaintBudget.QueuedInvalidates();
  const int budget = std::max(mInstancePaintBudget.BudgetCeiling(), 1);
  const bool belowHalfBudget = (pendingPaints <= std::max(1, budget / 2)) && (queued <= std::max(1, budget / 2));
  const bool needsDrain = mInstancePaintBudget.NeedsDrain();

  if (mSchedulerState.throttleState == SchedulerState::ThrottleState::kBurstCooling && belowHalfBudget && !needsDrain)
  {
    mSchedulerState.pendingParamFlush = std::max(mSchedulerState.pendingParamFlush, 1);
    EnterIdleState(SchedulerState::ThrottleState::kIdleCatchUp,
                   kIdleCatchUpStretch,
                   "burst_recovered",
                   nowTick,
                   {
                     schedulerlog::MakeField("drainedRegions", drainedRegions),
                     schedulerlog::MakeField("pendingPaints", pendingPaints),
                     schedulerlog::MakeField("queuedInvalidates", queued)
                   });
    return;
  }

  if (mSchedulerState.throttleState == SchedulerState::ThrottleState::kIdleCatchUp && needsDrain)
  {
    EnterIdleState(SchedulerState::ThrottleState::kBurstCooling,
                   kBurstCoolingStretch,
                   "catchup_reverted",
                   nowTick,
                   {
                     schedulerlog::MakeField("pendingPaints", pendingPaints),
                     schedulerlog::MakeField("queuedInvalidates", queued)
                   });
    return;
  }

  if (mSchedulerState.throttleState != SchedulerState::ThrottleState::kNormal && !needsDrain && pendingPaints == 0 && queued == 0)
  {
    mSchedulerState.pendingParamFlush = 0;
    EnterIdleState(SchedulerState::ThrottleState::kNormal,
                   1.0,
                   "drain_idle",
                   nowTick,
                   {});
  }
}

bool IGraphicsWin::MaybeExtendIdleForgiveness(ULONGLONG nowTick,
                                              int requestMs,
                                              const HostIdleTickInfo& info,
                                              const char* reason,
                                              schedulerlog::Severity severity)
{
  if (requestMs <= 0)
  {
    return false;
  }

  const int clamped = std::max(kIdleForgivenessMinMs, std::min(requestMs, kIdleForgivenessMaxMs));
  const ULONGLONG candidate = nowTick + static_cast<ULONGLONG>(clamped);

  if (candidate <= mSchedulerState.forgivenessDeadlineTick)
  {
    return false;
  }

  mSchedulerState.forgivenessDeadlineTick = candidate;

  schedulerlog::LogEvent(schedulerlog::kCategoryIdleForgiveness, reason, severity,
    {
      schedulerlog::MakeField("durationMs", clamped),
      schedulerlog::MakeField("deadlineTick", static_cast<uint64_t>(candidate)),
      schedulerlog::MakeField("paramQueued", info.paramQueueDepthBefore),
      schedulerlog::MakeField("paramProcessed", info.paramMessagesProcessed),
      schedulerlog::MakeField("paramRemaining", info.paramQueueDepthAfter),
      schedulerlog::MakeField("pendingFlush", std::max(mSchedulerState.pendingParamFlush, 0))
    });

  return true;
}

void IGraphicsWin::MaybeExpireIdleForgiveness(ULONGLONG nowTick,
                                              bool forgivenessExtended,
                                              const HostIdleTickInfo& info)
{
  if (forgivenessExtended)
  {
    return;
  }

  if (mSchedulerState.forgivenessDeadlineTick == 0)
  {
    return;
  }

  if (nowTick < mSchedulerState.forgivenessDeadlineTick)
  {
    return;
  }

  schedulerlog::LogEvent(schedulerlog::kCategoryIdleForgiveness, "expired", schedulerlog::Severity::kDebug,
    {
      schedulerlog::MakeField("deadlineTick", static_cast<uint64_t>(mSchedulerState.forgivenessDeadlineTick)),
      schedulerlog::MakeField("paramQueued", info.paramQueueDepthAfter),
      schedulerlog::MakeField("pendingFlush", std::max(mSchedulerState.pendingParamFlush, 0))
    });

  mSchedulerState.forgivenessDeadlineTick = 0;
}
#endif

IGraphicsWin::SchedulerTelemetrySnapshot IGraphicsWin::GetSchedulerTelemetrySnapshot()
{
  SchedulerTelemetrySnapshot snapshot;
  auto& telemetry = PaintBudgetTelemetry();
  snapshot.maxInflight = telemetry.maxInflight.load(std::memory_order_acquire);
  snapshot.maxQueued = telemetry.maxQueued.load(std::memory_order_acquire);
  for (int i = 0; i < 4; ++i)
  {
    snapshot.histogram[i] = telemetry.histogram[i].load(std::memory_order_acquire);
  }
  snapshot.sampleCount = telemetry.samples.load(std::memory_order_acquire);
  snapshot.vblankQueueHighWater = telemetry.vblankQueueHighWater.load(std::memory_order_acquire);
  snapshot.vblankQueueWarnCount = telemetry.vblankQueueWarnCount.load(std::memory_order_acquire);
  snapshot.vblankDispatches = telemetry.vblankDispatches.load(std::memory_order_acquire);
  snapshot.vblankLatencySampleCount = telemetry.vblankLatencySamples.load(std::memory_order_acquire);
  for (size_t i = 0; i < kVBlankLatencySampleCount; ++i)
  {
    const uint32_t micros = telemetry.vblankLatencyUs[i].load(std::memory_order_acquire);
    snapshot.vblankLatencyMs[i] = static_cast<double>(micros) / 1000.0;
  }
  snapshot.pendingFlushHighWater = telemetry.pendingFlushHighWater.load(std::memory_order_acquire);
  snapshot.idleStretchHighWaterHundredths = telemetry.idleStretchHighWaterHundredths.load(std::memory_order_acquire);
  snapshot.paramQueueHighWater = telemetry.paramQueueHighWater.load(std::memory_order_acquire);
  snapshot.paramQueueSampleCount = telemetry.paramQueueSampleCount.load(std::memory_order_acquire);
  snapshot.paramQueueErrorSamples = telemetry.paramQueueErrorSamples.load(std::memory_order_acquire);
  snapshot.droppedVBlankTotal = telemetry.droppedVBlankTotal.load(std::memory_order_acquire);
  snapshot.schedulerSampleCursor = telemetry.schedulerSampleCursor.load(std::memory_order_acquire);
  snapshot.schedulerSampleCount = telemetry.schedulerSampleCount.load(std::memory_order_acquire);
  for (size_t i = 0; i < kSchedulerSampleWindow; ++i)
  {
    snapshot.queuedInvalidatesWindow[i] = telemetry.queuedInvalidatesWindow[i].load(std::memory_order_acquire);
    snapshot.pendingPaintsWindow[i] = telemetry.pendingPaintsWindow[i].load(std::memory_order_acquire);
    snapshot.pendingFlushWindow[i] = telemetry.pendingFlushWindow[i].load(std::memory_order_acquire);
    snapshot.idleStretchHundredthsWindow[i] = telemetry.idleStretchHundredthsWindow[i].load(std::memory_order_acquire);
    snapshot.paramQueueOutstandingWindow[i] = telemetry.paramQueueOutstandingWindow[i].load(std::memory_order_acquire);
    snapshot.droppedVBlankWindow[i] = telemetry.droppedVBlankWindow[i].load(std::memory_order_acquire);
    snapshot.idleForgivenessWindow[i] = telemetry.idleForgivenessWindow[i].load(std::memory_order_acquire);
    snapshot.idleTimerBehindWindow[i] = telemetry.idleTimerBehindWindow[i].load(std::memory_order_acquire);
  }
  return snapshot;
}

void IGraphicsWin::ResetSchedulerTelemetrySnapshot()
{
  auto& telemetry = PaintBudgetTelemetry();
  telemetry.maxInflight.store(0, std::memory_order_release);
  telemetry.maxQueued.store(0, std::memory_order_release);
  for (auto& bucket : telemetry.histogram)
  {
    bucket.store(0, std::memory_order_release);
  }
  telemetry.samples.store(0, std::memory_order_release);
  telemetry.vblankQueueHighWater.store(0, std::memory_order_release);
  telemetry.vblankQueueWarnCount.store(0, std::memory_order_release);
  telemetry.vblankDispatches.store(0, std::memory_order_release);
  telemetry.vblankLatencyIndex.store(0, std::memory_order_release);
  telemetry.vblankLatencySamples.store(0, std::memory_order_release);
  for (auto& sample : telemetry.vblankLatencyUs)
  {
    sample.store(0, std::memory_order_release);
  }
  telemetry.schedulerSampleCursor.store(0, std::memory_order_release);
  telemetry.schedulerSampleCount.store(0, std::memory_order_release);
  telemetry.pendingFlushHighWater.store(0, std::memory_order_release);
  telemetry.idleStretchHighWaterHundredths.store(100, std::memory_order_release);
  telemetry.paramQueueHighWater.store(0, std::memory_order_release);
  telemetry.paramQueueSampleCount.store(0, std::memory_order_release);
  telemetry.paramQueueErrorSamples.store(0, std::memory_order_release);
  telemetry.droppedVBlankTotal.store(0, std::memory_order_release);
  for (auto& sample : telemetry.queuedInvalidatesWindow)
  {
    sample.store(0, std::memory_order_release);
  }
  for (auto& sample : telemetry.pendingPaintsWindow)
  {
    sample.store(0, std::memory_order_release);
  }
  for (auto& sample : telemetry.pendingFlushWindow)
  {
    sample.store(0, std::memory_order_release);
  }
  for (auto& sample : telemetry.idleStretchHundredthsWindow)
  {
    sample.store(100, std::memory_order_release);
  }
  for (auto& sample : telemetry.paramQueueOutstandingWindow)
  {
    sample.store(0, std::memory_order_release);
  }
  for (auto& sample : telemetry.droppedVBlankWindow)
  {
    sample.store(0, std::memory_order_release);
  }
  for (auto& sample : telemetry.idleForgivenessWindow)
  {
    sample.store(0, std::memory_order_release);
  }
  for (auto& sample : telemetry.idleTimerBehindWindow)
  {
    sample.store(0, std::memory_order_release);
  }
}

void IGraphicsWin::InstancePaintBudget::Configure(int widthPixels, int heightPixels)
{
  const int clampedWidth = std::max(widthPixels, 0);
  const int clampedHeight = std::max(heightPixels, 0);
  const int64_t pixels = static_cast<int64_t>(clampedWidth) * static_cast<int64_t>(clampedHeight);

  if (pixels <= 0)
  {
    mSurfacePixels.store(0, std::memory_order_release);
    mBudgetCeiling.store(3, std::memory_order_release);
    return;
  }

  const int previousPixels = mSurfacePixels.load(std::memory_order_acquire);
  if (previousPixels == pixels)
  {
    return;
  }

  mSurfacePixels.store(static_cast<int>(pixels), std::memory_order_release);

  const double budget = 3.0 + std::ceil(static_cast<double>(pixels) / 1500000.0);
  mBudgetCeiling.store(static_cast<int>(budget), std::memory_order_release);
}

void IGraphicsWin::InstancePaintBudget::Reset()
{
  mInflight.store(0, std::memory_order_release);
  mQueuedInvalidates.store(0, std::memory_order_release);
  mBudgetCeiling.store(3, std::memory_order_release);
  mNeedsDrain.store(false, std::memory_order_release);
  mBurstCooling.store(false, std::memory_order_release);
  mBurstCoolingDeadline.store(0, std::memory_order_release);
  mLastDrainTick.store(0, std::memory_order_release);
  mSurfacePixels.store(0, std::memory_order_release);
  mDeferredRegion = RECT{0, 0, 0, 0};
  mHasDeferredRegion = false;
  mOverBudgetConsecutive.store(0, std::memory_order_release);
  mLastDecision.store(static_cast<int>(DecisionKind::kNone), std::memory_order_release);
  mLastDecisionRegions.store(0, std::memory_order_release);
  mLastDecisionWidth.store(0, std::memory_order_release);
  mLastDecisionHeight.store(0, std::memory_order_release);
  mLastDecisionTick.store(0, std::memory_order_release);
}

void IGraphicsWin::InstancePaintBudget::OnInvalidateScheduled(int regionCount)
{
  const int clamped = std::max(regionCount, 1);
  mInflight.fetch_add(1, std::memory_order_acq_rel);
  mQueuedInvalidates.fetch_add(clamped, std::memory_order_acq_rel);
  mNeedsDrain.store(false, std::memory_order_release);
}

void IGraphicsWin::InstancePaintBudget::OnAdditionalInvalidationQueued(int regionCount)
{
  const int clamped = std::max(regionCount, 1);
  mQueuedInvalidates.fetch_add(clamped, std::memory_order_acq_rel);
}

void IGraphicsWin::InstancePaintBudget::OnPaintCompleted(int drainedRegions, ULONGLONG nowTick)
{
  const int drained = std::max(drainedRegions, 1);

  const int prevInflight = mInflight.fetch_sub(1, std::memory_order_acq_rel);
  if (prevInflight <= 0)
  {
    mInflight.store(0, std::memory_order_release);
  }

  const int prevQueued = mQueuedInvalidates.fetch_sub(drained, std::memory_order_acq_rel);
  if (prevQueued <= drained)
  {
    mQueuedInvalidates.store(0, std::memory_order_release);
  }

  UpdateLastDrainTick(nowTick);
  ClearNeedsDrainIfRecovered();

  const int budget = std::max(BudgetCeiling(), 1);
  if (QueuedInvalidates() <= (budget / 2))
  {
    ClearBurstCooling();
  }
}

int IGraphicsWin::InstancePaintBudget::PendingPaints() const
{
  return mInflight.load(std::memory_order_acquire);
}

int IGraphicsWin::InstancePaintBudget::QueuedInvalidates() const
{
  return mQueuedInvalidates.load(std::memory_order_acquire);
}

int IGraphicsWin::InstancePaintBudget::BudgetCeiling() const
{
  return mBudgetCeiling.load(std::memory_order_acquire);
}

bool IGraphicsWin::InstancePaintBudget::NeedsDrain() const
{
  return mNeedsDrain.load(std::memory_order_acquire);
}

bool IGraphicsWin::InstancePaintBudget::MarkNeedsDrain()
{
  const bool previous = mNeedsDrain.exchange(true, std::memory_order_acq_rel);
  return !previous;
}

bool IGraphicsWin::InstancePaintBudget::ClearNeedsDrainIfRecovered()
{
  if ((QueuedInvalidates() < BudgetCeiling()) && (PendingPaints() < BudgetCeiling()))
  {
    const bool previous = mNeedsDrain.exchange(false, std::memory_order_acq_rel);
    return previous;
  }
  return false;
}

bool IGraphicsWin::InstancePaintBudget::EngageBurstCooling(ULONGLONG deadlineTick)
{
  const bool wasActive = mBurstCooling.exchange(true, std::memory_order_acq_rel);
  mBurstCoolingDeadline.store(deadlineTick, std::memory_order_release);
  return !wasActive;
}

bool IGraphicsWin::InstancePaintBudget::BurstCoolingActive(ULONGLONG nowTick) const
{
  if (!mBurstCooling.load(std::memory_order_acquire))
  {
    return false;
  }

  const ULONGLONG deadline = mBurstCoolingDeadline.load(std::memory_order_acquire);
  if (deadline == 0)
  {
    return true;
  }

  if (nowTick <= deadline)
  {
    return true;
  }

  // Deadline expired; clear burst cooling state lazily.
  mBurstCooling.store(false, std::memory_order_release);
  mBurstCoolingDeadline.store(0, std::memory_order_release);
  return false;
}

bool IGraphicsWin::InstancePaintBudget::ClearBurstCooling()
{
  const bool wasActive = mBurstCooling.exchange(false, std::memory_order_acq_rel);
  mBurstCoolingDeadline.store(0, std::memory_order_release);
  return wasActive;
}

bool IGraphicsWin::InstancePaintBudget::ShouldThrottle(int additionalRegions, ULONGLONG nowTick, DecisionKind& outReason) const
{
  outReason = DecisionKind::kNone;

  if (additionalRegions <= 0)
  {
    return false;
  }

  if (NeedsDrain())
  {
    outReason = DecisionKind::kNeedsDrain;
    return true;
  }

  if (BurstCoolingActive(nowTick))
  {
    outReason = DecisionKind::kBurstCooling;
    return true;
  }

  const int budget = BudgetCeiling();
  const int queued = QueuedInvalidates();
  const int inflight = PendingPaints();

  if (queued + additionalRegions > budget)
  {
    outReason = DecisionKind::kBudgetExceeded;
    return true;
  }

  const ULONGLONG lastDrain = mLastDrainTick.load(std::memory_order_acquire);
  if (inflight > 0 && lastDrain != 0)
  {
    const ULONGLONG elapsed = nowTick - lastDrain;
    if (elapsed > kStaleDrainThresholdMs)
    {
      outReason = DecisionKind::kStaleDrain;
      return true;
    }
  }

  return false;
}

void IGraphicsWin::InstancePaintBudget::MergeDeferredRegion(const RECT& rect)
{
  if (!mHasDeferredRegion)
  {
    mDeferredRegion = rect;
    mHasDeferredRegion = true;
    return;
  }

  mDeferredRegion.left = std::min(mDeferredRegion.left, rect.left);
  mDeferredRegion.top = std::min(mDeferredRegion.top, rect.top);
  mDeferredRegion.right = std::max(mDeferredRegion.right, rect.right);
  mDeferredRegion.bottom = std::max(mDeferredRegion.bottom, rect.bottom);
}

bool IGraphicsWin::InstancePaintBudget::ConsumeDeferredRegion(RECT& rectOut)
{
  if (!mHasDeferredRegion)
  {
    return false;
  }

  rectOut = mDeferredRegion;
  mDeferredRegion = RECT{0, 0, 0, 0};
  mHasDeferredRegion = false;
  return true;
}

void IGraphicsWin::InstancePaintBudget::UpdateLastDrainTick(ULONGLONG nowTick)
{
  mLastDrainTick.store(nowTick, std::memory_order_release);
}

void IGraphicsWin::InstancePaintBudget::RecordDecision(DecisionKind kind, int regionCount, const RECT& unionRect, ULONGLONG timestamp)
{
  const int clampedCount = std::max(regionCount, 0);
  const int width = std::max<LONG>(0, unionRect.right - unionRect.left);
  const int height = std::max<LONG>(0, unionRect.bottom - unionRect.top);

  mLastDecision.store(static_cast<int>(kind), std::memory_order_release);
  mLastDecisionRegions.store(clampedCount, std::memory_order_release);
  mLastDecisionWidth.store(width, std::memory_order_release);
  mLastDecisionHeight.store(height, std::memory_order_release);
  mLastDecisionTick.store(timestamp, std::memory_order_release);
}

IGraphicsWin::InstancePaintBudget::DecisionKind IGraphicsWin::InstancePaintBudget::LastDecision() const
{
  return static_cast<DecisionKind>(mLastDecision.load(std::memory_order_acquire));
}

int IGraphicsWin::InstancePaintBudget::LastDecisionRegionCount() const
{
  return mLastDecisionRegions.load(std::memory_order_acquire);
}

int IGraphicsWin::InstancePaintBudget::LastDecisionWidth() const
{
  return mLastDecisionWidth.load(std::memory_order_acquire);
}

int IGraphicsWin::InstancePaintBudget::LastDecisionHeight() const
{
  return mLastDecisionHeight.load(std::memory_order_acquire);
}

ULONGLONG IGraphicsWin::InstancePaintBudget::LastDecisionTick() const
{
  return mLastDecisionTick.load(std::memory_order_acquire);
}

int IGraphicsWin::InstancePaintBudget::OverBudgetConsecutive() const
{
  return mOverBudgetConsecutive.load(std::memory_order_acquire);
}

int IGraphicsWin::InstancePaintBudget::UpdateOverBudgetConsecutive(bool overBudget)
{
  if (overBudget)
  {
    return mOverBudgetConsecutive.fetch_add(1, std::memory_order_acq_rel) + 1;
  }

  mOverBudgetConsecutive.store(0, std::memory_order_release);
  return 0;
}

int IGraphicsWin::UpdateOverBudgetTracking()
{
  const int budget = std::max(mInstancePaintBudget.BudgetCeiling(), 1);
  const int queued = mInstancePaintBudget.QueuedInvalidates();
  const bool overBudget = queued > (budget * 3) / 2;
  return mInstancePaintBudget.UpdateOverBudgetConsecutive(overBudget);
}

void IGraphicsWin::PublishPaintBudgetSnapshot(const char* stage, const char* reason, int drainedRegions, ULONGLONG nowTick, bool recordHistogram)
{
  const int consecutive = UpdateOverBudgetTracking();

  InstancePaintBudget::Snapshot snapshot;
  mInstancePaintBudget.SnapshotState(snapshot);
  snapshot.overBudgetConsecutive = consecutive;

  const ULONGLONG now = (nowTick != 0) ? nowTick : GetTickCount64();
  const ULONGLONG lastDrain = snapshot.lastDrainTick;
  const uint64_t sinceDrain = (lastDrain != 0 && now >= lastDrain) ? static_cast<uint64_t>(now - lastDrain) : 0ULL;

  if (stage)
  {
    schedulerlog::Severity severity = SeverityForSnapshot(snapshot);
    const char* effectiveReason = reason ? reason : DecisionKindLabel(snapshot.lastDecision);
    schedulerlog::LogEvent(schedulerlog::kCategoryPaintBudget, stage, severity,
      {
        schedulerlog::MakeStringField("reason", effectiveReason),
        schedulerlog::MakeField("pendingPaints", snapshot.pendingPaints),
        schedulerlog::MakeField("queuedInvalidates", snapshot.queuedInvalidates),
        schedulerlog::MakeField("budget", snapshot.budget),
        schedulerlog::MakeBoolField("needsDrain", snapshot.needsDrain),
        schedulerlog::MakeBoolField("burstCooling", snapshot.burstCooling),
        schedulerlog::MakeField("overBudgetConsecutive", snapshot.overBudgetConsecutive),
        schedulerlog::MakeField("lastDrainMs", sinceDrain),
        schedulerlog::MakeField("decisionRegions", snapshot.lastDecisionRegionCount),
        schedulerlog::MakeField("decisionWidth", snapshot.lastDecisionWidth),
        schedulerlog::MakeField("decisionHeight", snapshot.lastDecisionHeight),
        schedulerlog::MakeField("drainedRegions", drainedRegions)
      });
  }

  if (recordHistogram)
  {
    RecordPaintBudgetHistogram(snapshot);
  }

  UpdateSchedulerHUD(snapshot);
}

void IGraphicsWin::RefreshPaintBudgetHUD()
{
  const int consecutive = UpdateOverBudgetTracking();

  InstancePaintBudget::Snapshot snapshot;
  mInstancePaintBudget.SnapshotState(snapshot);
  snapshot.overBudgetConsecutive = consecutive;

  UpdatePaintBudgetCounters(snapshot);
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  const ULONGLONG nowTick = GetTickCount64();
  const bool forgivenessActive =
    (mSchedulerState.forgivenessDeadlineTick != 0 && nowTick < mSchedulerState.forgivenessDeadlineTick);
  const uint32_t dropped = mDroppedVBlank.load(std::memory_order_acquire);
  RecordSchedulerSample(snapshot, &mSchedulerState, dropped, forgivenessActive);
#else
  const uint32_t dropped = mDroppedVBlank.load(std::memory_order_acquire);
  RecordSchedulerSample(snapshot, nullptr, dropped, false);
#endif
  UpdateSchedulerHUD(snapshot);
}

void IGraphicsWin::UpdateSchedulerHUD(const InstancePaintBudget::Snapshot& snapshot)
{
  if (!ShowingFPSDisplay())
  {
    UpdateFPSDisplaySupplementalText(nullptr, nullptr);
    return;
  }

  const ULONGLONG nowTick = GetTickCount64();
  WDL_String primary;
  primary.SetFormatted(128, "Queued %d/%d Pending %d", snapshot.queuedInvalidates, snapshot.budget, snapshot.pendingPaints);

  if (snapshot.needsDrain)
  {
    primary.Append(" !Drain");
  }

  if (snapshot.burstCooling)
  {
    primary.Append(" Burst");
  }

  if (snapshot.overBudgetConsecutive > 2)
  {
    primary.Append(" Over");
  }

#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  primary.Append(" ");
  primary.AppendFormatted(64, "Idle %s", IdleStateLabel(mSchedulerState.throttleState));

  if (GetIdlePacingMode() == EIdlePacingMode::Adaptive)
  {
    primary.AppendFormatted(64, " x%.1f", mSchedulerState.idleStretchFactor);
    primary.AppendFormatted(64, " %dms", mSchedulerState.idleCadenceTargetMs);
  }
  else
  {
    primary.AppendFormatted(32, " %dms", mSchedulerState.baseCadenceMs);
  }

  const int pendingFlush = std::max(mSchedulerState.pendingParamFlush, 0);
  const int outstanding = std::max(mSchedulerState.lastIdleOutstanding, 0);
  if (pendingFlush > 0 || outstanding > 0)
  {
    primary.AppendFormatted(64, " Param %d", pendingFlush);
    if (outstanding > 0)
    {
      primary.AppendFormatted(32, "/%d", outstanding);
    }
  }

  if (mSchedulerState.lastIdleTimerBehind)
  {
    primary.Append(" Timer+");
  }

  if (mSchedulerState.forgivenessDeadlineTick != 0 && nowTick < mSchedulerState.forgivenessDeadlineTick)
  {
    const ULONGLONG remaining = mSchedulerState.forgivenessDeadlineTick - nowTick;
    primary.AppendFormatted(48, " Forg %llums", static_cast<unsigned long long>(remaining));
  }
#endif

  WDL_String secondary;

  if (snapshot.lastDecision != InstancePaintBudget::DecisionKind::kNone)
  {
    const char* label = DecisionKindLabel(snapshot.lastDecision);

    if (snapshot.lastDecisionRegionCount > 0 && snapshot.lastDecisionWidth > 0 && snapshot.lastDecisionHeight > 0)
    {
      secondary.SetFormatted(128, "%s %d@%dx%d", label, snapshot.lastDecisionRegionCount, snapshot.lastDecisionWidth, snapshot.lastDecisionHeight);
    }
    else if (snapshot.lastDecisionRegionCount > 0)
    {
      secondary.SetFormatted(128, "%s %d", label, snapshot.lastDecisionRegionCount);
    }
    else
    {
      secondary.Set(label);
    }

    if (snapshot.lastDecisionTick != 0 && nowTick >= snapshot.lastDecisionTick)
    {
      const ULONGLONG age = nowTick - snapshot.lastDecisionTick;
      secondary.Append(" ");
      secondary.AppendFormatted(32, "%llums", static_cast<unsigned long long>(age));
    }
  }

  const uint32_t vblankQueued = mQueuedVBlank.load(std::memory_order_acquire);
  const uint32_t vblankDrops = mDroppedVBlank.load(std::memory_order_acquire);
  const bool vblankPaused = mVBlankPaused.load(std::memory_order_acquire);
  WDL_String vblankLine;
  vblankLine.SetFormatted(128, "VBlank q%u d%u", vblankQueued, vblankDrops);
  if (vblankPaused)
  {
    vblankLine.Append(" Paused");
  }

  if (secondary.GetLength())
  {
    secondary.Append(" | ");
    secondary.Append(vblankLine.Get());
  }
  else
  {
    secondary.Set(vblankLine.Get());
  }

  UpdateFPSDisplaySupplementalText(primary.GetLength() ? primary.Get() : nullptr,
                                   secondary.GetLength() ? secondary.Get() : nullptr);
}

void IGraphicsWin::InstancePaintBudget::SnapshotState(Snapshot& out) const
{
  out.pendingPaints = PendingPaints();
  out.queuedInvalidates = QueuedInvalidates();
  out.budget = BudgetCeiling();
  out.needsDrain = NeedsDrain();
  out.burstCooling = mBurstCooling.load(std::memory_order_acquire);
  out.lastDrainTick = mLastDrainTick.load(std::memory_order_acquire);
  out.burstCoolingDeadline = mBurstCoolingDeadline.load(std::memory_order_acquire);
  out.lastDecision = static_cast<DecisionKind>(mLastDecision.load(std::memory_order_acquire));
  out.lastDecisionRegionCount = mLastDecisionRegions.load(std::memory_order_acquire);
  out.lastDecisionWidth = mLastDecisionWidth.load(std::memory_order_acquire);
  out.lastDecisionHeight = mLastDecisionHeight.load(std::memory_order_acquire);
  out.lastDecisionTick = mLastDecisionTick.load(std::memory_order_acquire);
  out.overBudgetConsecutive = mOverBudgetConsecutive.load(std::memory_order_acquire);
  out.surfacePixels = mSurfacePixels.load(std::memory_order_acquire);
}

inline IMouseInfo IGraphicsWin::GetMouseInfo(LPARAM lParam, WPARAM wParam)
{
  IMouseInfo info;
  const float scale = GetTotalScale();
  info.x = mCursorX = GET_X_LPARAM(lParam) / scale;
  info.y = mCursorY = GET_Y_LPARAM(lParam) / scale;
  info.ms = IMouseMod((wParam & MK_LBUTTON), (wParam & MK_RBUTTON), (wParam & MK_SHIFT), (wParam & MK_CONTROL),
#ifdef AAX_API
                      GetAsyncKeyState(VK_MENU) < 0
#else
                      GetKeyState(VK_MENU) < 0
#endif
  );

  return info;
}

void IGraphicsWin::CheckTabletInput(UINT msg)
{
  if ((msg == WM_LBUTTONDOWN) || (msg == WM_RBUTTONDOWN) || (msg == WM_MBUTTONDOWN) || (msg == WM_MOUSEMOVE) || (msg == WM_RBUTTONDBLCLK) || (msg == WM_LBUTTONDBLCLK) || (msg == WM_MBUTTONDBLCLK)
      || (msg == WM_RBUTTONUP) || (msg == WM_LBUTTONUP) || (msg == WM_MBUTTONUP) || (msg == WM_MOUSEHOVER) || (msg == WM_MOUSELEAVE))
  {
    const LONG_PTR c_SIGNATURE_MASK = 0xFFFFFF00;
    const LONG_PTR c_MOUSEEVENTF_FROMTOUCH = 0xFF515700;

    LONG_PTR extraInfo = GetMessageExtraInfo();
    SetTabletInput(((extraInfo & c_SIGNATURE_MASK) == c_MOUSEEVENTF_FROMTOUCH));
    mCursorLock &= !mTabletInput;
  }
}

void IGraphicsWin::FlushDeferredInvalidations()
{
  if (!mPlugWnd)
  {
    return;
  }

  if (mInstancePaintBudget.NeedsDrain())
  {
    return;
  }

  RECT deferredRect{};
  if (!mInstancePaintBudget.ConsumeDeferredRegion(deferredRect))
  {
    return;
  }

  const ULONGLONG nowTick = GetTickCount64();

  if (!mPaintPending.exchange(true, std::memory_order_acq_rel))
  {
    mInstancePaintBudget.OnInvalidateScheduled(1);
  }
  else
  {
    mInstancePaintBudget.OnAdditionalInvalidationQueued(1);
  }

  mInstancePaintBudget.MarkNeedsDrain();
  mInstancePaintBudget.RecordDecision(InstancePaintBudget::DecisionKind::kDeferredFlush, 1, deferredRect, nowTick);
  PublishPaintBudgetSnapshot("invalidate.flush", "DeferredFlush", 0, nowTick, true);
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  OnIdleThrottleTriggered(nowTick, InstancePaintBudget::DecisionKind::kDeferredFlush, 1);
#endif
  InvalidateRect(mPlugWnd, &deferredRect, FALSE);
}

void IGraphicsWin::DestroyEditWindow()
{
  if (mParamEditWnd)
  {
    SetWindowLongPtrW(mParamEditWnd, GWLP_WNDPROC, (LPARAM)mDefEditProc);
    DestroyWindow(mParamEditWnd);
    mParamEditWnd = nullptr;
    mDefEditProc = nullptr;
    DeleteObject(mEditFont);
    mEditFont = nullptr;
  }
}

void IGraphicsWin::RecordVBlankDispatchPosted(DWORD count, uint64_t enqueueMicros)
{
  const size_t slot = static_cast<size_t>(count % kVBlankLatencySampleCount);
  mVBlankLatencyCounts[slot].store(count, std::memory_order_release);
  mVBlankLatencyMicros[slot].store(enqueueMicros, std::memory_order_release);
}

void IGraphicsWin::RecordVBlankDispatchHandled(DWORD count, uint64_t handledMicros)
{
  const size_t slot = static_cast<size_t>(count % kVBlankLatencySampleCount);
  const DWORD recordedCount = mVBlankLatencyCounts[slot].load(std::memory_order_acquire);
  if (recordedCount != count)
  {
    return;
  }

  const uint64_t enqueuedMicros = mVBlankLatencyMicros[slot].load(std::memory_order_acquire);
  if (enqueuedMicros == 0 || handledMicros <= enqueuedMicros)
  {
    return;
  }

  const uint64_t latencyMicros = handledMicros - enqueuedMicros;
  const uint32_t clampedMicros = static_cast<uint32_t>(std::min<uint64_t>(latencyMicros, std::numeric_limits<uint32_t>::max()));
  RecordVBlankLatencySample(clampedMicros);

  const double latencyMs = static_cast<double>(latencyMicros) / 1000.0;
  schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                         "ui",
                         schedulerlog::Severity::kInfo,
                         {schedulerlog::MakeField("event", "ack"),
                          schedulerlog::MakeField("count", count),
                          schedulerlog::MakeField("latencyMs", latencyMs),
                          schedulerlog::MakeField("pendingPaints", mInstancePaintBudget.PendingPaints()),
                          schedulerlog::MakeField("queuedInvalidates", mInstancePaintBudget.QueuedInvalidates())});

  mVBlankLatencyMicros[slot].store(0, std::memory_order_release);
}

void IGraphicsWin::EnterVBlankPaused(DWORD failedCount, DWORD errorCode)
{
  mPendingSyncVBlank.store(failedCount, std::memory_order_release);

  const ULONGLONG nowTick = GetTickCount64();
  const bool wasPaused = mVBlankPaused.exchange(true, std::memory_order_acq_rel);

  if (!wasPaused)
  {
    mVBlankPausedSinceTick = nowTick;
    mVBlankSoftResetIssued = false;
    mVBlankConsecutiveDrops.store(1, std::memory_order_release);
    mVBlankHealthCheckAttempts.store(0, std::memory_order_release);

    schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                           "worker",
                           schedulerlog::Severity::kWarn,
                           {schedulerlog::MakeField("event", "pause"),
                            schedulerlog::MakeField("count", failedCount),
                            schedulerlog::MakeField("error", static_cast<uint32_t>(errorCode)),
                            schedulerlog::MakeField(
                              "droppedTotal", mDroppedVBlank.load(std::memory_order_acquire))});

    PublishPaintBudgetSnapshot("vblank.pause", "Pause", 0, nowTick, true);
  }
  else
  {
    const uint32_t drops = mVBlankConsecutiveDrops.fetch_add(1, std::memory_order_acq_rel) + 1;
    schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                           "worker",
                           schedulerlog::Severity::kInfo,
                           {schedulerlog::MakeField("event", "pause_extend"),
                            schedulerlog::MakeField("count", failedCount),
                            schedulerlog::MakeField("error", static_cast<uint32_t>(errorCode)),
                            schedulerlog::MakeField("drops", drops)});
  }

  StartVBlankHealthTimer();
}

void IGraphicsWin::ExitVBlankPaused(DWORD recoveredCount, ULONGLONG resumeTick)
{
  if (!mVBlankPaused.exchange(false, std::memory_order_acq_rel))
  {
    return;
  }

  StopVBlankHealthTimer();

  const ULONGLONG nowTick = (resumeTick != 0) ? resumeTick : GetTickCount64();
  const ULONGLONG sincePause =
    (mVBlankPausedSinceTick != 0 && nowTick >= mVBlankPausedSinceTick) ? (nowTick - mVBlankPausedSinceTick) : 0ULL;
  const uint32_t attempts = mVBlankHealthCheckAttempts.load(std::memory_order_acquire);
  const uint32_t drops = mVBlankConsecutiveDrops.exchange(0, std::memory_order_acq_rel);

  schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                         "ui",
                         schedulerlog::Severity::kInfo,
                         {schedulerlog::MakeField("event", "resume"),
                          schedulerlog::MakeField("count", recoveredCount),
                          schedulerlog::MakeField("pausedMs", static_cast<uint32_t>(sincePause)),
                          schedulerlog::MakeField("healthChecks", attempts),
                          schedulerlog::MakeField("drops", drops),
                          schedulerlog::MakeField("droppedTotal", mDroppedVBlank.load(std::memory_order_acquire))});

  PublishPaintBudgetSnapshot("vblank.resume", "Resume", 0, nowTick, true);

  mVBlankPausedSinceTick = 0;
  mVBlankSoftResetIssued = false;

  FlushDeferredInvalidations();
}

void IGraphicsWin::StartVBlankHealthTimer()
{
  if (!mPlugWnd)
  {
    return;
  }

  if (!mVBlankHealthTimerActive.exchange(true, std::memory_order_acq_rel))
  {
    ::SetTimer(mPlugWnd, IPLUG_VBLANK_HEALTH_TIMER_ID, kVBlankHealthCheckIntervalMs, nullptr);
  }
}

void IGraphicsWin::StopVBlankHealthTimer()
{
  if (!mPlugWnd)
  {
    mVBlankHealthTimerActive.store(false, std::memory_order_release);
    return;
  }

  if (mVBlankHealthTimerActive.exchange(false, std::memory_order_acq_rel))
  {
    ::KillTimer(mPlugWnd, IPLUG_VBLANK_HEALTH_TIMER_ID);
  }
}

void IGraphicsWin::PerformVBlankHealthCheck()
{
  if (!mVBlankPaused.load(std::memory_order_acquire))
  {
    StopVBlankHealthTimer();
    return;
  }

  const ULONGLONG nowTick = GetTickCount64();
  const ULONGLONG sincePause =
    (mVBlankPausedSinceTick != 0 && nowTick >= mVBlankPausedSinceTick) ? (nowTick - mVBlankPausedSinceTick) : 0ULL;
  const uint32_t attempt = mVBlankHealthCheckAttempts.fetch_add(1, std::memory_order_acq_rel) + 1;
  const DWORD latest = mQueuedVBlank.load(std::memory_order_acquire);
  const uint32_t drops = mVBlankConsecutiveDrops.load(std::memory_order_acquire);

  schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                         "ui",
                         schedulerlog::Severity::kInfo,
                         {schedulerlog::MakeField("event", "health_check"),
                          schedulerlog::MakeField("attempt", attempt),
                          schedulerlog::MakeField("sincePauseMs", static_cast<uint32_t>(sincePause)),
                          schedulerlog::MakeField("latestCount", latest),
                          schedulerlog::MakeField("drops", drops)});

  const bool attemptsExceeded = attempt >= kVBlankHealthAlertAttemptThreshold;
  const bool durationExceeded = sincePause >= kVBlankHealthAlertDurationMs;
  if (attemptsExceeded || durationExceeded)
  {
    schedulerlog::LogEvent(schedulerlog::kCategoryAlerts,
                           "ui",
                           schedulerlog::Severity::kError,
                           {schedulerlog::MakeField("event", "vblank_pause_alert"),
                            schedulerlog::MakeField("attempt", attempt),
                            schedulerlog::MakeField("sincePauseMs", static_cast<uint32_t>(sincePause)),
                            schedulerlog::MakeField("latestCount", latest),
                            schedulerlog::MakeField("drops", drops),
                            schedulerlog::MakeBoolField("attemptThreshold", attemptsExceeded),
                            schedulerlog::MakeBoolField("durationThreshold", durationExceeded)});
  }

  std::shared_ptr<VBlankSubscription> subscription;
  {
    std::lock_guard<std::mutex> lock(mVBlankSubscriptionMutex);
    subscription = mVBlankSubscription;
  }
  if (subscription && subscription->active.load(std::memory_order_acquire))
  {
    mPendingSyncVBlank.store(latest, std::memory_order_release);

    bool queued = true;
    if (!mVBlankMessagePending.exchange(true, std::memory_order_acq_rel))
    {
      queued = VBlankDispatchWorker::Instance().QueueDispatch(subscription, latest);
      if (!queued)
      {
        mVBlankMessagePending.store(false, std::memory_order_release);
      }
    }

    if (!queued)
    {
      schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                             "worker",
                             schedulerlog::Severity::kWarn,
                             {schedulerlog::MakeField("event", "health_queue_fail"),
                              schedulerlog::MakeField("count", latest)});
    }
  }

  if (!mVBlankSoftResetIssued && sincePause >= kVBlankPauseSoftResetThresholdMs)
  {
    RequestSwapchainSoftReset(sincePause);
    mVBlankSoftResetIssued = true;
  }
}

void IGraphicsWin::RequestSwapchainSoftReset(ULONGLONG sincePauseMs)
{
#if defined IGRAPHICS_VULKAN
  if (!mVkDevice || mVkSwapchain.handle == VK_NULL_HANDLE)
  {
    schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                           "ui",
                           schedulerlog::Severity::kInfo,
                           {schedulerlog::MakeField("event", "soft_reset_skipped"),
                            schedulerlog::MakeField("reason", "no_swapchain"),
                            schedulerlog::MakeField("pausedMs", static_cast<uint32_t>(sincePauseMs))});
    return;
  }

  schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                         "ui",
                         schedulerlog::Severity::kWarn,
                         {schedulerlog::MakeField("event", "soft_reset_request"),
                          schedulerlog::MakeField("pausedMs", static_cast<uint32_t>(sincePauseMs))});

  const bool recreated = RecreateVulkanContext();
  schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                         "ui",
                         recreated ? schedulerlog::Severity::kInfo : schedulerlog::Severity::kError,
                         {schedulerlog::MakeField("event", recreated ? "soft_reset_complete" : "soft_reset_failed"),
                          schedulerlog::MakeField("pausedMs", static_cast<uint32_t>(sincePauseMs))});
#else
  schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                         "ui",
                         schedulerlog::Severity::kInfo,
                         {schedulerlog::MakeField("event", "soft_reset_skipped"),
                          schedulerlog::MakeField("reason", "backend_not_supported"),
                          schedulerlog::MakeField("pausedMs", static_cast<uint32_t>(sincePauseMs))});
#endif
}

void IGraphicsWin::OnDisplayTimer(DWORD vBlankCount, bool fromVBlankMessage)
{
  // Check the message vblank with the current one to see if we are way behind. If so, then throw these away.
  DWORD msgCount = vBlankCount;
  DWORD curCount = mVBlankCount.load(std::memory_order_acquire);
  const bool hadPendingPaint = mPaintPending.load(std::memory_order_acquire);

  if (mVSYNCEnabled)
  {
    const bool hasVBlankMessage = fromVBlankMessage;

    auto drainVBlankMessages = [&](DWORD count) {
      DWORD newest = std::max<DWORD>(count, mQueuedVBlank.load(std::memory_order_acquire));
      MSG msg;
      while (PeekMessageW(&msg, mPlugWnd, WM_VBLANK, WM_VBLANK, PM_REMOVE))
      {
        const DWORD observed = static_cast<DWORD>(msg.wParam);
        if (observed > newest)
        {
          newest = observed;
        }
      }

      curCount = mVBlankCount.load(std::memory_order_acquire);

      if (newest > curCount)
      {
        newest = curCount;
      }

      return newest;
    };

    if (hasVBlankMessage)
    {
      msgCount = drainVBlankMessages(msgCount);
    }

    // skip until the actual vblank is at a certain number.
    if (mVBlankSkipUntil != 0 && mVBlankSkipUntil > curCount)
    {
      if (hasVBlankMessage)
      {
        mLastProcessedVBlank = std::max<DWORD>(mLastProcessedVBlank, msgCount);
        mVBlankMessagePending.store(false, std::memory_order_release);
      }
      return;
    }

    mVBlankSkipUntil = 0;

    if (hasVBlankMessage)
    {
      if (static_cast<int32_t>(msgCount - mLastProcessedVBlank) <= 0)
      {
        // The counter can wrap to zero; compare using signed arithmetic so wrapped ticks still
        // look "new" while duplicates remain filtered out.
        mLastProcessedVBlank = std::max<DWORD>(mLastProcessedVBlank, msgCount);
        mVBlankMessagePending.store(false, std::memory_order_release);
        return;
      }

      mLastProcessedVBlank = msgCount;
      mVBlankMessagePending.store(false, std::memory_order_release);
      const uint64_t handledMicros = SteadyClockMicros(std::chrono::steady_clock::now());
      RecordVBlankDispatchHandled(msgCount, handledMicros);
      ExitVBlankPaused(msgCount, GetTickCount64());
    }
    else
    {
      mLastProcessedVBlank = curCount;
    }
  }
  else if (msgCount == 0)
  {
    mLastProcessedVBlank = curCount;
  }

  if (mParamEditWnd && mParamEditMsg != kNone)
  {
    switch (mParamEditMsg)
    {
    case kCommit: {
      WCHAR strWide[MAX_WIN32_PARAM_LEN];
      SendMessageW(mParamEditWnd, WM_GETTEXT, MAX_WIN32_PARAM_LEN, (LPARAM)strWide);
      SetControlValueAfterTextEdit(UTF16AsUTF8(strWide).Get());
      DestroyEditWindow();
      break;
    }
    case kCancel:
      DestroyEditWindow();
      ClearInTextEntryControl();
      break;
    }

    mParamEditMsg = kNone;

    return; // TODO: check this!
  }

  if (mDeferInvalidation)
  {
    return;
  }

  // TODO: move this... listen to the right messages in windows for screen resolution changes, etc.
  if (!GetCapture()) // workaround Windows issues with window sizing during mouse move
  {
    RefreshPlatformScale(false);
  }

  // TODO: this is far too aggressive for slow drawing animations and data changing.  We need to
  // gate the rate of updates to a certain percentage of the wall clock time.
  IRECTList rects;
  const float totalScale = GetTotalScale();
  if (IsDirty(rects))
  {
    SetAllControlsClean();

    const int surfaceWidth = std::max<int>(1, static_cast<int>(std::ceil(WindowWidth() * totalScale)));
    const int surfaceHeight = std::max<int>(1, static_cast<int>(std::ceil(WindowHeight() * totalScale)));
    mInstancePaintBudget.Configure(surfaceWidth, surfaceHeight);

    RECT surfaceRect{0, 0, surfaceWidth, surfaceHeight};
    std::vector<RECT> batchedRects;
    batchedRects.reserve(rects.Size());

    const double surfaceArea = static_cast<double>(surfaceWidth) * static_cast<double>(surfaceHeight);
    const double tier2AreaThreshold = surfaceArea * 0.35;

    RECT smallUnion{0, 0, 0, 0};
    bool hasSmallUnion = false;
    bool escalateTier1 = false;
    bool escalateTier2 = false;

    for (int i = 0; i < rects.Size(); ++i)
    {
      IRECT dirtyR = rects.Get(i);
      dirtyR.Scale(totalScale);
      dirtyR.PixelAlign();

      RECT r = {(LONG)dirtyR.L, (LONG)dirtyR.T, (LONG)dirtyR.R, (LONG)dirtyR.B};
      const double width = static_cast<double>(std::max<LONG>(0, r.right - r.left));
      const double height = static_cast<double>(std::max<LONG>(0, r.bottom - r.top));
      const double area = width * height;

      if (area <= 4096.0)
      {
        if (!hasSmallUnion)
        {
          smallUnion = r;
          hasSmallUnion = true;
        }
        else
        {
          smallUnion.left = std::min(smallUnion.left, r.left);
          smallUnion.top = std::min(smallUnion.top, r.top);
          smallUnion.right = std::max(smallUnion.right, r.right);
          smallUnion.bottom = std::max(smallUnion.bottom, r.bottom);
        }
        continue;
      }

      if (area >= tier2AreaThreshold || width >= (surfaceWidth - 2) || height >= (surfaceHeight - 2))
      {
        escalateTier2 = true;
        break;
      }

      batchedRects.push_back(r);
      if (batchedRects.size() > 4)
      {
        escalateTier1 = true;
      }
    }

    if (escalateTier2)
    {
      batchedRects.clear();
      batchedRects.push_back(surfaceRect);
    }
    else
    {
      if (hasSmallUnion)
      {
        batchedRects.push_back(smallUnion);
      }

      if (escalateTier1 && batchedRects.size() > 1)
      {
        RECT merged = batchedRects[0];
        for (size_t idx = 1; idx < batchedRects.size(); ++idx)
        {
          merged.left = std::min(merged.left, batchedRects[idx].left);
          merged.top = std::min(merged.top, batchedRects[idx].top);
          merged.right = std::max(merged.right, batchedRects[idx].right);
          merged.bottom = std::max(merged.bottom, batchedRects[idx].bottom);
        }
        batchedRects.clear();
        batchedRects.push_back(merged);
      }
    }

    if (batchedRects.empty())
    {
      batchedRects.push_back(surfaceRect);
    }

    const int additionalRegions = static_cast<int>(batchedRects.size());
    const ULONGLONG nowTick = GetTickCount64();
    InstancePaintBudget::DecisionKind throttleReason = InstancePaintBudget::DecisionKind::kNone;
    const bool shouldThrottle = mInstancePaintBudget.ShouldThrottle(additionalRegions, nowTick, throttleReason);
    const bool throttle = shouldThrottle && hadPendingPaint;
    const RECT telemetryRect = UnionRects(batchedRects);

    if (throttle)
    {
      for (const RECT& rect : batchedRects)
      {
        mInstancePaintBudget.MergeDeferredRegion(rect);
      }

      mInstancePaintBudget.MarkNeedsDrain();
      mInstancePaintBudget.EngageBurstCooling(nowTick + kBurstCoolingWindowMs);
      mInstancePaintBudget.RecordDecision(throttleReason, additionalRegions, telemetryRect, nowTick);
      PublishPaintBudgetSnapshot("invalidate.defer", DecisionKindLabel(throttleReason), 0, nowTick, true);
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
      OnIdleThrottleTriggered(nowTick, throttleReason, additionalRegions);
#endif
    }
    else
    {
      const bool scheduledPaint = !mPaintPending.exchange(true, std::memory_order_acq_rel);
      if (scheduledPaint)
      {
        mInstancePaintBudget.OnInvalidateScheduled(additionalRegions);
      }
      else
      {
        mInstancePaintBudget.OnAdditionalInvalidationQueued(additionalRegions);
      }

      for (const RECT& rect : batchedRects)
      {
        InvalidateRect(mPlugWnd, &rect, FALSE);
      }

      if (escalateTier2)
      {
        mInstancePaintBudget.MarkNeedsDrain();
        mInstancePaintBudget.EngageBurstCooling(nowTick + kBurstCoolingWindowMs);
        mInstancePaintBudget.RecordDecision(InstancePaintBudget::DecisionKind::kTierEscalation, additionalRegions, telemetryRect, nowTick);
        PublishPaintBudgetSnapshot("invalidate.escalate", "TierEscalation", 0, nowTick, true);
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
        OnIdleThrottleTriggered(nowTick, InstancePaintBudget::DecisionKind::kTierEscalation, additionalRegions);
#endif
      }
      else if (additionalRegions > mInstancePaintBudget.BudgetCeiling())
      {
        mInstancePaintBudget.MarkNeedsDrain();
        mInstancePaintBudget.RecordDecision(InstancePaintBudget::DecisionKind::kBudgetExceeded, additionalRegions, telemetryRect, nowTick);
        PublishPaintBudgetSnapshot("invalidate.markDrain", "BudgetExceeded", 0, nowTick, true);
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
        OnIdleThrottleTriggered(nowTick, InstancePaintBudget::DecisionKind::kBudgetExceeded, additionalRegions);
#endif
      }
      else if (escalateTier1)
      {
        if (mInstancePaintBudget.EngageBurstCooling(nowTick + kBurstCoolingWindowMs))
        {
          mInstancePaintBudget.RecordDecision(InstancePaintBudget::DecisionKind::kBurstCooling, additionalRegions, telemetryRect, nowTick);
          PublishPaintBudgetSnapshot("invalidate.burstcooling", "BurstCooling", 0, nowTick, true);
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
          OnIdleThrottleTriggered(nowTick, InstancePaintBudget::DecisionKind::kBurstCooling, additionalRegions);
#endif
        }
      }
    }

    if (mParamEditWnd)
    {
      IRECT notDirtyR = mEditRECT;
      notDirtyR.Scale(totalScale);
      notDirtyR.PixelAlign();
      RECT r2 = {(LONG)notDirtyR.L, (LONG)notDirtyR.T, (LONG)notDirtyR.R, (LONG)notDirtyR.B};
      ValidateRect(mPlugWnd, &r2); // make sure we dont redraw the edit box area
      UpdateWindow(mPlugWnd);
      mParamEditMsg = kUpdate;
    }
    else if (GetResizingInProcess())
    {
      UpdateWindow(mPlugWnd);
    }
    else if (!hadPendingPaint && mVSYNCEnabled)
    {
      // Check and see if we are still in this frame.
      curCount = mVBlankCount.load(std::memory_order_acquire);
      if (msgCount != curCount)
      {
        // we are late, skip the next vblank to give us a breather.
        mVBlankSkipUntil = curCount + 1;
      }
    }
  }
  RefreshPaintBudgetHUD();
  return;
}

// static
LRESULT CALLBACK IGraphicsWin::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_CREATE)
  {
    CREATESTRUCTW* lpcs = (CREATESTRUCTW*)lParam;
    SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LPARAM)lpcs->lpCreateParams);
    IGraphicsWin* pGraphics = (IGraphicsWin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (pGraphics->mVSYNCEnabled) // use VBLANK thread
    {
      assert((pGraphics->FPS() == 60) && "If you want to run at frame rates other than 60FPS");
      pGraphics->StartVBlankThread(hWnd);
    }
    else // use WM_TIMER -- its best to get below 16ms because the windows time quanta is slightly above 15ms.
    {
      int mSec = static_cast<int>(std::floorf(1000.0f / (pGraphics->FPS())));
      if (mSec < 20)
        mSec = 15;
      SetTimer(hWnd, IPLUG_TIMER_ID, mSec, NULL);
    }

    SetFocus(hWnd); // gets scroll wheel working straight away
    DragAcceptFiles(hWnd, true);
    return 0;
  }

  IGraphicsWin* pGraphics = (IGraphicsWin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

  if (!pGraphics || hWnd != pGraphics->mPlugWnd)
  {
    return DefWindowProcW(hWnd, msg, wParam, lParam);
  }

  if (pGraphics->mParamEditWnd && pGraphics->mParamEditMsg == kEditing)
  {
    if (msg == WM_RBUTTONDOWN || (msg == WM_LBUTTONDOWN))
    {
      pGraphics->mParamEditMsg = kCancel;
      return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
  }

  auto IsTouchEvent = []() {
    const LONG_PTR c_SIGNATURE_MASK = 0xFFFFFF00;
    const LONG_PTR c_MOUSEEVENTF_FROMTOUCH = 0xFF515700;
    LONG_PTR extraInfo = GetMessageExtraInfo();
    return ((extraInfo & c_SIGNATURE_MASK) == c_MOUSEEVENTF_FROMTOUCH);
  };

  pGraphics->CheckTabletInput(msg);

  switch (msg)
  {
  case WM_VBLANK:
    pGraphics->OnDisplayTimer(static_cast<DWORD>(wParam), true);
    return 0;

  case WM_TIMER:
    if (wParam == IPLUG_TIMER_ID)
      pGraphics->OnDisplayTimer(0, false);
    else if (wParam == IPLUG_VBLANK_HEALTH_TIMER_ID)
      pGraphics->PerformVBlankHealthCheck();

    return 0;

  case WM_ERASEBKGND:
    return 0;

  case WM_RBUTTONDOWN:
  case WM_LBUTTONDOWN:
  case WM_MBUTTONDOWN: {
    if (IsTouchEvent())
      return 0;

    pGraphics->HideTooltip();
    if (pGraphics->mParamEditWnd)
    {
      pGraphics->mParamEditMsg = kCommit;
      return 0;
    }
    SetFocus(hWnd); // Added to get keyboard focus again when user clicks in window

    IMouseInfo info = pGraphics->GetMouseInfo(lParam, wParam);
    std::vector<IMouseInfo> list{info};

    pGraphics->OnMouseDown(list);

    const bool hasCapture = pGraphics->ControlIsCaptured();
    const bool osHasCapture = GetCapture() == hWnd;

    if (hasCapture && !osHasCapture)
    {
      SetCapture(hWnd);
    }
    if (!hasCapture && osHasCapture)
    {
      ReleaseCapture();
    }
    return 0;
  }
  case WM_SETCURSOR: {
    pGraphics->OnSetCursor();
    return 0;
  }
  case WM_MOUSEMOVE: {
    if (IsTouchEvent())
      return 0;

    if (!(wParam & (MK_LBUTTON | MK_RBUTTON)))
    {
      IMouseInfo info = pGraphics->GetMouseInfo(lParam, wParam);
      if (pGraphics->OnMouseOver(info.x, info.y, info.ms))
      {
        TRACKMOUSEEVENT eventTrack = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, HOVER_DEFAULT};
        if (pGraphics->TooltipsEnabled())
        {
          int c = pGraphics->GetMouseOver();
          if (c != pGraphics->mTooltipIdx)
          {
            if (c >= 0)
              eventTrack.dwFlags |= TME_HOVER;
            pGraphics->mTooltipIdx = c;
            pGraphics->HideTooltip();
          }
        }

        TrackMouseEvent(&eventTrack);
      }
    }
    else if (GetCapture() == hWnd && !pGraphics->IsInPlatformTextEntry())
    {
      float oldX = pGraphics->mCursorX;
      float oldY = pGraphics->mCursorY;

      IMouseInfo info = pGraphics->GetMouseInfo(lParam, wParam);

      info.dX = info.x - oldX;
      info.dY = info.y - oldY;

      if (info.dX || info.dY)
      {
        std::vector<IMouseInfo> list{info};
        pGraphics->OnMouseDrag(list);

        if (pGraphics->MouseCursorIsLocked())
        {
          const float x = pGraphics->mHiddenCursorX;
          const float y = pGraphics->mHiddenCursorY;

          pGraphics->MoveMouseCursor(x, y);
          pGraphics->mHiddenCursorX = x;
          pGraphics->mHiddenCursorY = y;
        }
      }
    }

    return 0;
  }
  case WM_MOUSEHOVER: {
    pGraphics->ShowTooltip();
    return 0;
  }
  case WM_MOUSELEAVE: {
    pGraphics->HideTooltip();
    pGraphics->OnMouseOut();
    return 0;
  }
  case WM_CANCELMODE: {
    const HWND osCapture = GetCapture();
    const bool resizing = pGraphics->GetResizingInProcess() && pGraphics->ControlIsCaptured();
    if (resizing)
    {
      if (osCapture != hWnd)
      {
        SetCapture(hWnd);
      }
      return 0;
    }

    const bool hadCapture = pGraphics->ControlIsCaptured() || osCapture == hWnd;
    if (hadCapture)
    {
      pGraphics->ReleaseMouseCapture();
    }

    return 0;
  }
  case WM_CAPTURECHANGED: {
    const HWND newCapture = reinterpret_cast<HWND>(lParam);
    const bool resizing = pGraphics->GetResizingInProcess() && pGraphics->ControlIsCaptured();
    if (resizing && newCapture != hWnd)
    {
      SetCapture(hWnd);
      return 0;
    }

    const bool hadCapture = (newCapture != hWnd) && (pGraphics->ControlIsCaptured() || GetCapture() == hWnd);
    if (hadCapture)
    {
      pGraphics->ReleaseMouseCapture();
    }

    return 0;
  }
  case WM_LBUTTONUP:
  case WM_RBUTTONUP: {
    IMouseInfo info = pGraphics->GetMouseInfo(lParam, wParam);
    std::vector<IMouseInfo> list{info};
    pGraphics->OnMouseUp(list);
    if (GetCapture() == hWnd)
      ReleaseCapture();
    return 0;
  }
  case WM_LBUTTONDBLCLK:
  case WM_RBUTTONDBLCLK: {
    if (IsTouchEvent())
      return 0;

    IMouseInfo info = pGraphics->GetMouseInfo(lParam, wParam);
    if (pGraphics->OnMouseDblClick(info.x, info.y, info.ms))
    {
      SetCapture(hWnd);
    }
    return 0;
  }
  case WM_MOUSEWHEEL: {
    if (pGraphics->mParamEditWnd)
    {
      pGraphics->mParamEditMsg = kCancel;
      return 0;
    }
    else
    {
      IMouseInfo info = pGraphics->GetMouseInfo(lParam, wParam);
      float d = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
      const float scale = pGraphics->GetTotalScale();
      RECT r;
      GetWindowRect(hWnd, &r);
      pGraphics->OnMouseWheel(info.x - (r.left / scale), info.y - (r.top / scale), info.ms, d);
      return 0;
    }
  }
  case WM_DPICHANGED: {
    if (const RECT* suggested = reinterpret_cast<const RECT*>(lParam))
    {
      SetWindowPos(hWnd,
                   nullptr,
                   suggested->left,
                   suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }

    pGraphics->RefreshPlatformScale(true);
    return 0;
  }
  case WM_WINDOWPOSCHANGING: {
    if (WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(lParam))
    {
      if (!(wp->flags & SWP_NOMOVE))
      {
        pGraphics->mDeferInvalidation = true;
      }
    }
    break;
  }
  case WM_WINDOWPOSCHANGED: {
    pGraphics->mDeferInvalidation = false;
    if (WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(lParam))
    {
      if (!(wp->flags & SWP_NOSIZE))
      {
        pGraphics->SetAllControlsDirty();
        InvalidateRect(hWnd, nullptr, FALSE);
      }
    }
    break;
  }
  case WM_TOUCH: {
    UINT nTouches = LOWORD(wParam);

    if (nTouches > 0)
    {
      WDL_TypedBuf<TOUCHINPUT> touches;
      touches.Resize(nTouches);
      HTOUCHINPUT hTouchInput = (HTOUCHINPUT)lParam;
      std::vector<IMouseInfo> downlist;
      std::vector<IMouseInfo> uplist;
      std::vector<IMouseInfo> movelist;
      const float scale = pGraphics->GetTotalScale();

      GetTouchInputInfo(hTouchInput, nTouches, touches.Get(), sizeof(TOUCHINPUT));

      for (int i = 0; i < nTouches; i++)
      {
        TOUCHINPUT* pTI = touches.Get() + i;

        POINT pt;
        pt.x = TOUCH_COORD_TO_PIXEL(pTI->x);
        pt.y = TOUCH_COORD_TO_PIXEL(pTI->y);
        ScreenToClient(pGraphics->mPlugWnd, &pt);

        IMouseInfo info;
        info.x = static_cast<float>(pt.x) / scale;
        info.y = static_cast<float>(pt.y) / scale;
        info.dX = 0.f;
        info.dY = 0.f;
        info.ms.touchRadius = 0;

        if (pTI->dwMask & TOUCHINPUTMASKF_CONTACTAREA)
        {
          info.ms.touchRadius = pTI->cxContact;
        }

        info.ms.touchID = static_cast<ITouchID>(pTI->dwID);

        if (pTI->dwFlags & TOUCHEVENTF_DOWN)
        {
          downlist.push_back(info);
          pGraphics->mDeltaCapture.insert(std::make_pair(info.ms.touchID, info));
        }
        else if (pTI->dwFlags & TOUCHEVENTF_UP)
        {
          pGraphics->mDeltaCapture.erase(info.ms.touchID);
          uplist.push_back(info);
        }
        else if (pTI->dwFlags & TOUCHEVENTF_MOVE)
        {
          IMouseInfo previous = pGraphics->mDeltaCapture.find(info.ms.touchID)->second;
          info.dX = info.x - previous.x;
          info.dY = info.y - previous.y;
          movelist.push_back(info);
          pGraphics->mDeltaCapture[info.ms.touchID] = info;
        }
      }

      if (downlist.size())
        pGraphics->OnMouseDown(downlist);

      if (uplist.size())
        pGraphics->OnMouseUp(uplist);

      if (movelist.size())
        pGraphics->OnMouseDrag(movelist);

      CloseTouchInputHandle(hTouchInput);
    }
    return 0;
  }
  case WM_GETDLGCODE:
    return DLGC_WANTALLKEYS;
  case WM_KEYDOWN:
  case WM_KEYUP: {
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(hWnd, &p);

    BYTE keyboardState[256] = {};
    GetKeyboardState(keyboardState);
    const int keyboardScanCode = (lParam >> 16) & 0x00ff;
    WORD character = 0;
    const int len = ToAscii(wParam, keyboardScanCode, keyboardState, &character, 0);
    // TODO: should get unicode?
    bool handle = false;

    // send when len is 0 because wParam might be something like VK_LEFT or VK_HOME, etc.
    if (len == 0 || len == 1)
    {
      char str[2];
      str[0] = static_cast<char>(character);
      str[1] = '\0';

      IKeyPress keyPress{
        str, static_cast<int>(wParam), static_cast<bool>(GetKeyState(VK_SHIFT) & 0x8000), static_cast<bool>(GetKeyState(VK_CONTROL) & 0x8000), static_cast<bool>(GetKeyState(VK_MENU) & 0x8000)};

      const float scale = pGraphics->GetTotalScale();

      if (msg == WM_KEYDOWN)
        handle = pGraphics->OnKeyDown(p.x / scale, p.y / scale, keyPress);
      else
        handle = pGraphics->OnKeyUp(p.x / scale, p.y / scale, keyPress);
    }

    if (!handle)
    {
      HWND rootHWnd = GetAncestor(hWnd, GA_ROOT);
      SendMessageW(rootHWnd, msg, wParam, lParam);
      return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    else
      return 0;
  }
  case WM_PAINT: {
    const float scale = pGraphics->GetTotalScale();
    const bool hadPaintPending = pGraphics->mPaintPending.exchange(false, std::memory_order_acq_rel);
    auto addDrawRect = [pGraphics, scale](IRECTList& rects, RECT r) {
      IRECT ir(r.left, r.top, r.right, r.bottom);
      ir.Scale(1.f / scale);
      ir.PixelAlign();
      rects.Add(ir);
    };

    HRGN region = CreateRectRgn(0, 0, 0, 0);
    int regionType = GetUpdateRgn(hWnd, region, FALSE);

    int drainedRegionCount = 0;
    RECT drainedBounds{0, 0, 0, 0};
    bool hasDrainedBounds = false;

    auto updateBounds = [&](const RECT& r) {
      if (!hasDrainedBounds)
      {
        drainedBounds = r;
        hasDrainedBounds = true;
      }
      else
      {
        drainedBounds.left = std::min(drainedBounds.left, r.left);
        drainedBounds.top = std::min(drainedBounds.top, r.top);
        drainedBounds.right = std::max(drainedBounds.right, r.right);
        drainedBounds.bottom = std::max(drainedBounds.bottom, r.bottom);
      }
    };

    if ((regionType == COMPLEXREGION) || (regionType == SIMPLEREGION))
    {
      IRECTList rects;
      const int bufferSize = sizeof(RECT) * 64;
      unsigned char stackBuffer[sizeof(RGNDATA) + bufferSize];
      RGNDATA* regionData = (RGNDATA*)stackBuffer;

      if (regionType == COMPLEXREGION && GetRegionData(region, bufferSize, regionData))
      {
        for (int i = 0; i < regionData->rdh.nCount; i++)
        {
          RECT r = *(((RECT*)regionData->Buffer) + i);
          addDrawRect(rects, r);
          updateBounds(r);
        }
      }
      else
      {
        RECT r;
        GetRgnBox(region, &r);
        addDrawRect(rects, r);
        updateBounds(r);
      }

      drainedRegionCount = std::max(rects.Size(), 1);

#if defined IGRAPHICS_GL || defined IGRAPHICS_VULKAN //|| IGRAPHICS_D2D
      PAINTSTRUCT ps;
      BeginPaint(hWnd, &ps);
#endif

      {
#if defined IGRAPHICS_VULKAN
        // When the Vulkan backend is shutting down (e.g. during CloseWindow), the device and
        // swap-chain handles are reset prior to the HWND being destroyed. Skip drawing in that
        // window to avoid dereferencing torn-down state while lingering WM_PAINT messages drain.
        const bool hasVulkanContext = (pGraphics->mVkDevice != VK_NULL_HANDLE &&
                                       pGraphics->mVkSwapchain.handle != VK_NULL_HANDLE);
#else
        const bool hasVulkanContext = true;
#endif

        if (hasVulkanContext)
        {
#if defined IGRAPHICS_GL
        ScopedGraphicsContext scopedGLCtx{pGraphics};
        pGraphics->Draw(rects);
        SwapBuffers((HDC)pGraphics->GetPlatformContext());
#else
        pGraphics->Draw(rects);
#endif
        }
      }

#if defined IGRAPHICS_GL || defined IGRAPHICS_VULKAN || defined IGRAPHICS_D2D
      EndPaint(hWnd, &ps);
#endif
    }

    // For the D2D if we don't call endpaint, then you really need to call ValidateRect otherwise
    // we are just going to get another WM_PAINT to handle.  Bad!  It also exibits the odd property
    // that windows will be popped under the window.
    ValidateRect(hWnd, 0);

    if (hadPaintPending)
    {
      const ULONGLONG nowTick = GetTickCount64();
      pGraphics->mInstancePaintBudget.OnPaintCompleted(drainedRegionCount, nowTick);
      RECT decisionRect = hasDrainedBounds ? drainedBounds : RECT{0, 0, 0, 0};
      pGraphics->mInstancePaintBudget.RecordDecision(InstancePaintBudget::DecisionKind::kDrainComplete, drainedRegionCount, decisionRect, nowTick);
      pGraphics->PublishPaintBudgetSnapshot("paint.completed", "DrainComplete", drainedRegionCount, nowTick, true);
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
      pGraphics->OnIdleDrainComplete(nowTick, drainedRegionCount);
#endif
      pGraphics->FlushDeferredInvalidations();
    }

    DeleteObject(region);

    return 0;
  }

  case WM_CTLCOLOREDIT: {
    if (!pGraphics->mParamEditWnd)
      return 0;

    const IText& text = pGraphics->mEditText;
    HDC dc = (HDC)wParam;
    SetBkColor(dc, RGB(text.mTextEntryBGColor.R, text.mTextEntryBGColor.G, text.mTextEntryBGColor.B));
    SetTextColor(dc, RGB(text.mTextEntryFGColor.R, text.mTextEntryFGColor.G, text.mTextEntryFGColor.B));
    SetBkMode(dc, OPAQUE);
    SetDCBrushColor(dc, RGB(text.mTextEntryBGColor.R, text.mTextEntryBGColor.G, text.mTextEntryBGColor.B));
    return (LRESULT)GetStockObject(DC_BRUSH);
  }
  case WM_DROPFILES: {
    HDROP hdrop = (HDROP)wParam;

    int numDroppedFiles = DragQueryFileW(hdrop, -1, nullptr, 0);

    std::vector<std::vector<char>> pathBuffers(numDroppedFiles, std::vector<char>(1025, 0));
    std::vector<const char*> pathPtrs(numDroppedFiles);

    for (int i = 0; i < numDroppedFiles; i++)
    {
      wchar_t pathBufferW[1025] = {'\0'};
      DragQueryFileW(hdrop, i, pathBufferW, 1024);
      strncpy(pathBuffers[i].data(), UTF16AsUTF8(pathBufferW).Get(), 1024);
      pathPtrs[i] = pathBuffers[i].data();
    }
    POINT p;
    DragQueryPoint(hdrop, &p);

    const float scale = pGraphics->GetTotalScale();

    if (numDroppedFiles == 1)
    {
      pGraphics->OnDrop(&pathPtrs[0][0], p.x / scale, p.y / scale);
    }
    else
    {
      pGraphics->OnDropMultiple(pathPtrs, p.x / scale, p.y / scale);
    }

    return 0;
  }
  case WM_CLOSE: {
    pGraphics->CloseWindow();
    return 0;
  }
  case WM_SETFOCUS: {
    return 0;
  }
  case WM_KILLFOCUS: {
    return 0;
  }
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// static
LRESULT CALLBACK IGraphicsWin::ParamEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  IGraphicsWin* pGraphics = (IGraphicsWin*)GetWindowLongPtrW(GetParent(hWnd), GWLP_USERDATA);

  if (pGraphics && pGraphics->mParamEditWnd && pGraphics->mParamEditWnd == hWnd)
  {
    pGraphics->HideTooltip();

    switch (msg)
    {
    case WM_CHAR: {
      // limit to numbers for text entry on appropriate parameters
      if (pGraphics->mEditParam)
      {
        char c = wParam;

        if (c == 0x08)
          break; // backspace

        switch (pGraphics->mEditParam->Type())
        {
        case IParam::kTypeEnum:
        case IParam::kTypeInt:
        case IParam::kTypeBool:
          if (c >= '0' && c <= '9')
            break;
          else if (c == '-')
            break;
          else if (c == '+')
            break;
          else
            return 0;
        case IParam::kTypeDouble:
          if (c >= '0' && c <= '9')
            break;
          else if (c == '-')
            break;
          else if (c == '+')
            break;
          else if (c == '.')
            break;
          else
            return 0;
        default:
          break;
        }
      }
      break;
    }
    case WM_KEYDOWN: {
      if (wParam == VK_RETURN)
      {
        pGraphics->mParamEditMsg = kCommit;
        return 0;
      }
      else if (wParam == VK_ESCAPE)
      {
        pGraphics->mParamEditMsg = kCancel;
        return 0;
      }
      break;
    }
    case WM_SETFOCUS: {
      pGraphics->mParamEditMsg = kEditing;
      break;
    }
    case WM_KILLFOCUS: {
      pGraphics->mParamEditMsg = kCommit;
      break;
    }
    // handle WM_GETDLGCODE so that we can say that we want the return key message
    //  (normally single line edit boxes don't get sent return key messages)
    case WM_GETDLGCODE: {
      LPARAM lres;
      // find out if the original control wants it
      lres = CallWindowProcW(pGraphics->mDefEditProc, hWnd, WM_GETDLGCODE, wParam, lParam);
      // add in that we want it if it is a return keydown
      if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN && wParam == VK_RETURN)
      {
        lres |= DLGC_WANTMESSAGE;
      }
      return lres;
    }
    case WM_COMMAND: {
      switch
        HIWORD(wParam)
        {
        case CBN_SELCHANGE: {
          if (pGraphics->mParamEditWnd)
          {
            pGraphics->mParamEditMsg = kCommit;
            return 0;
          }
        }
        }
      break; // Else let the default proc handle it.
    }
    }
    return CallWindowProcW(pGraphics->mDefEditProc, hWnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

IGraphicsWin::IGraphicsWin(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
  : IGRAPHICS_DRAW_CLASS(dlg, w, h, fps, scale)
{
  StaticStorage<InstalledFont>::Accessor fontStorage(sPlatformFontCache);
  StaticStorage<HFontHolder>::Accessor hfontStorage(sHFontCache);
  fontStorage.Retain();
  hfontStorage.Retain();

#ifndef IGRAPHICS_DISABLE_VSYNC
  mVSYNCEnabled = IsWindows8OrGreater();
#endif

  InitializeIdlePacingConfiguration();
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  InitializeIdleSchedulerState();
#endif
}

IGraphicsWin::~IGraphicsWin()
{
  StaticStorage<InstalledFont>::Accessor fontStorage(sPlatformFontCache);
  StaticStorage<HFontHolder>::Accessor hfontStorage(sHFontCache);
  fontStorage.Release();
  hfontStorage.Release();
  if (mPaintPending.exchange(false, std::memory_order_acq_rel))
  {
    mInstancePaintBudget.OnPaintCompleted(1, GetTickCount64());
  }
  DestroyEditWindow();
  CloseWindow();
}

static void GetWindowSize(HWND pWnd, int* pW, int* pH)
{
  if (pWnd)
  {
    RECT r;
    GetWindowRect(pWnd, &r);
    *pW = r.right - r.left;
    *pH = r.bottom - r.top;
  }
  else
  {
    *pW = *pH = 0;
  }
}

void IGraphicsWin::OnIdlePacingModeChanged(EIdlePacingMode mode)
{
  IGRAPHICS_DRAW_CLASS::OnIdlePacingModeChanged(mode);
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  schedulerlog::LogEvent(schedulerlog::kCategoryRollout,
                         mIdlePacingModeFromConfig ? "config" : "runtime",
                         schedulerlog::Severity::kInfo,
                         {schedulerlog::MakeField("event", "mode_changed"),
                          schedulerlog::MakeStringField("mode", IdlePacingModeToString(mode)),
                          schedulerlog::MakeField("hwnd", reinterpret_cast<uintptr_t>(mPlugWnd)),
                          schedulerlog::MakeBoolField("fromConfig", mIdlePacingModeFromConfig)});
  ResetIdleSchedulerState(mode, GetTickCount64());
#endif
}

void IGraphicsWin::OnHostIdleTick()
{
  if (IsDispatchingLegacyHostIdleTick())
    return;

  HostIdleTickInfo info{};
  OnHostIdleTick(info);
}

void IGraphicsWin::OnHostIdleTick(const HostIdleTickInfo& info)
{
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  const ULONGLONG now = GetTickCount64();
  mSchedulerState.lastIdleTick = now;

  const int forgivenessRequestMs = ConsumeIdleForgivenessRequest();
  bool forgivenessExtended = false;

  const int queueBefore = std::max(info.paramQueueDepthBefore, 0);
  const int queueAfter = std::max(info.paramQueueDepthAfter, 0);
  const int backlogBefore = std::max(queueBefore, info.paramMessagesProcessed);
  const int outstanding = std::max(backlogBefore, queueAfter);

  mSchedulerState.lastIdleOutstanding = outstanding;
  mSchedulerState.lastIdleParamDepthBefore = info.paramQueueDepthBefore;
  mSchedulerState.lastIdleParamDepthAfter = info.paramQueueDepthAfter;
  mSchedulerState.lastIdleProcessed = info.paramMessagesProcessed;
  mSchedulerState.lastIdleElapsedMs = info.elapsedMs;
  mSchedulerState.lastIdleTimerBehind = info.timerFellBehind;
  mSchedulerState.lastIdleSampleTick = now;
  mSchedulerState.paramQueueHighWater = std::max(mSchedulerState.paramQueueHighWater, outstanding);

  schedulerlog::Severity queueSeverity = schedulerlog::Severity::kDebug;
  uint32_t overThresholdMs = 0;

  if (outstanding > 0)
  {
    queueSeverity = schedulerlog::Severity::kInfo;
  }

  if (outstanding > kParamQueueWarnThreshold)
  {
    if (mSchedulerState.paramQueueAboveThresholdSince == 0)
    {
      mSchedulerState.paramQueueAboveThresholdSince = now;
    }

    if (now >= mSchedulerState.paramQueueAboveThresholdSince)
    {
      const ULONGLONG duration = now - mSchedulerState.paramQueueAboveThresholdSince;
      overThresholdMs = static_cast<uint32_t>(std::min<ULONGLONG>(duration, std::numeric_limits<uint32_t>::max()));
      if (outstanding > kParamQueueErrorThreshold && duration >= static_cast<ULONGLONG>(kParamQueueErrorWindowMs))
      {
        queueSeverity = schedulerlog::Severity::kError;
      }
      else
      {
        queueSeverity = schedulerlog::Severity::kWarn;
      }
    }
  }
  else
  {
    mSchedulerState.paramQueueAboveThresholdSince = 0;
  }

  schedulerlog::LogEvent(schedulerlog::kCategoryParamQueueDepth,
                         "idle_tick",
                         queueSeverity,
                         {schedulerlog::MakeField("outstanding", outstanding),
                          schedulerlog::MakeField("depthBefore", queueBefore),
                          schedulerlog::MakeField("depthAfter", queueAfter),
                          schedulerlog::MakeField("processed", info.paramMessagesProcessed),
                          schedulerlog::MakeField("elapsedMs", info.elapsedMs),
                          schedulerlog::MakeBoolField("timerFellBehind", info.timerFellBehind),
                          schedulerlog::MakeField("overThresholdMs", overThresholdMs),
                          schedulerlog::MakeField("highWater", mSchedulerState.paramQueueHighWater)});
  RecordParamQueueTelemetry(outstanding, queueSeverity);

  if (GetIdlePacingMode() == EIdlePacingMode::Adaptive)
  {
    if (outstanding > 0)
    {
      mSchedulerState.pendingParamFlush = std::max(mSchedulerState.pendingParamFlush, outstanding);
      const int windowMs = std::max(forgivenessRequestMs, kIdleForgivenessDefaultMs);
      forgivenessExtended |= MaybeExtendIdleForgiveness(now, windowMs, info, "queue_backlog");
    }
    else if (forgivenessRequestMs > 0)
    {
      forgivenessExtended |= MaybeExtendIdleForgiveness(now, forgivenessRequestMs, info, "requested");
    }

    if (!forgivenessExtended && info.timerFellBehind && info.elapsedMs > 0.0)
    {
      const int stretchMs = std::max(static_cast<int>(std::lround(info.elapsedMs)), kIdleForgivenessMinMs);
      forgivenessExtended |= MaybeExtendIdleForgiveness(now, stretchMs, info, "timer_stretch", schedulerlog::Severity::kDebug);
    }

    if (info.paramMessagesProcessed > 0 && mSchedulerState.pendingParamFlush > 0)
    {
      const int processed = std::max(info.paramMessagesProcessed, 0);
      mSchedulerState.pendingParamFlush = std::max(0, mSchedulerState.pendingParamFlush - processed);
    }
    else if (info.paramMessagesProcessed == 0 && outstanding == 0 && mSchedulerState.pendingParamFlush > 0)
    {
      --mSchedulerState.pendingParamFlush;
    }

    if (mSchedulerState.throttleState == SchedulerState::ThrottleState::kIdleCatchUp)
    {
      if (mSchedulerState.pendingParamFlush <= 0)
      {
        EnterIdleState(SchedulerState::ThrottleState::kNormal, 1.0, "catchup_complete", now, {});
      }
    }
    else if (mSchedulerState.throttleState == SchedulerState::ThrottleState::kBurstCooling)
    {
      if (!mInstancePaintBudget.NeedsDrain() &&
          mInstancePaintBudget.PendingPaints() == 0 &&
          mInstancePaintBudget.QueuedInvalidates() == 0)
      {
        mSchedulerState.pendingParamFlush = 0;
        EnterIdleState(SchedulerState::ThrottleState::kNormal, 1.0, "burst_idle", now, {});
      }
    }

    if (outstanding == 0 && mSchedulerState.pendingParamFlush <= 0 && forgivenessRequestMs == 0)
    {
      MaybeExpireIdleForgiveness(now, forgivenessExtended, info);
    }
  }
#else
  (void) info;
#endif

  DispatchLegacyHostIdleTick();
}

void IGraphicsWin::InitializeIdlePacingConfiguration()
{
  if (mIdlePacingModeInitialized)
    return;

#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  if (mIdlePacingConfigPath.GetLength() == 0)
  {
    WDL_String basePath;
    AppSupportPath(basePath, false);

    if (basePath.GetLength() > 0)
    {
      WDL_String resolved(basePath);
      const int lastIndex = resolved.GetLength() - 1;
      const char lastChar = resolved.Get()[lastIndex];
      if (lastChar != '\\' && lastChar != '/')
      {
        resolved.Append("\\");
      }
      resolved.Append("iPlug2\\IPlugSettings.json");
      mIdlePacingConfigPath.Set(resolved.Get());
    }
  }

  if (mIdlePacingConfigPath.GetLength() > 0)
  {
    LoadIdlePacingModeFromConfigFile(mIdlePacingConfigPath.Get());
  }
#endif

  mIdlePacingModeInitialized = true;
}

void IGraphicsWin::RefreshIdlePacingModeFromDefaultConfig()
{
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  if (!mIdlePacingModeInitialized)
  {
    InitializeIdlePacingConfiguration();
    return;
  }

  if (mIdlePacingConfigPath.GetLength() > 0)
  {
    LoadIdlePacingModeFromConfigFile(mIdlePacingConfigPath.Get());
  }
#else
  DBGMSG("IGraphicsWin: idle pacing configuration refresh ignored because experimental scheduler is disabled\n");
#endif
}

void IGraphicsWin::LoadIdlePacingModeFromConfigFile(const char* filePath)
{
#if !IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  (void) filePath;
  return;
#else
  if (!filePath || !filePath[0])
    return;

  EIdlePacingMode parsed = EIdlePacingMode::Legacy;
  const IdlePacingConfigStatus status = ParseIdlePacingModeFromSettings(filePath, parsed);
  if (status != IdlePacingConfigStatus::kOk)
  {
    schedulerlog::LogEvent(schedulerlog::kCategoryRollout,
                           "config",
                           schedulerlog::Severity::kWarn,
                           {schedulerlog::MakeField("event", "config_load_failed"),
                            schedulerlog::MakeStringField("path", filePath),
                            schedulerlog::MakeStringField("status", IdlePacingConfigStatusToString(status))});
    return;
  }

  mIdlePacingConfigPath.Set(filePath);
  mIdlePacingModeFromConfig = true;
  if (!ApplyIdlePacingModeString(IdlePacingModeToString(parsed), true))
  {
    schedulerlog::LogEvent(schedulerlog::kCategoryRollout,
                           "config",
                           schedulerlog::Severity::kWarn,
                           {schedulerlog::MakeField("event", "config_apply_failed"),
                            schedulerlog::MakeStringField("path", filePath)});
    return;
  }

  schedulerlog::LogEvent(schedulerlog::kCategoryRollout,
                         "config",
                         schedulerlog::Severity::kInfo,
                         {schedulerlog::MakeField("event", "config_applied"),
                          schedulerlog::MakeStringField("path", filePath),
                          schedulerlog::MakeStringField("mode", IdlePacingModeToString(parsed))});

  DBGMSG("IGraphicsWin: idle pacing mode set to %s from %s\n", IdlePacingModeToString(parsed), filePath);
#endif
}

IGraphicsWin::IdlePacingConfigStatus IGraphicsWin::ParseIdlePacingModeFromSettings(const char* path,
                                                                                  EIdlePacingMode& modeOut) const
{
#if !IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  (void) path;
  (void) modeOut;
  return IdlePacingConfigStatus::kFileMissing;
#else
  if (!path || !path[0])
    return IdlePacingConfigStatus::kFileMissing;

  std::ifstream stream(UTF8AsUTF16(path).Get());
  if (!stream.is_open())
    return IdlePacingConfigStatus::kFileMissing;

  std::stringstream buffer;
  buffer << stream.rdbuf();
  std::string contents = buffer.str();

  std::string extracted;
  if (!ExtractJsonStringForKey(contents, "idlePacingMode", extracted))
    return IdlePacingConfigStatus::kMissingKey;

  if (!ParseIdlePacingModeStringInternal(extracted, modeOut))
    return IdlePacingConfigStatus::kInvalidValue;

  return IdlePacingConfigStatus::kOk;
#endif
}

bool IGraphicsWin::ApplyIdlePacingModeString(const std::string& modeString, bool fromConfig)
{
  EIdlePacingMode parsed = EIdlePacingMode::Legacy;
  if (!ParseIdlePacingModeStringInternal(modeString, parsed))
  {
    schedulerlog::LogEvent(schedulerlog::kCategoryRollout,
                           fromConfig ? "config" : "runtime",
                           schedulerlog::Severity::kWarn,
                           {schedulerlog::MakeField("event", "mode_parse_failed"),
                            schedulerlog::MakeField("input", modeString)});
    DBGMSG("IGraphicsWin: unrecognised idle pacing mode '%s'\n", modeString.c_str());
    return false;
  }

#if !IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  if (parsed != EIdlePacingMode::Legacy)
  {
    schedulerlog::LogEvent(schedulerlog::kCategoryRollout,
                           fromConfig ? "config" : "runtime",
                           schedulerlog::Severity::kWarn,
                           {schedulerlog::MakeField("event", "mode_rejected_disabled"),
                            schedulerlog::MakeField("requested", modeString)});
    DBGMSG("IGraphicsWin: requested idle pacing mode '%s' ignored because experimental scheduler is disabled\n", modeString.c_str());
    parsed = EIdlePacingMode::Legacy;
  }
#endif

  const EIdlePacingMode previous = GetIdlePacingMode();
  if (!fromConfig)
  {
    mIdlePacingModeFromConfig = false;
  }

  SetIdlePacingMode(parsed);
  const EIdlePacingMode effective = GetIdlePacingMode();
  const bool changed = (effective != previous);

#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  const schedulerlog::Severity severity = changed ? schedulerlog::Severity::kInfo : schedulerlog::Severity::kDebug;
  schedulerlog::LogEvent(schedulerlog::kCategoryRollout,
                         fromConfig ? "config" : "runtime",
                         severity,
                         {schedulerlog::MakeField("event", changed ? "mode_applied" : "mode_unchanged"),
                          schedulerlog::MakeField("requested", modeString),
                          schedulerlog::MakeStringField("effective", IdlePacingModeToString(effective)),
                          schedulerlog::MakeStringField("previous", IdlePacingModeToString(previous)),
                          schedulerlog::MakeBoolField("fromConfig", fromConfig),
                          schedulerlog::MakeBoolField("changed", changed),
                          schedulerlog::MakeStringField("configPath",
                                                        (fromConfig && mIdlePacingConfigPath.GetLength() > 0)
                                                          ? mIdlePacingConfigPath.Get()
                                                          : ""))});
#else
  (void) effective;
  (void) previous;
  (void) changed;
#endif

  DBGMSG("IGraphicsWin: idle pacing mode set to %s\n", IdlePacingModeToString(parsed));
  return true;
}

bool IGraphicsWin::ApplySchedulerConsoleCommand(const char* command)
{
  if (!command)
    return false;

  std::string trimmed = TrimCopy(command);
  if (trimmed.empty())
    return false;

  std::string lowered = ToLowerCopy(trimmed);

  if (lowered == "sched_idle_legacy")
    return ApplyIdlePacingModeString("legacy");

  if (lowered == "sched_idle_adaptive")
    return ApplyIdlePacingModeString("adaptive");

  if (lowered == "sched_idle_locked60" || lowered == "sched_idle_locked60hz")
    return ApplyIdlePacingModeString("locked60");

  const std::string directive = "sched.idle.mode";
  if (lowered.rfind(directive, 0) == 0)
  {
    std::string argument = trimmed.substr(directive.size());
    size_t valueStart = argument.find_first_not_of(" \t=:");
    std::string value = valueStart == std::string::npos ? std::string{} : argument.substr(valueStart);
    return ApplyIdlePacingModeString(value);
  }

  return false;
}

static bool IsChildWindow(HWND pWnd)
{
  if (pWnd)
  {
    int style = GetWindowLongW(pWnd, GWL_STYLE);
    int exStyle = GetWindowLongW(pWnd, GWL_EXSTYLE);
    return ((style & WS_CHILD) && !(exStyle & WS_EX_MDICHILD));
  }
  return false;
}

void IGraphicsWin::ForceEndUserEdit() { mParamEditMsg = kCancel; }

static UINT SETPOS_FLAGS = SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE;

void IGraphicsWin::PlatformResize(bool parentHasResized)
{
  if (WindowIsOpen())
  {
    HWND pParent = 0, pGrandparent = 0;
    int dlgW = 0, dlgH = 0, parentW = 0, parentH = 0, grandparentW = 0, grandparentH = 0;
    GetWindowSize(mPlugWnd, &dlgW, &dlgH);
    const float windowScale = GetBackingPixelScaleForParentResize();
    const float targetScale = (windowScale > 0.f && std::isfinite(windowScale)) ? windowScale : GetScreenScale();
    const int targetWidth = static_cast<int>(std::round(static_cast<float>(WindowWidth()) * targetScale));
    const int targetHeight = static_cast<int>(std::round(static_cast<float>(WindowHeight()) * targetScale));
    int dw = targetWidth - dlgW;
    int dh = targetHeight - dlgH;

    if (IsChildWindow(mPlugWnd))
    {
      pParent = GetParent(mPlugWnd);
      GetWindowSize(pParent, &parentW, &parentH);

      if (IsChildWindow(pParent))
      {
        pGrandparent = GetParent(pParent);
        GetWindowSize(pGrandparent, &grandparentW, &grandparentH);
      }
    }

    if (!dw && !dh)
      return;

    SetWindowPos(mPlugWnd, 0, 0, 0, dlgW + dw, dlgH + dh, SETPOS_FLAGS);

    const bool forceParentResize = mBypassHostContentScale;

    if (pParent && (!parentHasResized || forceParentResize))
    {
      SetWindowPos(pParent, 0, 0, 0, parentW + dw, parentH + dh, SETPOS_FLAGS);
    }

    if (pGrandparent && (!parentHasResized || forceParentResize))
    {
      SetWindowPos(pGrandparent, 0, 0, 0, grandparentW + dw, grandparentH + dh, SETPOS_FLAGS);
    }
  }
}

void IGraphicsWin::HideMouseCursor(bool hide, bool lock)
{
  if (mCursorHidden == hide)
    return;

  if (hide)
  {
    mHiddenCursorX = mCursorX;
    mHiddenCursorY = mCursorY;

    ShowCursor(false);
    mCursorHidden = true;
    mCursorLock = lock && !mTabletInput;
  }
  else
  {
    if (mCursorLock)
      MoveMouseCursor(mHiddenCursorX, mHiddenCursorY);

    ShowCursor(true);
    mCursorHidden = false;
    mCursorLock = false;
  }
}

void IGraphicsWin::MoveMouseCursor(float x, float y)
{
  if (mTabletInput)
    return;

  const float scale = GetTotalScale();

  POINT p;
  p.x = std::round(x * scale);
  p.y = std::round(y * scale);

  ::ClientToScreen(mPlugWnd, &p);

  if (SetCursorPos(p.x, p.y))
  {
    GetCursorPos(&p);
    ScreenToClient(mPlugWnd, &p);

    mHiddenCursorX = mCursorX = p.x / scale;
    mHiddenCursorY = mCursorY = p.y / scale;
  }
}

ECursor IGraphicsWin::SetMouseCursor(ECursor cursorType)
{
  HCURSOR cursor;

  switch (cursorType)
  {
  case ECursor::ARROW:
    cursor = LoadCursor(NULL, IDC_ARROW);
    break;
  case ECursor::IBEAM:
    cursor = LoadCursor(NULL, IDC_IBEAM);
    break;
  case ECursor::WAIT:
    cursor = LoadCursor(NULL, IDC_WAIT);
    break;
  case ECursor::CROSS:
    cursor = LoadCursor(NULL, IDC_CROSS);
    break;
  case ECursor::UPARROW:
    cursor = LoadCursor(NULL, IDC_UPARROW);
    break;
  case ECursor::SIZENWSE:
    cursor = LoadCursor(NULL, IDC_SIZENWSE);
    break;
  case ECursor::SIZENESW:
    cursor = LoadCursor(NULL, IDC_SIZENESW);
    break;
  case ECursor::SIZEWE:
    cursor = LoadCursor(NULL, IDC_SIZEWE);
    break;
  case ECursor::SIZENS:
    cursor = LoadCursor(NULL, IDC_SIZENS);
    break;
  case ECursor::SIZEALL:
    cursor = LoadCursor(NULL, IDC_SIZEALL);
    break;
  case ECursor::INO:
    cursor = LoadCursor(NULL, IDC_NO);
    break;
  case ECursor::HAND:
    cursor = LoadCursor(NULL, IDC_HAND);
    break;
  case ECursor::APPSTARTING:
    cursor = LoadCursor(NULL, IDC_APPSTARTING);
    break;
  case ECursor::HELP:
    cursor = LoadCursor(NULL, IDC_HELP);
    break;
  default:
    cursor = LoadCursor(NULL, IDC_ARROW);
  }

  SetCursor(cursor);
  return IGraphics::SetMouseCursor(cursorType);
}

bool IGraphicsWin::MouseCursorIsLocked() { return mCursorLock; }

void IGraphicsWin::GetMouseLocation(float& x, float& y) const
{
  POINT p;
  GetCursorPos(&p);
  ScreenToClient(mPlugWnd, &p);

  const float scale = GetTotalScale();

  x = p.x / scale;
  y = p.y / scale;
}

void IGraphicsWin::PlatformReleaseMouseCapture()
{
  if (mPlugWnd && GetCapture() == mPlugWnd)
  {
    ReleaseCapture();
  }
}

void IGraphicsWin::PlatformOnCaptureFinished(ITouchID touchID)
{
  auto itr = mDeltaCapture.find(touchID);

  if (itr != mDeltaCapture.end())
  {
    mDeltaCapture.erase(itr);
  }
}

#ifdef IGRAPHICS_GL
void IGraphicsWin::CreateGLContext()
{
  PIXELFORMATDESCRIPTOR pfd = {sizeof(PIXELFORMATDESCRIPTOR),
                               1,
                               PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, // Flags
                               PFD_TYPE_RGBA,                                              // The kind of framebuffer. RGBA or palette.
                               32,                                                         // Colordepth of the framebuffer.
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               24, // Number of bits for the depthbuffer
                               8,  // Number of bits for the stencilbuffer
                               0,  // Number of Aux buffers in the framebuffer.
                               PFD_MAIN_PLANE,
                               0,
                               0,
                               0,
                               0};

  HDC dc = mWindowDC;
  if (!dc)
    dc = GetDC(mPlugWnd);

  int fmt = ChoosePixelFormat(dc, &pfd);
  SetPixelFormat(dc, fmt, &pfd);
  mHGLRC = wglCreateContext(dc);
  wglMakeCurrent(dc, mHGLRC);

  #ifdef IGRAPHICS_GL3
  // On windows we can't create a 3.3 context directly, since we need the wglCreateContextAttribsARB extension.
  // We load the extension, then re-create the context.
  auto wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

  if (wglCreateContextAttribsARB)
  {
    wglDeleteContext(mHGLRC);

    const int attribList[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};

    mHGLRC = wglCreateContextAttribsARB(dc, 0, attribList);
    wglMakeCurrent(dc, mHGLRC);
  }

  #endif

  // TODO: return false if GL init fails?
  if (!gladLoadGL())
    DBGMSG("Error initializing glad");

  glGetError();

#if defined IGRAPHICS_GL
  if (PFNWGLSWAPINTERVALEXTPROC swapInterval = (PFNWGLSWAPINTERVALEXTPROC) wglGetProcAddress("wglSwapIntervalEXT"))
  {
    const intptr_t sentinel = reinterpret_cast<intptr_t>(swapInterval);
    if (sentinel > 3 || sentinel < -3)
      swapInterval(0);
  }
#endif

  if (!mWindowDC)
    ReleaseDC(mPlugWnd, dc);
}

void IGraphicsWin::DestroyGLContext()
{
  wglMakeCurrent(NULL, NULL);
  wglDeleteContext(mHGLRC);
}
#endif

#ifdef IGRAPHICS_VULKAN
bool IGraphicsWin::CreateVulkanContext()
{
  if (mVkInstance)
    return true;

  WinVulkanPreferredAdapter preferredAdapter = WinVulkanPreferredAdapter::kAny;
  if (const char* pref = std::getenv("IGRAPHICS_VK_GPU"))
  {
    if (std::strcmp(pref, "integrated") == 0)
      preferredAdapter = WinVulkanPreferredAdapter::kIntegrated;
    else if (std::strcmp(pref, "discrete") == 0)
      preferredAdapter = WinVulkanPreferredAdapter::kDiscrete;
  }

  WinVulkanDeviceRequest request{};
  request.instanceHandle = mHInstance;
  request.windowHandle = mPlugWnd;
  request.preferredAdapter = preferredAdapter;
#if !defined(NDEBUG)
  request.enableValidationLayer = true;
#else
  request.enableValidationLayer = false;
#endif

  WinVulkanDeviceSnapshot snapshot{};
  uint64_t generation = 0;
  VkResult res = mVulkanDeviceCoordinator.Initialize(request, snapshot, generation);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateVulkanContext",
                        "deviceCoordinator",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    mVulkanDeviceCoordinator.Teardown(generation);
    return false;
  }

  mVkInstance = snapshot.instance;
  mVkPhysicalDevice = snapshot.physicalDevice;
  mVkDevice = snapshot.device;
  mVkSurface = snapshot.surface;
  mPresentQueue = snapshot.presentQueue;
  mVkQueueFamily = snapshot.queueFamily;
  mVulkanDeviceGeneration = generation;

  VkSurfaceCapabilitiesKHR caps{};
  res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mVkPhysicalDevice, mVkSurface, &caps);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateVulkanContext",
                        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    DestroyVulkanContext();
    return false;
  }

  mVkSwapchain.device = mVkDevice;
  bool submissionPending = false;
  res = CreateOrResizeVulkanSwapchain(caps.currentExtent.width, caps.currentExtent.height, mVkSwapchain.handle, mVkSwapchainImages, mVkFormat, mVkSwapchainUsageFlags, submissionPending);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateVulkanContext",
                        "createOrResizeSwapchain",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    DestroyVulkanContext();
    return false;
  }

  VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  mImageAvailableSemaphore.device = mVkDevice;
  res = vkCreateSemaphore(mVkDevice, &semInfo, nullptr, &mImageAvailableSemaphore.handle);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateVulkanContext",
                        "vkCreateSemaphore",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)),
                         vulkanlog::MakeField("semaphore", "imageAvailable"));
    DestroyVulkanContext();
    return false;
  }
  mRenderFinishedSemaphore.device = mVkDevice;
  res = vkCreateSemaphore(mVkDevice, &semInfo, nullptr, &mRenderFinishedSemaphore.handle);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateVulkanContext",
                        "vkCreateSemaphore",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)),
                         vulkanlog::MakeField("semaphore", "renderFinished"));
    DestroyVulkanContext();
    return false;
  }

  VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  mInFlightFence.device = mVkDevice;
  res = vkCreateFence(mVkDevice, &fenceInfo, nullptr, &mInFlightFence.handle);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateVulkanContext",
                        "vkCreateFence",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    DestroyVulkanContext();
    return false;
  }

  return true;
}

void IGraphicsWin::DestroyVulkanContext()
{
  if (mVkDevice)
    vkDeviceWaitIdle(mVkDevice);

  mImageAvailableSemaphore.Reset();
  mRenderFinishedSemaphore.Reset();
  mInFlightFence.Reset();
  mVkSwapchain.Reset();
  mVkSwapchain.device = mVkDevice;
  mVkSwapchainImages.clear();
  mVkFormat = VK_FORMAT_B8G8R8A8_UNORM;
  mVkSwapchainUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  if (mVulkanDeviceGeneration != 0)
  {
    mVulkanDeviceCoordinator.Teardown(mVulkanDeviceGeneration);
  }
  else if (mVkInstance || mVkDevice)
  {
    mVulkanDeviceCoordinator.Teardown();
  }

  mVkInstance = VK_NULL_HANDLE;
  mVkPhysicalDevice = VK_NULL_HANDLE;
  mVkDevice = VK_NULL_HANDLE;
  mVkSurface = VK_NULL_HANDLE;
  mPresentQueue = VK_NULL_HANDLE;
  mVkQueueFamily = 0;
  mVkSwapchain.device = VK_NULL_HANDLE;
  mVulkanDeviceGeneration = 0;
}

bool IGraphicsWin::RecreateVulkanContext()
{
  OnViewDestroyed();
  SkipVKFrame();
  DestroyVulkanContext();
  if (!CreateVulkanContext())
    return false;
  VulkanContext ctx;
  ctx.instance = mVkInstance;
  ctx.physicalDevice = mVkPhysicalDevice;
  ctx.device = mVkDevice;
  ctx.surface = mVkSurface;
  ctx.swapchain = mVkSwapchain.handle;
  ctx.queue = mPresentQueue;
  ctx.queueFamily = mVkQueueFamily;
  ctx.imageAvailableSemaphore = mImageAvailableSemaphore.handle;
  ctx.renderFinishedSemaphore = mRenderFinishedSemaphore.handle;
  ctx.inFlightFence = mInFlightFence.handle;
  ctx.swapchainImages = &mVkSwapchainImages;
  ctx.format = mVkFormat;
  ctx.usageFlags = mVkSwapchainUsageFlags;
  OnViewInitialized(&ctx);
  return true;
}

VkResult IGraphicsWin::CreateOrResizeVulkanSwapchain(
  uint32_t width, uint32_t height, VkSwapchainKHR& swapchain, std::vector<VkImage>& images, VkFormat& format, VkImageUsageFlags& usage, bool& submissionPending)
{
  IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                      "request",
                      vulkanlog::Severity::kInfo,
                      vulkanlog::MakeField("width", static_cast<uint32_t>(width)),
                       vulkanlog::MakeField("height", static_cast<uint32_t>(height)),
                       vulkanlog::MakeHandleField("previousSwapchain", vulkanlog::HandleToUint64(reinterpret_cast<uintptr_t>(mVkSwapchain.handle))));
  if (!mVkDevice || !mVkPhysicalDevice || !mVkSurface)
    return VK_ERROR_INITIALIZATION_FAILED;

  VkResult res = VK_SUCCESS;
  if (mInFlightFence.handle)
  {
    vkQueueWaitIdle(mPresentQueue);
    res = vkResetFences(mVkDevice, 1, &mInFlightFence.handle);
    if (res != VK_SUCCESS)
    {
      IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                          "vkResetFences",
                          vulkanlog::Severity::kError,
                          vulkanlog::MakeField("vkResult", static_cast<int>(res)));
      return res;
    }
    // ensure next BeginFrame sees the fence as signaled
    res = vkQueueSubmit(mPresentQueue, 0, nullptr, mInFlightFence.handle);
    if (res != VK_SUCCESS)
    {
      IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                          "vkQueueSubmit",
                          vulkanlog::Severity::kError,
                          vulkanlog::MakeField("vkResult", static_cast<int>(res)));
      return res;
    }
    submissionPending = false;
    IGRAPHICS_VK_LOG_SIMPLE("CreateOrResizeVulkanSwapchain",
                        "resetInFlightFence",
                        vulkanlog::Severity::kDebug);
  }

  mVkSwapchain.Reset();
  mVkSwapchain.device = mVkDevice;
  IGRAPHICS_VK_LOG_SIMPLE("CreateOrResizeVulkanSwapchain",
                      "clearedPreviousSwapchain",
                      vulkanlog::Severity::kDebug);

  VkSurfaceCapabilitiesKHR caps{};
  res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mVkPhysicalDevice, mVkSurface, &caps);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    return res;
  }
  IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                      "surfaceCapabilities",
                      vulkanlog::Severity::kDebug,
                      vulkanlog::MakeField("currentWidth", caps.currentExtent.width),
                       vulkanlog::MakeField("currentHeight", caps.currentExtent.height),
                       vulkanlog::MakeField("minWidth", caps.minImageExtent.width),
                       vulkanlog::MakeField("minHeight", caps.minImageExtent.height),
                       vulkanlog::MakeField("maxWidth", caps.maxImageExtent.width),
                       vulkanlog::MakeField("maxHeight", caps.maxImageExtent.height),
                       vulkanlog::MakeField("usage", static_cast<uint32_t>(caps.supportedUsageFlags)));

  uint32_t formatCount = 0;
  res = vkGetPhysicalDeviceSurfaceFormatsKHR(mVkPhysicalDevice, mVkSurface, &formatCount, nullptr);
  if (res != VK_SUCCESS || formatCount == 0)
  {
    IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                        "vkGetPhysicalDeviceSurfaceFormatsKHR",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)),
                         vulkanlog::MakeField("count", static_cast<uint32_t>(formatCount)));
    return res;
  }
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  res = vkGetPhysicalDeviceSurfaceFormatsKHR(mVkPhysicalDevice, mVkSurface, &formatCount, formats.data());
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                        "fetchSurfaceFormats",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    return res;
  }
  VkSurfaceFormatKHR surfaceFormat = formats[0];
  for (auto& f : formats)
  {
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    {
      surfaceFormat = f;
      break;
    }
  }
  IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                      "selectSurfaceFormat",
                      vulkanlog::Severity::kDebug,
                      vulkanlog::MakeField("format", static_cast<int>(surfaceFormat.format)),
                       vulkanlog::MakeField("colorSpace", static_cast<int>(surfaceFormat.colorSpace)),
                       vulkanlog::MakeField("options", static_cast<uint32_t>(formatCount)));

  uint32_t presentCount = 0;
  res = vkGetPhysicalDeviceSurfacePresentModesKHR(mVkPhysicalDevice, mVkSurface, &presentCount, nullptr);
  if (res != VK_SUCCESS || presentCount == 0)
  {
    IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                        "vkGetPhysicalDeviceSurfacePresentModesKHR",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)),
                         vulkanlog::MakeField("count", static_cast<uint32_t>(presentCount)));
    return res;
  }
  std::vector<VkPresentModeKHR> presentModes(presentCount);
  res = vkGetPhysicalDeviceSurfacePresentModesKHR(mVkPhysicalDevice, mVkSurface, &presentCount, presentModes.data());
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                        "fetchPresentModes",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    return res;
  }
  VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
  for (auto pm : presentModes)
  {
    if (pm == VK_PRESENT_MODE_FIFO_KHR)
    {
      presentMode = pm;
      break;
    }
  }
  IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                      "selectPresentMode",
                      vulkanlog::Severity::kDebug,
                      vulkanlog::MakeField("presentMode", static_cast<int>(presentMode)),
                       vulkanlog::MakeField("options", static_cast<uint32_t>(presentCount)));

  VkSwapchainCreateInfoKHR swapInfo{};
  swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapInfo.surface = mVkSurface;
  swapInfo.minImageCount = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && swapInfo.minImageCount > caps.maxImageCount)
    swapInfo.minImageCount = caps.maxImageCount;
  swapInfo.imageFormat = surfaceFormat.format;
  swapInfo.imageColorSpace = surfaceFormat.colorSpace;
  uint32_t swapWidth = width;
  uint32_t swapHeight = height;
  if (caps.currentExtent.width != UINT32_MAX)
  {
    swapWidth = caps.currentExtent.width;
    swapHeight = caps.currentExtent.height;
  }
  else
  {
    swapWidth = std::max(caps.minImageExtent.width, std::min(width, caps.maxImageExtent.width));
    swapHeight = std::max(caps.minImageExtent.height, std::min(height, caps.maxImageExtent.height));
  }
  swapInfo.imageExtent.width = swapWidth;
  swapInfo.imageExtent.height = swapHeight;
  swapInfo.imageArrayLayers = 1;
  VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
    usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  swapInfo.imageUsage = usageFlags;
  swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapInfo.preTransform = caps.currentTransform;
  swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapInfo.presentMode = presentMode;
  swapInfo.clipped = VK_TRUE;
  IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                      "createInfo",
                      vulkanlog::Severity::kDebug,
                      vulkanlog::MakeField("width", static_cast<uint32_t>(swapInfo.imageExtent.width)),
                       vulkanlog::MakeField("height", static_cast<uint32_t>(swapInfo.imageExtent.height)),
                       vulkanlog::MakeField("minImageCount", static_cast<uint32_t>(swapInfo.minImageCount)),
                       vulkanlog::MakeField("usage", static_cast<uint32_t>(swapInfo.imageUsage)),
                       vulkanlog::MakeHandleField("oldSwapchain", vulkanlog::HandleToUint64(reinterpret_cast<uintptr_t>(mVkSwapchain.handle))));

  res = vkCreateSwapchainKHR(mVkDevice, &swapInfo, nullptr, &mVkSwapchain.handle);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                        "vkCreateSwapchainKHR",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    return res;
  }

  uint32_t imageCount = 0;
  res = vkGetSwapchainImagesKHR(mVkDevice, mVkSwapchain.handle, &imageCount, nullptr);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                        "vkGetSwapchainImagesKHR_count",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    return res;
  }
  mVkSwapchainImages.resize(imageCount);
  res = vkGetSwapchainImagesKHR(mVkDevice, mVkSwapchain.handle, &imageCount, mVkSwapchainImages.data());
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                        "vkGetSwapchainImagesKHR_data",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    return res;
  }
  IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                      "retrievedImages",
                      vulkanlog::Severity::kDebug,
                      vulkanlog::MakeField("count", static_cast<uint32_t>(imageCount)));

  IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                      "creationResult",
                      vulkanlog::Severity::kDebug,
                      vulkanlog::MakeHandleField("swapchain", vulkanlog::HandleToUint64(reinterpret_cast<uintptr_t>(mVkSwapchain.handle))),
                       vulkanlog::MakeField("width", static_cast<uint32_t>(swapInfo.imageExtent.width)),
                       vulkanlog::MakeField("height", static_cast<uint32_t>(swapInfo.imageExtent.height)),
                       vulkanlog::MakeField("format", static_cast<int>(surfaceFormat.format)),
                       vulkanlog::MakeField("usage", static_cast<uint32_t>(usageFlags)),
                       vulkanlog::MakeField("images", static_cast<uint32_t>(imageCount)));
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    IGRAPHICS_VK_LOG("CreateOrResizeVulkanSwapchain",
                        "swapchainImage",
                        vulkanlog::Severity::kDebug,
                        vulkanlog::MakeField("index", static_cast<uint32_t>(i)),
                         vulkanlog::MakeHandleField("image", vulkanlog::HandleToUint64(mVkSwapchainImages[i])));
  }

  mVkFormat = surfaceFormat.format;
  format = mVkFormat;
  usage = usageFlags;
  mVkSwapchainUsageFlags = usageFlags;
  swapchain = mVkSwapchain.handle;
  images = mVkSwapchainImages;
  return VK_SUCCESS;
}

void IGraphicsWin::ActivateVulkanContext()
{
  // Vulkan does not require explicit context activation
}

void IGraphicsWin::DeactivateVulkanContext()
{
  // Vulkan does not require explicit context deactivation. Avoid
  // blocking on the present queue here to prevent unnecessary stalls;
  // required synchronization is handled via semaphores/fences.
}
#endif

void IGraphicsWin::ActivateGLContext()
{
#if defined IGRAPHICS_GL
  mStartHDC = wglGetCurrentDC();
  mStartHGLRC = wglGetCurrentContext();
  if (mWindowDC && mHGLRC)
  {
    if (mStartHDC != mWindowDC || mStartHGLRC != mHGLRC)
      wglMakeCurrent(mWindowDC, mHGLRC);
  }
#elif defined IGRAPHICS_VULKAN
  ActivateVulkanContext();
#endif
}

void IGraphicsWin::DeactivateGLContext()
{
#if defined IGRAPHICS_GL
  if (mStartHDC != mWindowDC || mStartHGLRC != mHGLRC)
    wglMakeCurrent(mStartHDC, mStartHGLRC); // return current ctxt to start
#elif defined IGRAPHICS_VULKAN
  DeactivateVulkanContext();
#endif
}

EMsgBoxResult IGraphicsWin::ShowMessageBox(const char* str, const char* title, EMsgBoxType type, IMsgBoxCompletionHandlerFunc completionHandler)
{
  ReleaseMouseCapture();

  EMsgBoxResult result = static_cast<EMsgBoxResult>(MessageBoxW(GetMainWnd(), UTF8AsUTF16(str).Get(), UTF8AsUTF16(title).Get(), static_cast<int>(type)));

  if (completionHandler)
    completionHandler(result);

  return result;
}

float IGraphicsWin::MeasureWindowScale() const
{
  const HWND target = mPlugWnd ? mPlugWnd : mParentWnd;
  if (!target)
    return 1.f;

  const float measured = GetScaleForHWND(target);
  if (!std::isfinite(measured) || measured <= 0.f)
    return 1.f;

  return measured;
}

float IGraphicsWin::MeasureWindowDPIScale() const
{
  const HWND target = mPlugWnd ? mPlugWnd : mParentWnd;
  const float dpiScale = ComputeWindowDpiScale(target);

  if (!std::isfinite(dpiScale) || dpiScale <= 0.f)
    return 1.f;

  return dpiScale;
}

void IGraphicsWin::SetHostContentScaleBypassed(bool bypass)
{
  if (mBypassHostContentScale == bypass)
    return;

  mBypassHostContentScale = bypass;

  if (WindowIsOpen())
    RefreshPlatformScale(true);
}

float IGraphicsWin::GetBackingPixelScaleForParentResize() const
{
  if (!mBypassHostContentScale)
    return GetScreenScale();

  if (mWindowDPIScale <= 0.f || !std::isfinite(mWindowDPIScale))
    return GetScreenScale();

  return mWindowDPIScale;
}

void IGraphicsWin::RefreshPlatformScale(bool force)
{
  const float measured = MeasureWindowScale();
  const float dpiScale = MeasureWindowDPIScale();

  if (dpiScale > 0.f && std::isfinite(dpiScale))
    mWindowDPIScale = dpiScale;
  else
    mWindowDPIScale = 1.f;

  const float current = GetScreenScale();

  if (!force)
  {
    if (std::fabs(measured - current) < 0.001f)
      return;
  }

  DBGMSG("IGraphicsWin: RefreshPlatformScale measured=%.3f host=%.3f bypass=%s force=%s\n",
         measured,
         mWindowDPIScale,
         mBypassHostContentScale ? "true" : "false",
         force ? "true" : "false");

#if defined IGRAPHICS_VULKAN
  IGRAPHICS_VK_LOG("RefreshPlatformScale",
                   "dpiUpdate",
                   vulkanlog::Severity::kInfo,
                   vulkanlog::MakeField("measured", std::to_string(measured)),
                   vulkanlog::MakeField("host", std::to_string(mWindowDPIScale)),
                   vulkanlog::MakeField("bypass", mBypassHostContentScale),
                   vulkanlog::MakeField("force", force));
#endif

  if (measured > 0.f && std::isfinite(measured))
    SetScreenScale(measured);
}

void* IGraphicsWin::OpenWindow(void* pParent)
{
  mParentWnd = (HWND)pParent;
  const float physicalScale = GetScaleForHWND(mParentWnd);
  const float hostScale = ComputeWindowDpiScale(mParentWnd);
  float windowScale = physicalScale;

  const bool hostScaleValid = hostScale > 0.f && std::isfinite(hostScale);
  const bool physicalScaleValid = physicalScale > 0.f && std::isfinite(physicalScale);

  if (mBypassHostContentScale && hostScaleValid)
    windowScale = hostScale;
  else if (!physicalScaleValid && hostScaleValid)
    windowScale = hostScale;

  if (hostScaleValid)
    mWindowDPIScale = hostScale;
  else if (!std::isfinite(mWindowDPIScale) || mWindowDPIScale <= 0.f)
    mWindowDPIScale = 1.f;

  if (!std::isfinite(windowScale) || windowScale <= 0.f)
    windowScale = 1.f;

  const int scaledWidth = static_cast<int>(std::round(static_cast<float>(WindowWidth()) * windowScale));
  const int scaledHeight = static_cast<int>(std::round(static_cast<float>(WindowHeight()) * windowScale));
  int x = 0;
  int y = 0;
  int w = scaledWidth;
  int h = scaledHeight;

  if (mPlugWnd)
  {
    RECT pR, cR;
    GetWindowRect((HWND)pParent, &pR);
    GetWindowRect(mPlugWnd, &cR);
    CloseWindow();
    x = cR.left - pR.left;
    y = cR.top - pR.top;
    w = cR.right - cR.left;
    h = cR.bottom - cR.top;
  }

  if (nWndClassReg++ == 0)
  {
    WNDCLASSW wndClass = {CS_DBLCLKS | CS_OWNDC, WndProc, 0, 0, mHInstance, 0, 0, 0, 0, wndClassName};
    RegisterClassW(&wndClass);
  }

  mPlugWnd = CreateWindowW(wndClassName, L"IPlug", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, x, y, w, h, mParentWnd, 0, mHInstance, this);
#if defined IGRAPHICS_VULKAN
  SetPlatformContext(mPlugWnd);
  if (!CreateVulkanContext())
  {
    DestroyWindow(mPlugWnd);
    mPlugWnd = nullptr;
    return nullptr;
  }
  VulkanContext ctx;
  ctx.instance = mVkInstance;
  ctx.physicalDevice = mVkPhysicalDevice;
  ctx.device = mVkDevice;
  ctx.surface = mVkSurface;
  ctx.swapchain = mVkSwapchain.handle;
  ctx.queue = mPresentQueue;
  ctx.queueFamily = mVkQueueFamily;
  ctx.imageAvailableSemaphore = mImageAvailableSemaphore.handle;
  ctx.renderFinishedSemaphore = mRenderFinishedSemaphore.handle;
  ctx.inFlightFence = mInFlightFence.handle;
  ctx.swapchainImages = &mVkSwapchainImages;
  ctx.format = mVkFormat;
  ctx.usageFlags = mVkSwapchainUsageFlags;
  OnViewInitialized(&ctx);
#else
  HDC dc = GetDC(mPlugWnd);
  #ifdef IGRAPHICS_GL
  mWindowDC = dc;
  SetPlatformContext(mWindowDC);
  CreateGLContext();
  OnViewInitialized((void*)mWindowDC);
  #else
  SetPlatformContext(dc);
  ReleaseDC(mPlugWnd, dc);
  OnViewInitialized((void*)dc);
  #endif
#endif

  RefreshPlatformScale(true); // resizes draw context using measured physical DPI

  GetDelegate()->LayoutUI(this);

  if (MultiTouchEnabled() && GetSystemMetrics(SM_DIGITIZER) & NID_MULTI_INPUT)
  {
    RegisterTouchWindow(mPlugWnd, 0);
  }

  if (!mPlugWnd && --nWndClassReg == 0)
  {
    UnregisterClassW(wndClassName, mHInstance);
  }
  else
  {
    SetAllControlsDirty();
  }

  if (mPlugWnd && TooltipsEnabled())
  {
    static const INITCOMMONCONTROLSEX iccex = {sizeof(INITCOMMONCONTROLSEX), ICC_TAB_CLASSES};

    if (InitCommonControlsEx(&iccex))
    {
      mTooltipWnd = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL, WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                    mPlugWnd, NULL, mHInstance, NULL);
      if (mTooltipWnd)
      {
        SetWindowPos(mTooltipWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        TOOLINFOW ti = {TTTOOLINFOW_V2_SIZE, TTF_IDISHWND | TTF_SUBCLASS, mPlugWnd, (UINT_PTR)mPlugWnd, {0, 0, 0, 0}, NULL, NULL, 0, NULL};
        SendMessageW(mTooltipWnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(mTooltipWnd, TTM_SETMAXTIPWIDTH, 0, TOOLTIPWND_MAXWIDTH);
      }
    }

    if (!mTooltipWnd)
      EnableTooltips(false);

#ifdef IGRAPHICS_GL
    wglMakeCurrent(NULL, NULL);
#endif
  }


  // Initialize OLE and register as a drop target for modern drag & drop (e.g., FL Studio)
  if (!mOLEInited)
  {
    HRESULT hrInit = OleInitialize(nullptr);
    if (SUCCEEDED(hrInit))
      mOLEInited = true;
  }
  if (!mDropTarget && mPlugWnd)
  {
    mDropTarget = new DragAndDropHelpers::DropTarget(mPlugWnd, this);
    HRESULT hrReg = RegisterDragDrop(mPlugWnd, mDropTarget);
    if (FAILED(hrReg))
    {
      mDropTarget->Release();
      mDropTarget = nullptr;
    }
  }

  GetDelegate()->OnUIOpen();

  return mPlugWnd;
}

static void GetWndClassName(HWND hWnd, WDL_String* pStr)
{
  wchar_t cStrW[MAX_CLASSNAME_LEN] = {'\0'};
  GetClassNameW(hWnd, cStrW, MAX_CLASSNAME_LEN);
  pStr->Set(UTF16AsUTF8(cStrW).Get());
}

BOOL CALLBACK IGraphicsWin::FindMainWindow(HWND hWnd, LPARAM lParam)
{
  IGraphicsWin* pGraphics = (IGraphicsWin*)lParam;
  if (pGraphics)
  {
    DWORD wPID;
    GetWindowThreadProcessId(hWnd, &wPID);
    WDL_String str;
    GetWndClassName(hWnd, &str);
    if (wPID == pGraphics->mPID && !strcmp(str.Get(), pGraphics->mMainWndClassName.Get()))
    {
      pGraphics->mMainWnd = hWnd;
      return FALSE; // Stop enumerating.
    }
  }
  return TRUE;
}

HWND IGraphicsWin::GetMainWnd()
{
  if (!mMainWnd)
  {
    if (mParentWnd)
    {
      HWND parentWnd = mParentWnd;
      while (parentWnd)
      {
        mMainWnd = parentWnd;
        parentWnd = GetParent(mMainWnd);
      }

      GetWndClassName(mMainWnd, &mMainWndClassName);
    }
    else if (CStringHasContents(mMainWndClassName.Get()))
    {
      mPID = GetCurrentProcessId();
      EnumWindows(FindMainWindow, (LPARAM)this);
    }
  }
  return mMainWnd;
}

IRECT IGraphicsWin::GetWindowRECT()
{
  if (mPlugWnd)
  {
    RECT r;
    GetWindowRect(mPlugWnd, &r);
    r.right -= TOOLWIN_BORDER_W;
    r.bottom -= TOOLWIN_BORDER_H;
    return IRECT(r.left, r.top, r.right, r.bottom);
  }
  return IRECT();
}

void IGraphicsWin::CloseWindow()
{
  if (mPlugWnd)
  {
    if (ControlIsCaptured() || GetCapture() == mPlugWnd)
      ReleaseMouseCapture();

    if (mVSYNCEnabled)
      StopVBlankThread();
    else
      KillTimer(mPlugWnd, IPLUG_TIMER_ID);

#if defined IGRAPHICS_GL
    HDC currentDC = wglGetCurrentDC();
    HGLRC currentContext = wglGetCurrentContext();

    if (currentContext != mHGLRC)
    {
      ActivateGLContext();
    }
#elif defined IGRAPHICS_VULKAN
    ActivateGLContext();
#endif

    OnViewDestroyed();

#if defined IGRAPHICS_GL

    DeactivateGLContext();

    DestroyGLContext();

    if (mWindowDC)
    {
      ReleaseDC(mPlugWnd, mWindowDC);
      mWindowDC = nullptr;
    }

#elif defined IGRAPHICS_VULKAN

    DeactivateGLContext();

    DestroyVulkanContext();

#endif

    SetPlatformContext(nullptr);

    if (mTooltipWnd)
    {
      DestroyWindow(mTooltipWnd);
      mTooltipWnd = 0;
      mShowingTooltip = false;
      mTooltipIdx = -1;
    }


    // Unregister OLE drop target and uninitialize OLE if needed


    if (mDropTarget)


    {


      RevokeDragDrop(mPlugWnd);


      mDropTarget->Release();


      mDropTarget = nullptr;
    }


    if (mOLEInited)


    {


      OleUninitialize();


      mOLEInited = false;
    }


    DestroyWindow(mPlugWnd);


    mPlugWnd = 0;

    if (--nWndClassReg == 0)
    {
      UnregisterClassW(wndClassName, mHInstance);
    }
  }
}


void IGraphicsWin::OnOLEDropFiles(const std::vector<std::wstring>& filesW, LONG xScreen, LONG yScreen)
{
  // Convert screen -> client coords and scale to IGraphics space
  POINT p{(LONG)xScreen, (LONG)yScreen};
  ScreenToClient(mPlugWnd, &p);
  const float scale = GetTotalScale();

  // Convert wide strings to UTF-8 and build buffers/pointers like WM_DROPFILES path
  const int count = static_cast<int>(filesW.size());
  std::vector<std::vector<char>> pathBuffers(count, std::vector<char>(2048, 0));
  std::vector<const char*> pathPtrs(count);

  for (int i = 0; i < count; ++i)
  {
    UTF16AsUTF8 utf8(filesW[i].c_str());
    strncpy(pathBuffers[i].data(), utf8.Get(), pathBuffers[i].size() - 1);
    pathPtrs[i] = pathBuffers[i].data();
  }

  if (count == 1)
    OnDrop(pathPtrs[0], p.x / scale, p.y / scale);
  else
    OnDropMultiple(pathPtrs, p.x / scale, p.y / scale);
}

bool IGraphicsWin::PlatformSupportsMultiTouch() const { return GetSystemMetrics(SM_DIGITIZER) & NID_MULTI_INPUT; }

IPopupMenu* IGraphicsWin::GetItemMenu(long idx, long& idxInMenu, long& offsetIdx, IPopupMenu& baseMenu)
{
  long oldIDx = offsetIdx;
  offsetIdx += baseMenu.NItems();

  if (idx < offsetIdx)
  {
    idxInMenu = idx - oldIDx;
    return &baseMenu;
  }

  IPopupMenu* pMenu = nullptr;

  for (int i = 0; i < baseMenu.NItems(); i++)
  {
    IPopupMenu::Item* pMenuItem = baseMenu.GetItem(i);
    if (pMenuItem->GetSubmenu())
    {
      pMenu = GetItemMenu(idx, idxInMenu, offsetIdx, *pMenuItem->GetSubmenu());

      if (pMenu)
        break;
    }
  }

  return pMenu;
}

HMENU IGraphicsWin::CreateMenu(IPopupMenu& menu, long* pOffsetIdx)
{
  HMENU hMenu = ::CreatePopupMenu();

  WDL_String escapedText;

  int flags = 0;
  long offset = *pOffsetIdx;
  long nItems = menu.NItems();
  *pOffsetIdx += nItems;
  long inc = 0;

  for (int i = 0; i < nItems; i++)
  {
    IPopupMenu::Item* pMenuItem = menu.GetItem(i);

    if (pMenuItem->GetIsSeparator())
    {
      AppendMenuW(hMenu, MF_SEPARATOR, 0, 0);
    }
    else
    {
      const char* str = pMenuItem->GetText();
      int maxlen = strlen(str) + menu.GetPrefix() ? 50 : 0;
      WDL_String entryText(str);

      if (menu.GetPrefix())
      {
        switch (menu.GetPrefix())
        {
        case 1: {
          entryText.SetFormatted(maxlen, "%1d: %s", i + 1, str);
          break;
        }
        case 2: {
          entryText.SetFormatted(maxlen, "%02d: %s", i + 1, str);
          break;
        }
        case 3: {
          entryText.SetFormatted(maxlen, "%03d: %s", i + 1, str);
          break;
        }
        }
      }

      // Escape ampersands if present

      if (strchr(entryText.Get(), '&'))
      {
        for (int c = 0; c < entryText.GetLength(); c++)
          if (entryText.Get()[c] == '&')
            entryText.Insert("&", c++);
      }

      flags = MF_STRING;
      if (nItems < 160 && menu.NItemsPerColumn() > 0 && inc && !(inc % menu.NItemsPerColumn()))
        flags |= MF_MENUBARBREAK;

      if (pMenuItem->GetEnabled())
        flags |= MF_ENABLED;
      else
        flags |= MF_GRAYED;
      if (pMenuItem->GetIsTitle())
        flags |= MF_DISABLED;
      if (pMenuItem->GetChecked())
        flags |= MF_CHECKED;
      else
        flags |= MF_UNCHECKED;

      if (pMenuItem->GetSubmenu())
      {
        HMENU submenu = CreateMenu(*pMenuItem->GetSubmenu(), pOffsetIdx);
        if (submenu)
        {
          AppendMenuW(hMenu, flags | MF_POPUP, (UINT_PTR)submenu, UTF8AsUTF16(entryText).Get());
        }
      }
      else
      {
        AppendMenuW(hMenu, flags, offset + inc, UTF8AsUTF16(entryText).Get());
      }
    }
    inc++;
  }

  return hMenu;
}

IPopupMenu* IGraphicsWin::CreatePlatformPopupMenu(IPopupMenu& menu, const IRECT bounds, bool& isAsync)
{
  long offsetIdx = 0;
  HMENU hMenu = CreateMenu(menu, &offsetIdx);

  if (hMenu)
  {
    IPopupMenu* result = nullptr;

    POINT cPos;
    const float scale = GetTotalScale();

    cPos.x = bounds.L * scale;
    cPos.y = bounds.B * scale;

    ::ClientToScreen(mPlugWnd, &cPos);

    if (TrackPopupMenu(hMenu, TPM_LEFTALIGN, cPos.x, cPos.y, 0, mPlugWnd, 0))
    {
      MSG msg;
      if (PeekMessage(&msg, mPlugWnd, WM_COMMAND, WM_COMMAND, PM_REMOVE))
      {
        if (HIWORD(msg.wParam) == 0)
        {
          long res = LOWORD(msg.wParam);
          if (res != -1)
          {
            long idx = 0;
            offsetIdx = 0;
            IPopupMenu* pReturnMenu = GetItemMenu(res, idx, offsetIdx, menu);
            if (pReturnMenu)
            {
              result = pReturnMenu;
              result->SetChosenItemIdx(idx);

              // synchronous
              if (pReturnMenu && pReturnMenu->GetFunction())
                pReturnMenu->ExecFunction();
            }
          }
        }
      }
    }
    DestroyMenu(hMenu);

    RECT r = {0, 0, static_cast<LONG>(WindowWidth() * GetScreenScale()), static_cast<LONG>(WindowHeight() * GetScreenScale())};
    InvalidateRect(mPlugWnd, &r, FALSE);

    return result;
  }

  return nullptr;
}

void IGraphicsWin::CreatePlatformTextEntry(int paramIdx, const IText& text, const IRECT& bounds, int length, const char* str)
{
  if (mParamEditWnd)
    return;

  DWORD editStyle;

  switch (text.mAlign)
  {
  case EAlign::Near:
    editStyle = ES_LEFT;
    break;
  case EAlign::Far:
    editStyle = ES_RIGHT;
    break;
  case EAlign::Center:
  default:
    editStyle = ES_CENTER;
    break;
  }

  const float scale = GetTotalScale();
  IRECT scaledBounds = bounds.GetScaled(scale);

  mParamEditWnd = CreateWindowW(L"EDIT", UTF8AsUTF16(str).Get(), ES_AUTOHSCROLL | WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE | editStyle, scaledBounds.L, scaledBounds.T,
                                scaledBounds.W() + 1, scaledBounds.H() + 1, mPlugWnd, (HMENU)PARAM_EDIT_ID, mHInstance, 0);

  if (!mParamEditWnd)
  {
    return;
  }

  StaticStorage<HFontHolder>::Accessor hfontStorage(sHFontCache);

  LOGFONTW lFont = {0};
  HFontHolder* hfontHolder = hfontStorage.Find(text.mFont);

  assert(hfontHolder && "font not found - did you forget to load it?");
  if (!hfontHolder)
  {
    DestroyWindow(mParamEditWnd);
    mParamEditWnd = nullptr;
    return;
  }

  GetObjectW(hfontHolder->mHFont, sizeof(LOGFONTW), &lFont);
  lFont.lfHeight = text.mSize * scale;
  mEditFont = CreateFontIndirectW(&lFont);

  mEditParam = paramIdx > kNoParameter ? GetDelegate()->GetParam(paramIdx) : nullptr;
  mEditText = text;
  mEditRECT = bounds;

  SendMessageW(mParamEditWnd, EM_LIMITTEXT, (WPARAM)length, 0);
  SendMessageW(mParamEditWnd, WM_SETFONT, (WPARAM)mEditFont, 0);
  SendMessageW(mParamEditWnd, EM_SETSEL, 0, -1);

  if (text.mVAlign == EVAlign::Middle)
  {
    double textHeightToCenter = text.mSize * scale;
    double controlHeight = scaledBounds.H();
    double offset = (controlHeight - textHeightToCenter) / 2.0;

    if (offset < 0.0)
      offset = 0.0;

    RECT marginsRect{0, (LONG)offset, 0, 0};
    SendMessageW(mParamEditWnd, EM_SETRECT, 0, (LPARAM)&marginsRect);
  }

  SetFocus(mParamEditWnd);

  mDefEditProc = (WNDPROC)SetWindowLongPtrW(mParamEditWnd, GWLP_WNDPROC, (LONG_PTR)ParamEditProc);
  SetWindowLongPtrW(mParamEditWnd, GWLP_USERDATA, 0xdeadf00b);
}

bool IGraphicsWin::RevealPathInExplorerOrFinder(WDL_String& path, bool select)
{
  bool success = false;

  if (path.GetLength())
  {
    WCHAR winDir[IPLUG_WIN_MAX_WIDE_PATH];
    UINT len = GetSystemDirectoryW(winDir, IPLUG_WIN_MAX_WIDE_PATH);

    if (len && !(len > MAX_PATH - 2))
    {
      winDir[len] = L'\\';
      winDir[++len] = L'\0';

      WDL_String explorerParams;

      if (select)
        explorerParams.Append("/select,");

      explorerParams.Append("\"");
      explorerParams.Append(path.Get());
      explorerParams.Append("\\\"");

      HINSTANCE result;

      if ((result = ::ShellExecuteW(NULL, L"open", L"explorer.exe", UTF8AsUTF16(explorerParams).Get(), winDir, SW_SHOWNORMAL)) <= (HINSTANCE)32)
        success = true;
    }
  }

  return success;
}

void IGraphicsWin::PromptForFile(WDL_String& fileName, WDL_String& path, EFileAction action, const char* ext, IFileDialogCompletionHandlerFunc completionHandler)
{
  if (!WindowIsOpen())
  {
    fileName.Set("");
    return;
  }

  wchar_t fileNameWide[_MAX_PATH];

  UTF8ToUTF16(fileNameWide, fileName.Get(), _MAX_PATH);

  // if (!path.GetLength())
  //   DesktopPath(path);

  UTF8AsUTF16 directoryWide(path);

  OPENFILENAMEW ofn;
  memset(&ofn, 0, sizeof(OPENFILENAMEW));

  ofn.lStructSize = sizeof(OPENFILENAMEW);
  ofn.hwndOwner = (HWND)GetWindow();
  ofn.lpstrFile = fileNameWide;
  ofn.nMaxFile = _MAX_PATH - 1;
  ofn.lpstrInitialDir = directoryWide.Get();
  ofn.Flags = OFN_PATHMUSTEXIST;

  if (CStringHasContents(ext))
  {
    wchar_t extStr[256];
    wchar_t defExtStr[256];
    int i, p, n = strlen(ext);
    bool separator = true;

    for (i = 0, p = 0; i < n; ++i)
    {
      if (separator)
      {
        if (p)
          extStr[p++] = ';';

        separator = false;
        extStr[p++] = '*';
        extStr[p++] = '.';
      }

      if (ext[i] == ' ')
        separator = true;
      else
        extStr[p++] = ext[i];
    }
    extStr[p++] = '\0';

    wcscpy(&extStr[p], extStr);
    extStr[p + p] = '\0';
    ofn.lpstrFilter = extStr;

    for (i = 0, p = 0; i < n && ext[i] != ' '; ++i)
      defExtStr[p++] = ext[i];

    defExtStr[p++] = '\0';
    ofn.lpstrDefExt = defExtStr;
  }

  bool rc = false;

  switch (action)
  {
  case EFileAction::Save:
    ofn.Flags |= OFN_OVERWRITEPROMPT;
    rc = GetSaveFileNameW(&ofn);
    break;
  case EFileAction::Open:
  default:
    ofn.Flags |= OFN_FILEMUSTEXIST;
    rc = GetOpenFileNameW(&ofn);
    break;
  }

  if (rc)
  {
    char drive[_MAX_DRIVE];
    char directoryOutCStr[_MAX_PATH];

    UTF16AsUTF8 tempUTF8(ofn.lpstrFile);

    if (_splitpath_s(tempUTF8.Get(), drive, sizeof(drive), directoryOutCStr, sizeof(directoryOutCStr), NULL, 0, NULL, 0) == 0)
    {
      path.Set(drive);
      path.Append(directoryOutCStr);
    }

    fileName.Set(tempUTF8.Get());
  }
  else
  {
    fileName.Set("");
  }

  // Async is not required on windows, but call the completion handler anyway
  if (completionHandler)
  {
    completionHandler(fileName, path);
  }

  ReleaseMouseCapture();
}

void IGraphicsWin::PromptForDirectory(WDL_String& dir, IFileDialogCompletionHandlerFunc completionHandler)
{
  // If you have a UTF8->UTF16 helper similar to UTF16AsUTF8(), use it here.
  auto Utf8ToUtf16 = [](const char* s) -> std::wstring {
    if (!s) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(len ? len - 1 : 0, L'\0');
    if (len) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), len);
    return w;
  };

  dir.Set("");

  // STA is required for IFileDialog; OleInitialize is fine (calls CoInitializeEx under the hood).
  HRESULT hrInit = ::OleInitialize(nullptr);

  IFileDialog* pfd = nullptr;
  HRESULT hr = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
  if (SUCCEEDED(hr) && pfd)
  {
    // Configure dialog options
    DWORD opts = 0;
    if (SUCCEEDED(pfd->GetOptions(&opts)))
    {
      // Pick folders, only real filesystem, and don't change the process CWD
      opts |= FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
      pfd->SetOptions(opts);
    }

    pfd->SetTitle(L"Choose a Directory");

    // Optional: start from the current value in 'dir' if provided
    if (dir.GetLength() > 0)
    {
      std::wstring wdir = Utf8ToUtf16(dir.Get());
      // Strip any trailing slash to keep SHCreateItemFromParsingName happy
      while (!wdir.empty() && (wdir.back() == L'\\' || wdir.back() == L'/')) wdir.pop_back();

      if (!wdir.empty())
      {
        IShellItem* startItem = nullptr;
        if (SUCCEEDED(::SHCreateItemFromParsingName(wdir.c_str(), nullptr, IID_PPV_ARGS(&startItem))) && startItem)
        {
          pfd->SetFolder(startItem);
          startItem->Release();
        }
      }
    }

    // Show modal to your plug-in window
    hr = pfd->Show(mPlugWnd);

    if (SUCCEEDED(hr))
    {
      IShellItem* result = nullptr;
      if (SUCCEEDED(pfd->GetResult(&result)) && result)
      {
        PWSTR pszPath = nullptr;
        if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath)
        {
          WDL_String chosen(UTF16AsUTF8(pszPath).Get());
          // Ensure trailing backslash to match your previous behavior
          if (chosen.GetLength() && chosen.Get()[chosen.GetLength() - 1] != '\\')
            chosen.Append("\\");
          dir.Set(chosen.Get());

          ::CoTaskMemFree(pszPath);
        }
        result->Release();
      }
    }

    pfd->Release();
  }

  // Call your completion handler either way (mirrors your original code)
  if (completionHandler)
  {
    WDL_String fileName; // not used
    completionHandler(fileName, dir);
  }

  ReleaseMouseCapture();

  if (SUCCEEDED(hrInit)) ::OleUninitialize();
}

static UINT_PTR CALLBACK CCHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
  if (uiMsg == WM_INITDIALOG && lParam)
  {
    CHOOSECOLORW* cc = (CHOOSECOLORW*)lParam;
    if (cc && cc->lCustData)
    {
      const wchar_t* strWide = (const wchar_t*)cc->lCustData;
      SetWindowTextW(hdlg, strWide);
      UINT uiSetRGB;
      uiSetRGB = RegisterWindowMessageW(SETRGBSTRINGW);
      SendMessageW(hdlg, uiSetRGB, 0, (LPARAM)cc->rgbResult);
    }
  }
  return 0;
}

bool IGraphicsWin::PromptForColor(IColor& color, const char* prompt, IColorPickerHandlerFunc func)
{
  ReleaseMouseCapture();

  if (!mPlugWnd)
    return false;

  UTF8AsUTF16 promptWide(prompt);

  const COLORREF w = RGB(255, 255, 255);
  static COLORREF customColorStorage[16] = {w, w, w, w, w, w, w, w, w, w, w, w, w, w, w, w};

  CHOOSECOLORW cc;
  memset(&cc, 0, sizeof(CHOOSECOLORW));
  cc.lStructSize = sizeof(CHOOSECOLORW);
  cc.hwndOwner = mPlugWnd;
  cc.rgbResult = RGB(color.R, color.G, color.B);
  cc.lpCustColors = customColorStorage;
  cc.lCustData = (LPARAM)promptWide.Get();
  cc.lpfnHook = CCHookProc;
  cc.Flags = CC_RGBINIT | CC_ANYCOLOR | CC_FULLOPEN | CC_SOLIDCOLOR | CC_ENABLEHOOK;

  if (ChooseColorW(&cc))
  {
    color.R = GetRValue(cc.rgbResult);
    color.G = GetGValue(cc.rgbResult);
    color.B = GetBValue(cc.rgbResult);

    if (func)
      func(color);

    return true;
  }
  return false;
}

bool IGraphicsWin::OpenURL(const char* url, const char* msgWindowTitle, const char* confirmMsg, const char* errMsgOnFailure)
{
  if (confirmMsg && MessageBoxW(mPlugWnd, UTF8AsUTF16(confirmMsg).Get(), UTF8AsUTF16(msgWindowTitle).Get(), MB_YESNO) != IDYES)
  {
    return false;
  }
  DWORD inetStatus = 0;
  if (InternetGetConnectedState(&inetStatus, 0))
  {
    if (ShellExecuteW(mPlugWnd, L"open", UTF8AsUTF16(url).Get(), 0, 0, SW_SHOWNORMAL) > HINSTANCE(32))
    {
      return true;
    }
  }
  if (errMsgOnFailure)
  {
    MessageBoxW(mPlugWnd, UTF8AsUTF16(errMsgOnFailure).Get(), UTF8AsUTF16(msgWindowTitle).Get(), MB_OK);
  }
  return false;
}

void IGraphicsWin::SetTooltip(const char* tooltip)
{
  UTF8AsUTF16 tipWide(tooltip);
  TOOLINFOW ti = {TTTOOLINFOW_V2_SIZE, 0, mPlugWnd, (UINT_PTR)mPlugWnd, {0, 0, 0, 0}, NULL, NULL, 0, NULL};
  ti.lpszText = const_cast<wchar_t*>(tipWide.Get());
  SendMessageW(mTooltipWnd, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

void IGraphicsWin::ShowTooltip()
{
  if (mTooltipIdx > -1)
  {
    if (auto* pTooltipControl = GetControl(mTooltipIdx))
    {
      const char* tooltipStr = pTooltipControl->GetTooltip();
      if (tooltipStr)
      {
        SetTooltip(tooltipStr);
        mShowingTooltip = true;
      }
    }
  }
}

void IGraphicsWin::HideTooltip()
{
  if (mShowingTooltip)
  {
    SetTooltip(NULL);
    mShowingTooltip = false;
  }
}

bool IGraphicsWin::GetTextFromClipboard(WDL_String& str)
{
  bool result = false;

  if (IsClipboardFormatAvailable(CF_UNICODETEXT))
  {
    if (OpenClipboard(0))
    {
      HGLOBAL hglb = GetClipboardData(CF_UNICODETEXT);

      if (hglb)
      {
        WCHAR* origStr = (WCHAR*)GlobalLock(hglb);

        if (origStr)
        {
          UTF16ToUTF8(str, origStr);
          GlobalUnlock(hglb);
          result = true;
        }
      }
    }

    CloseClipboard();
  }

  if (!result)
    str.Set("");

  return result;
}

bool IGraphicsWin::SetTextInClipboard(const char* str)
{
  if (!OpenClipboard(mMainWnd))
    return false;

  EmptyClipboard();

  bool result = true;

  if (strlen(str))
  {
    // figure out how many characters we need for the wide version of this string
    const int lenWide = UTF8ToUTF16Len(str);

    // allocate global memory object for the text
    HGLOBAL hglbCopy = GlobalAlloc(GMEM_MOVEABLE, lenWide * sizeof(WCHAR));
    if (!hglbCopy)
    {
      CloseClipboard();
      return false;
    }

    // lock the handle
    LPWSTR lpstrCopy = (LPWSTR)GlobalLock(hglbCopy);

    if (lpstrCopy)
    {
      // copy the string into the buffer
      UTF8ToUTF16(lpstrCopy, str, lenWide);
      GlobalUnlock(hglbCopy);

      // place the handle on the clipboard
      result = SetClipboardData(CF_UNICODETEXT, hglbCopy);

      // free the handle if unsuccessful
      if (!result)
        GlobalFree(hglbCopy);
    }
  }

  CloseClipboard();

  return result;
}

bool IGraphicsWin::SetFilePathInClipboard(const char* path)
{
  if (!OpenClipboard(mMainWnd))
    return false;

  EmptyClipboard();

  UTF8AsUTF16 pathWide(path);

  // N.B. GHND ensures that the memory is zeroed

  HGLOBAL hGlobal = GlobalAlloc(GHND, sizeof(DROPFILES) + (sizeof(wchar_t) * (pathWide.GetLength() + 1)));

  if (!hGlobal)
    return false;

  DROPFILES* pDropFiles = (DROPFILES*)GlobalLock(hGlobal);
  bool result = false;

  if (pDropFiles)
  {
    // Populate the dropfile structure and copy the file path

    pDropFiles->pFiles = sizeof(DROPFILES);
    pDropFiles->pt = {0, 0};
    pDropFiles->fNC = true;
    pDropFiles->fWide = true;

    std::copy_n(pathWide.Get(), pathWide.GetLength(), reinterpret_cast<wchar_t*>(&pDropFiles[1]));

    GlobalUnlock(hGlobal);

    result = SetClipboardData(CF_HDROP, hGlobal);
  }

  // free the handle if unsuccessful
  if (!result)
    GlobalFree(hGlobal);

  CloseClipboard();
  return result;
}

bool IGraphicsWin::InitiateExternalFileDragDrop(const char* path, const IRECT& /*iconBounds*/)
{
  using namespace DragAndDropHelpers;
  OleInitialize(nullptr);

  FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};

  DataObject* dataObj = new DataObject(&format, path);
  DropSource* dropSource = new DropSource();

  DWORD dropEffect;
  HRESULT ret = DoDragDrop(dataObj, dropSource, DROPEFFECT_COPY, &dropEffect);
  bool success = SUCCEEDED(ret);

  dataObj->Release();
  dropSource->Release();

  OleUninitialize();

  ReleaseMouseCapture();

  return success;
}

static HFONT GetHFont(const char* fontName, int weight, bool italic, bool underline, DWORD quality = DEFAULT_QUALITY, bool enumerate = false)
{
  HDC hdc = GetDC(NULL);
  HFONT font = nullptr;
  LOGFONTW lFont;

  lFont.lfHeight = 0;
  lFont.lfWidth = 0;
  lFont.lfEscapement = 0;
  lFont.lfOrientation = 0;
  lFont.lfWeight = weight;
  lFont.lfItalic = italic;
  lFont.lfUnderline = underline;
  lFont.lfStrikeOut = false;
  lFont.lfCharSet = DEFAULT_CHARSET;
  lFont.lfOutPrecision = OUT_TT_PRECIS;
  lFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  lFont.lfQuality = quality;
  lFont.lfPitchAndFamily = DEFAULT_PITCH;

  wcsncpy(lFont.lfFaceName, UTF8AsUTF16(fontName).Get(), LF_FACESIZE);

  auto enumProc = [](const LOGFONTW* pLFont, const TEXTMETRICW* pTextMetric, DWORD FontType, LPARAM lParam) { return -1; };

  if ((!enumerate || EnumFontFamiliesExW(hdc, &lFont, enumProc, NULL, 0) == -1))
    font = CreateFontIndirectW(&lFont);

  if (font)
  {
    wchar_t selectedFontName[64] = {'\0'};

    SelectFont(hdc, font);
    GetTextFaceW(hdc, 64, selectedFontName);
    if (strcmp(UTF16AsUTF8(selectedFontName).Get(), fontName))
    {
      DeleteObject(font);
      return nullptr;
    }
  }

  ReleaseDC(NULL, hdc);

  return font;
}

PlatformFontPtr IGraphicsWin::LoadPlatformFont(const char* fontID, const char* fileNameOrResID)
{
  StaticStorage<InstalledFont>::Accessor fontStorage(sPlatformFontCache);

  void* pFontMem = nullptr;
  int resSize = 0;
  WDL_String fullPath;

  const EResourceLocation fontLocation = LocateResource(fileNameOrResID, "ttf", fullPath, GetBundleID(), GetWinModuleHandle(), nullptr);

  if (fontLocation == kNotFound)
    return nullptr;

  switch (fontLocation)
  {
  case kAbsolutePath: {
    HANDLE file = CreateFileW(UTF8AsUTF16(fullPath).Get(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    PlatformFontPtr ret = nullptr;
    if (file)
    {
      HANDLE mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
      if (mapping)
      {
        resSize = (int)GetFileSize(file, nullptr);
        pFontMem = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        ret = LoadPlatformFont(fontID, pFontMem, resSize);
        UnmapViewOfFile(pFontMem);
        CloseHandle(mapping);
      }
      CloseHandle(file);
    }
    return ret;
  }
  break;
  case kWinBinary: {
    pFontMem = const_cast<void*>(LoadWinResource(fullPath.Get(), "ttf", resSize, GetWinModuleHandle()));
    return LoadPlatformFont(fontID, pFontMem, resSize);
  }
  break;
  }

  return nullptr;
}

PlatformFontPtr IGraphicsWin::LoadPlatformFont(const char* fontID, const char* fontName, ETextStyle style)
{
  int weight = style == ETextStyle::Bold ? FW_BOLD : FW_REGULAR;
  bool italic = style == ETextStyle::Italic;
  bool underline = false;
  DWORD quality = DEFAULT_QUALITY;

  HFONT font = GetHFont(fontName, weight, italic, underline, quality, true);

  return PlatformFontPtr(font ? new Font(font, TextStyleString(style), true) : nullptr);
}

PlatformFontPtr IGraphicsWin::LoadPlatformFont(const char* fontID, void* pData, int dataSize)
{
  StaticStorage<InstalledFont>::Accessor fontStorage(sPlatformFontCache);

  std::unique_ptr<InstalledFont> pFont;
  void* pFontMem = pData;
  int resSize = dataSize;

  pFont = std::make_unique<InstalledFont>(pFontMem, resSize);

  if (pFontMem && pFont && pFont->IsValid())
  {
    IFontInfo fontInfo(pFontMem, resSize, 0);
    WDL_String family = fontInfo.GetFamily();
    int weight = fontInfo.IsBold() ? FW_BOLD : FW_REGULAR;
    bool italic = fontInfo.IsItalic();
    bool underline = fontInfo.IsUnderline();

    HFONT font = GetHFont(family.Get(), weight, italic, underline);

    if (font)
    {
      fontStorage.Add(pFont.release(), fontID);
      return PlatformFontPtr(new Font(font, "", false));
    }
  }

  return nullptr;
}

void IGraphicsWin::CachePlatformFont(const char* fontID, const PlatformFontPtr& font)
{
  StaticStorage<HFontHolder>::Accessor hfontStorage(sHFontCache);

  HFONT hfont = font->GetDescriptor();

  if (!hfontStorage.Find(fontID))
    hfontStorage.Add(new HFontHolder(hfont), fontID);
}

DWORD WINAPI VBlankRun(LPVOID lpParam)
{
  IGraphicsWin* pGraphics = (IGraphicsWin*)lpParam;
  return pGraphics->OnVBlankRun();
}

void IGraphicsWin::StartVBlankThread(HWND hWnd)
{
  mVBlankWindow = hWnd;
  mVBlankShutdown = false;
  mVBlankCount.store(0, std::memory_order_relaxed);
  mVBlankMessagePending.store(false, std::memory_order_relaxed);
  mQueuedVBlank.store(0, std::memory_order_relaxed);
  mPendingSyncVBlank.store(0, std::memory_order_relaxed);
  mLastProcessedVBlank = 0;
  mPaintPending.store(false, std::memory_order_relaxed);
  mInstancePaintBudget.Reset();
  mDroppedVBlank.store(0, std::memory_order_relaxed);
  StopVBlankHealthTimer();
  mVBlankPaused.store(false, std::memory_order_release);
  mVBlankConsecutiveDrops.store(0, std::memory_order_release);
  mVBlankHealthCheckAttempts.store(0, std::memory_order_release);
  mVBlankPausedSinceTick = 0;
  mVBlankSoftResetIssued = false;
  for (auto& entry : mVBlankLatencyCounts)
  {
    entry.store(0, std::memory_order_relaxed);
  }
  for (auto& entry : mVBlankLatencyMicros)
  {
    entry.store(0, std::memory_order_relaxed);
  }
  auto subscription = VBlankDispatchWorker::Instance().Subscribe(this, hWnd);
  {
    std::lock_guard<std::mutex> lock(mVBlankSubscriptionMutex);
    mVBlankSubscription = std::move(subscription);
  }
  DWORD threadId = 0;
  mVBlankThread = ::CreateThread(NULL, 0, VBlankRun, this, 0, &threadId);
}

void IGraphicsWin::StopVBlankThread()
{
  if (mVBlankThread != INVALID_HANDLE_VALUE)
  {
    if (mPaintPending.exchange(false, std::memory_order_acq_rel))
    {
      mInstancePaintBudget.OnPaintCompleted(1, GetTickCount64());
    }

    mVBlankShutdown = true;
    std::shared_ptr<VBlankSubscription> subscription;
    {
      std::lock_guard<std::mutex> lock(mVBlankSubscriptionMutex);
      subscription = mVBlankSubscription;
      mVBlankSubscription.reset();
    }
    VBlankDispatchWorker::Instance().Unsubscribe(subscription);
    mVBlankMessagePending.store(false, std::memory_order_release);
    mPendingSyncVBlank.store(0, std::memory_order_release);
    StopVBlankHealthTimer();
    mVBlankPaused.store(false, std::memory_order_release);
    mVBlankConsecutiveDrops.store(0, std::memory_order_release);
    mVBlankHealthCheckAttempts.store(0, std::memory_order_release);
    mVBlankSoftResetIssued = false;
    ::WaitForSingleObject(mVBlankThread, 10000);
    mVBlankThread = INVALID_HANDLE_VALUE;
    mVBlankWindow = 0;
  }
}

// Nasty kernel level definitions for wait for vblank.  Including the
// proper include file requires "d3dkmthk.h" from the driver development
// kit.  Instead we define the minimum needed to call the three methods we need.
// and use LoadLibrary/GetProcAddress to accomplish the same thing.
// See https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/
//
// Heres another link (rant) with a lot of good information about vsync on firefox
// https://www.vsynctester.com/firefoxisbroken.html
// https://bugs.chromium.org/p/chromium/issues/detail?id=467617

// structs to use
typedef UINT32 D3DKMT_HANDLE;
typedef UINT D3DDDI_VIDEO_PRESENT_SOURCE_ID;

typedef struct _D3DKMT_OPENADAPTERFROMHDC
{
  HDC hDc;
  D3DKMT_HANDLE hAdapter;
  LUID AdapterLuid;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;

typedef struct _D3DKMT_CLOSEADAPTER
{
  D3DKMT_HANDLE hAdapter;
} D3DKMT_CLOSEADAPTER;

typedef struct _D3DKMT_WAITFORVERTICALBLANKEVENT
{
  D3DKMT_HANDLE hAdapter;
  D3DKMT_HANDLE hDevice;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} D3DKMT_WAITFORVERTICALBLANKEVENT;

// entry points
typedef NTSTATUS(WINAPI* D3DKMTOpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC* Arg1);
typedef NTSTATUS(WINAPI* D3DKMTCloseAdapter)(const D3DKMT_CLOSEADAPTER* Arg1);
typedef NTSTATUS(WINAPI* D3DKMTWaitForVerticalBlankEvent)(const D3DKMT_WAITFORVERTICALBLANKEVENT* Arg1);

DWORD IGraphicsWin::OnVBlankRun()
{
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

  // TODO: get expected vsync value.  For now we will use a fallback
  // of 60Hz
  float rateFallback = 60.0f;
  int rateMS = (int)(1000.0f / rateFallback);

  // We need to try to load the module and entry points to wait on v blank.
  // if anything fails, we try to gracefully fallback to sleeping for some
  // number of milliseconds.
  //
  // TODO: handle low power modes

  D3DKMTOpenAdapterFromHdc pOpen = nullptr;
  D3DKMTCloseAdapter pClose = nullptr;
  D3DKMTWaitForVerticalBlankEvent pWait = nullptr;
  HINSTANCE hInst = LoadLibraryW(L"gdi32.dll");

  if (hInst != nullptr)
  {
    pOpen = (D3DKMTOpenAdapterFromHdc)GetProcAddress((HMODULE)hInst, "D3DKMTOpenAdapterFromHdc");
    pClose = (D3DKMTCloseAdapter)GetProcAddress((HMODULE)hInst, "D3DKMTCloseAdapter");
    pWait = (D3DKMTWaitForVerticalBlankEvent)GetProcAddress((HMODULE)hInst, "D3DKMTWaitForVerticalBlankEvent");
  }

  // if we don't get bindings to the methods we will fallback
  // to a crummy sleep loop for now.  This is really just a last
  // resort and not expected on modern hardware and Windows OS
  // installs.
  if (!pOpen || !pClose || !pWait)
  {
    while (mVBlankShutdown == false)
    {
      Sleep(rateMS);
      VBlankNotify();
    }
  }
  else
  {
    // we have a good set of functions to call.  We need to keep
    // track of the adapter and reask for it if the device is lost.
    bool adapterIsOpen = false;
    DWORD adapterLastFailTime = 0;
    _D3DKMT_WAITFORVERTICALBLANKEVENT we = {0};

    while (mVBlankShutdown == false)
    {
      if (!adapterIsOpen)
      {
        // reacquire the adapter (at most once a second).
        if (adapterLastFailTime < ::GetTickCount() - 1000)
        {
          // try to get adapter
          D3DKMT_OPENADAPTERFROMHDC openAdapterData = {0};
          HDC hDC = GetDC(mVBlankWindow);
          openAdapterData.hDc = hDC;
          NTSTATUS status = (*pOpen)(&openAdapterData);
          if (status == S_OK)
          {
            // success, setup wait request parameters.
            adapterLastFailTime = 0;
            adapterIsOpen = true;
            we.hAdapter = openAdapterData.hAdapter;
            we.hDevice = 0;
            we.VidPnSourceId = openAdapterData.VidPnSourceId;
          }
          else
          {
            // failed
            adapterLastFailTime = ::GetTickCount();
          }
          DeleteDC(hDC);
        }
      }

      if (adapterIsOpen)
      {
        // Finally we can wait on VBlank
        NTSTATUS status = (*pWait)(&we);
        if (status != S_OK)
        {
          // failed, close now and try again on the next pass.
          _D3DKMT_CLOSEADAPTER ca;
          ca.hAdapter = we.hAdapter;
          (*pClose)(&ca);
          adapterIsOpen = false;
        }
      }

      // Temporary fallback for lost adapter or failed call.
      if (!adapterIsOpen)
      {
        ::Sleep(rateMS);
      }

      // notify logic
      VBlankNotify();
    }

    // cleanup adapter before leaving
    if (adapterIsOpen)
    {
      _D3DKMT_CLOSEADAPTER ca;
      ca.hAdapter = we.hAdapter;
      (*pClose)(&ca);
      adapterIsOpen = false;
    }
  }

  // release module resource
  if (hInst != nullptr)
  {
    FreeLibrary((HMODULE)hInst);
    hInst = nullptr;
  }

  return 0;
}

void IGraphicsWin::VBlankNotify()
{
  if (!mVBlankWindow || mVBlankShutdown)
  {
    return;
  }

  const DWORD latestCount = mVBlankCount.fetch_add(1, std::memory_order_acq_rel) + 1;
  mQueuedVBlank.store(latestCount, std::memory_order_release);

  const int pendingPaints = mInstancePaintBudget.PendingPaints();
  if (pendingPaints > 0)
  {
    return;
  }

  if (mVBlankPaused.load(std::memory_order_acquire))
  {
    mPendingSyncVBlank.store(latestCount, std::memory_order_release);
    return;
  }

  std::shared_ptr<VBlankSubscription> subscription;
  {
    std::lock_guard<std::mutex> lock(mVBlankSubscriptionMutex);
    subscription = mVBlankSubscription;
  }
  if (!subscription || !subscription->active.load(std::memory_order_acquire))
  {
    return;
  }

  if (mVBlankMessagePending.exchange(true, std::memory_order_acq_rel))
  {
    DWORD observed = mPendingSyncVBlank.load(std::memory_order_acquire);
    while (observed < latestCount
           && !mPendingSyncVBlank.compare_exchange_weak(observed, latestCount, std::memory_order_acq_rel,
                                                        std::memory_order_acquire))
    {
    }
    return;
  }

  mPendingSyncVBlank.store(0, std::memory_order_release);

  if (!VBlankDispatchWorker::Instance().QueueDispatch(subscription, latestCount))
  {
    mVBlankMessagePending.store(false, std::memory_order_release);
    schedulerlog::LogEvent(schedulerlog::kCategoryVBlankDispatch,
                           "worker",
                           schedulerlog::Severity::kWarn,
                           {schedulerlog::MakeField("event", "queue_fail"),
                            schedulerlog::MakeField("count", latestCount)});
  }
}

} // namespace iplug::igraphics

#ifndef NO_IGRAPHICS
  #if defined IGRAPHICS_SKIA
    #include "IGraphicsSkia.cpp"
    #ifdef IGRAPHICS_GL
      #include "glad.c"
    #endif
  #elif defined IGRAPHICS_NANOVG
    #include "IGraphicsNanoVG.cpp"
    #ifdef IGRAPHICS_FREETYPE
      #define FONS_USE_FREETYPE
      #pragma comment(lib, "freetype.lib")
    #endif
    #include "glad.c"
    #include "nanovg.c"
  #else
    #error
  #endif
#endif
