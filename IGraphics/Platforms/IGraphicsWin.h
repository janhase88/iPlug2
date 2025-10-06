/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

#include "IPlugPlatform.h"

#include <windows.h>
#include <windowsx.h>
#include <winuser.h>

#include "IGraphicsWinFonts.h"

#include "IGraphics_select.h"
#include "SchedulerLogging.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <atomic>
#include <memory>
#include <array>
#include <initializer_list>
#include <mutex>

#ifdef IGRAPHICS_VULKAN
  #define VK_USE_PLATFORM_WIN32_KHR
  #include <vulkan/vulkan.h>
  #include <vulkan/vulkan_win32.h>
  #include "WinVulkanDeviceCoordinator.h"
#endif


BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

#ifdef IGRAPHICS_VULKAN
struct VulkanContext
{
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
  VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
  VkFence inFlightFence = VK_NULL_HANDLE;
  std::vector<VkImage>* swapchainImages = nullptr;
  VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
  VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
};
#endif

// Forward declare the OLE drop target helper (defined in IGraphicsWin_dnd.h)
namespace DragAndDropHelpers
{
class DropTarget;
}


/** IGraphics platform class for Windows
 * @ingroup PlatformClasses */
struct VBlankSubscription;

class VBlankDispatchWorker;

class IGraphicsWin final : public IGRAPHICS_DRAW_CLASS
{
  using InstalledFont = InstalledWinFont;
  using Font = WinFont;

public:
  IGraphicsWin(IGEditorDelegate& dlg, int w, int h, int fps, float scale);
  ~IGraphicsWin();

  void SetWinModuleHandle(void* pInstance) override { mHInstance = (HINSTANCE)pInstance; }
  void* GetWinModuleHandle() override { return mHInstance; }

  void ForceEndUserEdit() override;
  float GetPlatformWindowScale() const override { return GetScreenScale(); }

  void PlatformResize(bool parentHasResized) override;

  void CheckTabletInput(UINT msg);
  void DestroyEditWindow();

  void HideMouseCursor(bool hide, bool lock) override;
  void MoveMouseCursor(float x, float y) override;
  ECursor SetMouseCursor(ECursor cursorType) override;

  void GetMouseLocation(float& x, float& y) const override;

  void PlatformReleaseMouseCapture() override;
  void PlatformOnCaptureFinished(ITouchID touchID) override;

  EMsgBoxResult ShowMessageBox(const char* str, const char* title, EMsgBoxType type, IMsgBoxCompletionHandlerFunc completionHandler) override;

  void* OpenWindow(void* pParent) override;
  void CloseWindow() override;
  bool WindowIsOpen() override { return (mPlugWnd); }

  void UpdateTooltips() override {}

  bool RevealPathInExplorerOrFinder(WDL_String& path, bool select) override;
  void PromptForFile(WDL_String& fileName, WDL_String& path, EFileAction action, const char* ext, IFileDialogCompletionHandlerFunc completionHandler) override;
  void PromptForDirectory(WDL_String& dir, IFileDialogCompletionHandlerFunc completionHandler) override;
  bool PromptForColor(IColor& color, const char* str, IColorPickerHandlerFunc func) override;

  void OnIdlePacingModeChanged(EIdlePacingMode mode) override;
  void OnHostIdleTick() override;
  void OnHostIdleTick(const HostIdleTickInfo& info) override;

  IPopupMenu* GetItemMenu(long idx, long& idxInMenu, long& offsetIdx, IPopupMenu& baseMenu);
  HMENU CreateMenu(IPopupMenu& menu, long* pOffsetIdx);

  bool OpenURL(const char* url, const char* msgWindowTitle, const char* confirmMsg, const char* errMsgOnFailure) override;

  void* GetWindow() override { return mPlugWnd; }

  const char* GetPlatformAPIStr() override { return "win32"; };

  /** Reload the idle pacing mode from the default configuration file if present */
  void RefreshIdlePacingModeFromDefaultConfig();

  /** Load idle pacing configuration from an absolute file path */
  void LoadIdlePacingModeFromConfigFile(const char* filePath);

  enum class IdlePacingConfigStatus
  {
    kOk = 0,
    kFileMissing,
    kMissingKey,
    kInvalidValue
  };

  /** Parse idle pacing mode from a configuration file */
  IdlePacingConfigStatus ParseIdlePacingModeFromSettings(const char* path, EIdlePacingMode& modeOut) const;

  /** Handle developer console style commands for scheduler toggles */
  bool ApplySchedulerConsoleCommand(const char* command);

  bool GetTextFromClipboard(WDL_String& str) override;
  bool SetTextInClipboard(const char* str) override;
  bool SetFilePathInClipboard(const char* path) override;

  bool InitiateExternalFileDragDrop(const char* path, const IRECT& iconBounds) override;

  // Modern inbound drag & drop via OLE (IDropTarget)
  void OnOLEDropFiles(const std::vector<std::wstring>& filesW, LONG xScreen, LONG yScreen);


  bool PlatformSupportsMultiTouch() const override;

  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK ParamEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static BOOL CALLBACK FindMainWindow(HWND hWnd, LPARAM lParam);

  DWORD OnVBlankRun();

#ifdef IGRAPHICS_VULKAN
  VkResult CreateOrResizeVulkanSwapchain(uint32_t width, uint32_t height, VkSwapchainKHR& swapchain, std::vector<VkImage>& images, VkFormat& format, VkImageUsageFlags& usage, bool& submissionPending);
  bool RecreateVulkanContext();
#endif

protected:
  IPopupMenu* CreatePlatformPopupMenu(IPopupMenu& menu, const IRECT bounds, bool& isAsync) override;
  void CreatePlatformTextEntry(int paramIdx, const IText& text, const IRECT& bounds, int length, const char* str) override;

  void SetTooltip(const char* tooltip);
  void ShowTooltip();
  void HideTooltip();

  HWND GetMainWnd();
  IRECT GetWindowRECT();

private:
  // OLE drag & drop
  DragAndDropHelpers::DropTarget* mDropTarget = nullptr;
  bool mOLEInited = false;

  /** Called either in response to WM_TIMER tick or user message WM_VBLANK, triggered by VSYNC thread
   * @param vBlankCount will allow redraws to get paced by the vblank message. Passing 0 is a WM_TIMER fallback.
   * @param fromVBlankMessage distinguishes real WM_VBLANK deliveries from the WM_TIMER fallback. */
  void OnDisplayTimer(DWORD vBlankCount = 0, bool fromVBlankMessage = false);

  enum EParamEditMsg
  {
    kNone,
    kEditing,
    kUpdate,
    kCancel,
    kCommit
  };

  PlatformFontPtr LoadPlatformFont(const char* fontID, const char* fileNameOrResID) override;
  PlatformFontPtr LoadPlatformFont(const char* fontID, const char* fontName, ETextStyle style) override;
  PlatformFontPtr LoadPlatformFont(const char* fontID, void* pData, int dataSize) override;
  void CachePlatformFont(const char* fontID, const PlatformFontPtr& font) override;

  inline IMouseInfo GetMouseInfo(LPARAM lParam, WPARAM wParam);
  bool MouseCursorIsLocked();

  void ActivateGLContext() override;
  void DeactivateGLContext() override;

#ifdef IGRAPHICS_VULKAN
  bool CreateVulkanContext(); // Vulkan context management
  void DestroyVulkanContext();
  void ActivateVulkanContext();
  void DeactivateVulkanContext();
  void UpdateVulkanAdapterIdentity(const VkPhysicalDeviceIDProperties& idProps);
  void ClearVulkanAdapterIdentity();
  bool GetVulkanAdapterLuid(LUID& luidOut) const;
  uint32_t GetVulkanAdapterNodeMask() const;
  WinVulkanDeviceCoordinator mVulkanDeviceCoordinator;
  uint64_t mVulkanDeviceGeneration = 0;
  VkInstance mVkInstance = VK_NULL_HANDLE;
  VkPhysicalDevice mVkPhysicalDevice = VK_NULL_HANDLE;
  VkDevice mVkDevice = VK_NULL_HANDLE;
  VkSurfaceKHR mVkSurface = VK_NULL_HANDLE;
  VkSwapchainHolder mVkSwapchain;
  VkQueue mPresentQueue = VK_NULL_HANDLE;
  uint32_t mVkQueueFamily = 0;
  VkSemaphoreHolder mImageAvailableSemaphore;
  VkSemaphoreHolder mRenderFinishedSemaphore;
  VkFenceHolder mInFlightFence;
  std::vector<VkImage> mVkSwapchainImages;
  VkFormat mVkFormat = VK_FORMAT_B8G8R8A8_UNORM;
  VkImageUsageFlags mVkSwapchainUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  std::atomic<uint64_t> mVulkanAdapterLuidValue{0};
  std::atomic<bool> mVulkanAdapterLuidValid{false};
  std::atomic<uint32_t> mVulkanAdapterNodeMask{0};
#endif

#ifdef IGRAPHICS_GL
  void CreateGLContext(); // OpenGL context management - TODO: RAII instead ?
  void DestroyGLContext();
  HGLRC mHGLRC = nullptr;
  HGLRC mStartHGLRC = nullptr;
  HDC mStartHDC = nullptr;
  HDC mWindowDC = nullptr;
#endif

  HINSTANCE mHInstance = nullptr;
  HWND mPlugWnd = nullptr;
  HWND mParamEditWnd = nullptr;
  HWND mTooltipWnd = nullptr;
  HWND mParentWnd = nullptr;
  HWND mMainWnd = nullptr;
  WNDPROC mDefEditProc = nullptr;
  HFONT mEditFont = nullptr;
  DWORD mPID = 0;

  void StartVBlankThread(HWND hWnd);
  void StopVBlankThread();
  void VBlankNotify();

#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  static constexpr int kParamQueueWarnThreshold = 12;
  static constexpr int kParamQueueErrorThreshold = 16;
  static constexpr int kParamQueueErrorWindowMs = 100;
#endif

  void RecordVBlankDispatchPosted(DWORD count, uint64_t enqueueMicros);
  void RecordVBlankDispatchHandled(DWORD count, uint64_t handledMicros);
  void EnterVBlankPaused(DWORD failedCount, DWORD errorCode);
  void ExitVBlankPaused(DWORD recoveredCount, ULONGLONG resumeTick);
  void StartVBlankHealthTimer();
  void StopVBlankHealthTimer();
  void PerformVBlankHealthCheck();
  void RequestSwapchainSoftReset(ULONGLONG sincePauseMs);

public:
  // Telemetry helpers in IGraphicsWin.cpp require direct access to these types/constants.
  static constexpr size_t kVBlankLatencySampleCount = 32;
  static constexpr size_t kSchedulerSampleWindow = 120;

  struct InstancePaintBudget
  {
    enum class DecisionKind
    {
      kNone = 0,
      kNeedsDrain,
      kBurstCooling,
      kBudgetExceeded,
      kStaleDrain,
      kTierEscalation,
      kDeferredFlush,
      kDrainComplete
    };

    struct Snapshot
    {
      int pendingPaints = 0;
      int queuedInvalidates = 0;
      int budget = 0;
      bool needsDrain = false;
      bool burstCooling = false;
      ULONGLONG lastDrainTick = 0;
      ULONGLONG burstCoolingDeadline = 0;
      DecisionKind lastDecision = DecisionKind::kNone;
      int lastDecisionRegionCount = 0;
      int lastDecisionWidth = 0;
      int lastDecisionHeight = 0;
      ULONGLONG lastDecisionTick = 0;
      int overBudgetConsecutive = 0;
      int surfacePixels = 0;
    };

    void Configure(int widthPixels, int heightPixels);

    void Reset();

    void OnInvalidateScheduled(int regionCount);

    void OnAdditionalInvalidationQueued(int regionCount);

    void OnPaintCompleted(int drainedRegions, ULONGLONG nowTick);

    int PendingPaints() const;

    int QueuedInvalidates() const;

    int BudgetCeiling() const;

    bool NeedsDrain() const;

    bool MarkNeedsDrain();

    bool ClearNeedsDrainIfRecovered();

    bool EngageBurstCooling(ULONGLONG deadlineTick);

    bool BurstCoolingActive(ULONGLONG nowTick) const;

    bool ClearBurstCooling();

    bool ShouldThrottle(int additionalRegions, ULONGLONG nowTick, DecisionKind& outReason) const;

    void MergeDeferredRegion(const RECT& rect);

    bool ConsumeDeferredRegion(RECT& rectOut);

    void UpdateLastDrainTick(ULONGLONG nowTick);

    void RecordDecision(DecisionKind kind, int regionCount, const RECT& unionRect, ULONGLONG timestamp);

    DecisionKind LastDecision() const;

    int LastDecisionRegionCount() const;

    int LastDecisionWidth() const;

    int LastDecisionHeight() const;

    ULONGLONG LastDecisionTick() const;

    int OverBudgetConsecutive() const;

    int UpdateOverBudgetConsecutive(bool overBudget);

    void SnapshotState(Snapshot& out) const;

  private:
    std::atomic<int> mInflight{0};
    std::atomic<int> mQueuedInvalidates{0};
    std::atomic<int> mBudgetCeiling{3};
    std::atomic<bool> mNeedsDrain{false};
    mutable std::atomic<bool> mBurstCooling{false};
    mutable std::atomic<ULONGLONG> mBurstCoolingDeadline{0};
    std::atomic<ULONGLONG> mLastDrainTick{0};
    std::atomic<int> mSurfacePixels{0};
    RECT mDeferredRegion{0, 0, 0, 0};
    bool mHasDeferredRegion = false;
    std::atomic<int> mOverBudgetConsecutive{0};
    std::atomic<int> mLastDecision{static_cast<int>(DecisionKind::kNone)};
    std::atomic<int> mLastDecisionRegions{0};
    std::atomic<int> mLastDecisionWidth{0};
    std::atomic<int> mLastDecisionHeight{0};
    std::atomic<ULONGLONG> mLastDecisionTick{0};
  };

  struct SchedulerTelemetrySnapshot
  {
    int maxInflight = 0;
    int maxQueued = 0;
    uint64_t histogram[4] = {};
    uint64_t sampleCount = 0;
    uint32_t vblankQueueHighWater = 0;
    uint32_t vblankQueueWarnCount = 0;
    uint64_t vblankDispatches = 0;
    uint32_t vblankLatencySampleCount = 0;
    double vblankLatencyMs[kVBlankLatencySampleCount] = {};
    uint32_t pendingFlushHighWater = 0;
    uint32_t idleStretchHighWaterHundredths = 100;
    uint32_t paramQueueHighWater = 0;
    uint32_t paramQueueSampleCount = 0;
    uint32_t paramQueueErrorSamples = 0;
    uint32_t droppedVBlankTotal = 0;
    uint32_t schedulerSampleCursor = 0;
    uint32_t schedulerSampleCount = 0;
    uint32_t queuedInvalidatesWindow[kSchedulerSampleWindow] = {};
    uint32_t pendingPaintsWindow[kSchedulerSampleWindow] = {};
    uint32_t pendingFlushWindow[kSchedulerSampleWindow] = {};
    uint32_t idleStretchHundredthsWindow[kSchedulerSampleWindow] = {};
    uint32_t paramQueueOutstandingWindow[kSchedulerSampleWindow] = {};
    uint32_t droppedVBlankWindow[kSchedulerSampleWindow] = {};
    uint32_t idleForgivenessWindow[kSchedulerSampleWindow] = {};
    uint32_t idleTimerBehindWindow[kSchedulerSampleWindow] = {};
  };

private:
  friend class VBlankDispatchWorker;

  HWND mVBlankWindow = 0;                      // Window to post messages to for every vsync
  volatile bool mVBlankShutdown = false;       // Flag to indiciate that the vsync thread should shutdown
  HANDLE mVBlankThread = INVALID_HANDLE_VALUE; // ID of thread.
  std::atomic<DWORD> mVBlankCount{0};          // running count of vblank events since the start of the window.
  std::atomic<bool> mVBlankMessagePending{false}; // true while a WM_VBLANK message is outstanding on the UI queue
  std::atomic<DWORD> mQueuedVBlank{0};         // newest vblank counter queued for delivery to the UI thread
  std::atomic<DWORD> mPendingSyncVBlank{0};    // newest tick awaiting a fallback WM_VBLANK send when PostMessageW fails
  DWORD mLastProcessedVBlank = 0;              // last WM_VBLANK tick serviced by the UI thread
  int mVBlankSkipUntil = 0;                    // support for skipping vblank notification if the last callback took too long.
                                              // This helps keep the message pump clear in the case of overload.
  std::shared_ptr<VBlankSubscription> mVBlankSubscription; // worker registration for bounded WM_VBLANK dispatch
  mutable std::mutex mVBlankSubscriptionMutex;             // guards subscription access across threads
  std::atomic<uint32_t> mDroppedVBlank{0};     // number of ticks abandoned after exhausting retries
  std::atomic<bool> mVBlankPaused{false};
  std::atomic<bool> mVBlankHealthTimerActive{false};
  std::atomic<uint32_t> mVBlankConsecutiveDrops{0};
  std::atomic<uint32_t> mVBlankHealthCheckAttempts{0};
  ULONGLONG mVBlankPausedSinceTick = 0;
  bool mVBlankSoftResetIssued = false;
  std::array<std::atomic<DWORD>, kVBlankLatencySampleCount> mVBlankLatencyCounts{};
  std::array<std::atomic<uint64_t>, kVBlankLatencySampleCount> mVBlankLatencyMicros{};
  bool mVSYNCEnabled = false;
  bool mDeferInvalidation = false;
  static SchedulerTelemetrySnapshot GetSchedulerTelemetrySnapshot();
  static void ResetSchedulerTelemetrySnapshot();

  void FlushDeferredInvalidations();

  int UpdateOverBudgetTracking();
  void PublishPaintBudgetSnapshot(const char* stage, const char* reason, int drainedRegions, ULONGLONG nowTick, bool recordHistogram);
  void RefreshPaintBudgetHUD();
  void UpdateSchedulerHUD(const InstancePaintBudget::Snapshot& snapshot);

  void InitializeIdlePacingConfiguration();
  bool ApplyIdlePacingModeString(const std::string& modeString, bool fromConfig = false);

#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  struct SchedulerState
  {
    enum class ThrottleState
    {
      kNormal = 0,
      kBurstCooling,
      kIdleCatchUp
    };

    ThrottleState throttleState = ThrottleState::kNormal;
    ULONGLONG lastIdleTick = 0;
    ULONGLONG stateEnteredTick = 0;
    int baseCadenceMs = 50;
    int idleCadenceTargetMs = 50;
    double idleStretchFactor = 1.0;
    int pendingParamFlush = 0;
    ULONGLONG forgivenessDeadlineTick = 0;
    int lastIdleOutstanding = 0;
    int lastIdleParamDepthBefore = 0;
    int lastIdleParamDepthAfter = 0;
    int lastIdleProcessed = 0;
    double lastIdleElapsedMs = 0.0;
    bool lastIdleTimerBehind = false;
    ULONGLONG lastIdleSampleTick = 0;
    ULONGLONG paramQueueAboveThresholdSince = 0;
    int paramQueueHighWater = 0;

    void Reset(EIdlePacingMode mode, ULONGLONG nowTick);
  };

  void InitializeIdleSchedulerState();
  void ResetIdleSchedulerState(EIdlePacingMode mode, ULONGLONG nowTick);
  void EnterIdleState(SchedulerState::ThrottleState newState,
                      double stretchFactor,
                      const char* stage,
                      ULONGLONG nowTick,
                      std::initializer_list<schedulerlog::Field> extraFields = {});
  void OnIdleThrottleTriggered(ULONGLONG nowTick, InstancePaintBudget::DecisionKind reason, int regionCount);
  void OnIdleDrainComplete(ULONGLONG nowTick, int drainedRegions);
  int ComputeIdleCadenceMs(double stretchFactor) const;
  const char* IdleStateLabel(SchedulerState::ThrottleState state) const;
  bool MaybeExtendIdleForgiveness(ULONGLONG nowTick,
                                  int requestMs,
                                  const HostIdleTickInfo& info,
                                  const char* reason,
                                  schedulerlog::Severity severity = schedulerlog::Severity::kInfo);
  void MaybeExpireIdleForgiveness(ULONGLONG nowTick,
                                  bool forgivenessExtended,
                                  const HostIdleTickInfo& info);

  static constexpr int kIdleCadenceLegacyMs = 50;
  static constexpr int kIdleCadenceLocked60Ms = 16;
  static constexpr double kBurstCoolingStretch = 2.0;
  static constexpr double kIdleCatchUpStretch = 0.5;
  static constexpr int kIdleForgivenessMinMs = 20;
  static constexpr int kIdleForgivenessMaxMs = 500;
  static constexpr int kIdleForgivenessDefaultMs = 75;
#endif

  std::atomic<bool> mPaintPending{false};
  InstancePaintBudget mInstancePaintBudget;
#if IGRAPHICS_SCHED_IDLE_EXPERIMENTAL
  SchedulerState mSchedulerState;
#endif
  bool mIdlePacingModeInitialized = false;
  bool mIdlePacingModeFromConfig = false;
  WDL_String mIdlePacingConfigPath;

  const IParam* mEditParam = nullptr;
  IText mEditText;
  IRECT mEditRECT;

  EParamEditMsg mParamEditMsg = kNone;
  bool mShowingTooltip = false;
  float mHiddenCursorX = 0.f;
  float mHiddenCursorY = 0.f;
  int mTooltipIdx = -1;

  WDL_String mMainWndClassName;

  static StaticStorage<InstalledFont> sPlatformFontCache;
  static StaticStorage<HFontHolder> sHFontCache;

  std::unordered_map<ITouchID, IMouseInfo> mDeltaCapture; // associative array of touch id pointers to IMouseInfo structs, so that we can get deltas
};

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
