#pragma once

#if defined OS_WIN

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace iplug::win
{
namespace detail
{
using GetDpiForWindowFunc = UINT(WINAPI*)(HWND);
using GetDpiForMonitorFunc = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
using GetScaleFactorForMonitorFunc = HRESULT(WINAPI*)(HMONITOR, int*);
using SetThreadDpiAwarenessContextFunc = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
using SetWindowDpiAwarenessContextFunc = BOOL(WINAPI*)(HWND, DPI_AWARENESS_CONTEXT);
#if defined(DPI_HOSTING_BEHAVIOR_MIXED)
using SetThreadDpiHostingBehaviorFunc = DPI_HOSTING_BEHAVIOR(WINAPI*)(DPI_HOSTING_BEHAVIOR);
#endif

inline GetDpiForWindowFunc LoadGetDpiForWindow()
{
  static GetDpiForWindowFunc fn = []() -> GetDpiForWindowFunc {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
      user32 = LoadLibraryW(L"user32.dll");
    if (!user32)
      return nullptr;

    return reinterpret_cast<GetDpiForWindowFunc>(GetProcAddress(user32, "GetDpiForWindow"));
  }();

  return fn;
}

inline GetDpiForMonitorFunc LoadGetDpiForMonitor()
{
  static GetDpiForMonitorFunc fn = []() -> GetDpiForMonitorFunc {
    HMODULE shcore = GetModuleHandleW(L"shcore.dll");
    if (!shcore)
      shcore = LoadLibraryW(L"shcore.dll");
    if (!shcore)
      return nullptr;

    return reinterpret_cast<GetDpiForMonitorFunc>(GetProcAddress(shcore, "GetDpiForMonitor"));
  }();

  return fn;
}

inline GetScaleFactorForMonitorFunc LoadGetScaleFactorForMonitor()
{
  static GetScaleFactorForMonitorFunc fn = []() -> GetScaleFactorForMonitorFunc {
    HMODULE shcore = GetModuleHandleW(L"shcore.dll");
    if (!shcore)
      shcore = LoadLibraryW(L"shcore.dll");
    if (!shcore)
      return nullptr;

    return reinterpret_cast<GetScaleFactorForMonitorFunc>(GetProcAddress(shcore, "GetScaleFactorForMonitor"));
  }();

  return fn;
}

inline SetThreadDpiAwarenessContextFunc LoadSetThreadDpiAwarenessContext()
{
  static SetThreadDpiAwarenessContextFunc fn = []() -> SetThreadDpiAwarenessContextFunc {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
      user32 = LoadLibraryW(L"user32.dll");
    if (!user32)
      return nullptr;

    return reinterpret_cast<SetThreadDpiAwarenessContextFunc>(GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
  }();

  return fn;
}

inline SetWindowDpiAwarenessContextFunc LoadSetWindowDpiAwarenessContext()
{
  static SetWindowDpiAwarenessContextFunc fn = []() -> SetWindowDpiAwarenessContextFunc {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
      user32 = LoadLibraryW(L"user32.dll");
    if (!user32)
      return nullptr;

    return reinterpret_cast<SetWindowDpiAwarenessContextFunc>(GetProcAddress(user32, "SetWindowDpiAwarenessContext"));
  }();

  return fn;
}

#if defined(DPI_HOSTING_BEHAVIOR_MIXED)
inline SetThreadDpiHostingBehaviorFunc LoadSetThreadDpiHostingBehavior()
{
  static SetThreadDpiHostingBehaviorFunc fn = []() -> SetThreadDpiHostingBehaviorFunc {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
      user32 = LoadLibraryW(L"user32.dll");
    if (!user32)
      return nullptr;

    return reinterpret_cast<SetThreadDpiHostingBehaviorFunc>(GetProcAddress(user32, "SetThreadDpiHostingBehavior"));
  }();

  return fn;
}
#endif

inline UINT QueryWindowDpi(HWND hWnd)
{
  if (auto fn = LoadGetDpiForWindow())
  {
    if (hWnd)
    {
      if (UINT dpi = fn(hWnd))
        return dpi;
    }
  }

  return USER_DEFAULT_SCREEN_DPI;
}

inline UINT QueryMonitorDpi(HWND hWnd)
{
  constexpr int kEffectiveDpiType = 0; // MDT_EFFECTIVE_DPI

  HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
  if (!monitor)
    return 0;

  if (auto fn = LoadGetDpiForMonitor())
  {
    UINT dpiX = 0;
    UINT dpiY = 0;

    if (fn(monitor, kEffectiveDpiType, &dpiX, &dpiY) == S_OK && dpiX != 0)
      return dpiX;
  }

  if (auto scaleFn = LoadGetScaleFactorForMonitor())
  {
    int scaleFactor = 0;
    if (scaleFn(monitor, &scaleFactor) == S_OK && scaleFactor > 0)
    {
      const UINT dpiFromScale = static_cast<UINT>((scaleFactor * USER_DEFAULT_SCREEN_DPI + 50) / 100);
      if (dpiFromScale != 0)
        return dpiFromScale;
    }
  }

  const HWND targetWnd = hWnd ? hWnd : GetDesktopWindow();
  HDC dc = targetWnd ? GetDC(targetWnd) : nullptr;
  if (!dc)
    return 0;

  const int logicalWidth = GetDeviceCaps(dc, HORZRES);
  const int physicalWidth = GetDeviceCaps(dc, DESKTOPHORZRES);
  const int logicalDpi = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(targetWnd, dc);

  if (logicalDpi <= 0)
    return 0;

  float virtualization = 1.f;
  if (logicalWidth > 0 && physicalWidth > 0)
    virtualization = static_cast<float>(physicalWidth) / static_cast<float>(logicalWidth);

  if (!std::isfinite(virtualization) || virtualization <= 0.f)
    virtualization = 1.f;

  const float effectiveDpi = static_cast<float>(logicalDpi) * virtualization;
  if (effectiveDpi <= 0.f || !std::isfinite(effectiveDpi))
    return 0;

  return static_cast<UINT>(std::round(effectiveDpi));
}

inline float ScaleFromDpi(UINT dpi)
{
  if (dpi == 0)
    return 1.f;

  return static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
}
} // namespace detail

class ScopedThreadDpiAwarenessContext
{
public:
  explicit ScopedThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT targetContext)
  {
    auto fn = detail::LoadSetThreadDpiAwarenessContext();
    if (!fn || targetContext == nullptr)
      return;

    DPI_AWARENESS_CONTEXT previous = fn(targetContext);
    if (!previous)
      return;

    mSetter = fn;
    mPrevious = previous;
  }

  ScopedThreadDpiAwarenessContext(const ScopedThreadDpiAwarenessContext&) = delete;
  ScopedThreadDpiAwarenessContext& operator=(const ScopedThreadDpiAwarenessContext&) = delete;

  ScopedThreadDpiAwarenessContext(ScopedThreadDpiAwarenessContext&& other) noexcept
  {
    mSetter = other.mSetter;
    mPrevious = other.mPrevious;
    other.mSetter = nullptr;
    other.mPrevious = nullptr;
  }

  ScopedThreadDpiAwarenessContext& operator=(ScopedThreadDpiAwarenessContext&& other) noexcept
  {
    if (this != &other)
    {
      Reset();
      mSetter = other.mSetter;
      mPrevious = other.mPrevious;
      other.mSetter = nullptr;
      other.mPrevious = nullptr;
    }
    return *this;
  }

  ~ScopedThreadDpiAwarenessContext()
  {
    Reset();
  }

  void Reset()
  {
    if (mSetter && mPrevious)
    {
      mSetter(mPrevious);
    }
    mSetter = nullptr;
    mPrevious = nullptr;
  }

private:
  detail::SetThreadDpiAwarenessContextFunc mSetter = nullptr;
  DPI_AWARENESS_CONTEXT mPrevious = nullptr;
};

class ScopedPerMonitorDpiAwarenessContext : public ScopedThreadDpiAwarenessContext
{
public:
#ifdef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
  ScopedPerMonitorDpiAwarenessContext()
    : ScopedThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
  {
  }
#else
  ScopedPerMonitorDpiAwarenessContext()
    : ScopedThreadDpiAwarenessContext(nullptr)
  {
  }
#endif
};

#if defined(DPI_HOSTING_BEHAVIOR_MIXED)
class ScopedThreadDpiHostingBehavior
{
public:
  explicit ScopedThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR target)
  {
    auto fn = detail::LoadSetThreadDpiHostingBehavior();
    if (!fn)
      return;

    if (target == DPI_HOSTING_BEHAVIOR_INVALID)
      return;

    DPI_HOSTING_BEHAVIOR previous = fn(target);
    if (previous == DPI_HOSTING_BEHAVIOR_INVALID)
      return;

    mSetter = fn;
    mPrevious = previous;
  }

  ScopedThreadDpiHostingBehavior(const ScopedThreadDpiHostingBehavior&) = delete;
  ScopedThreadDpiHostingBehavior& operator=(const ScopedThreadDpiHostingBehavior&) = delete;

  ScopedThreadDpiHostingBehavior(ScopedThreadDpiHostingBehavior&& other) noexcept
  {
    mSetter = other.mSetter;
    mPrevious = other.mPrevious;
    other.mSetter = nullptr;
    other.mPrevious = DPI_HOSTING_BEHAVIOR_INVALID;
  }

  ScopedThreadDpiHostingBehavior& operator=(ScopedThreadDpiHostingBehavior&& other) noexcept
  {
    if (this != &other)
    {
      Reset();
      mSetter = other.mSetter;
      mPrevious = other.mPrevious;
      other.mSetter = nullptr;
      other.mPrevious = DPI_HOSTING_BEHAVIOR_INVALID;
    }
    return *this;
  }

  ~ScopedThreadDpiHostingBehavior()
  {
    Reset();
  }

  void Reset()
  {
    if (mSetter)
    {
      mSetter(mPrevious);
    }
    mSetter = nullptr;
    mPrevious = DPI_HOSTING_BEHAVIOR_INVALID;
  }

  bool Applied() const { return mSetter != nullptr; }

private:
  detail::SetThreadDpiHostingBehaviorFunc mSetter = nullptr;
  DPI_HOSTING_BEHAVIOR mPrevious = DPI_HOSTING_BEHAVIOR_INVALID;
};
#else
class ScopedThreadDpiHostingBehavior
{
public:
  explicit ScopedThreadDpiHostingBehavior(int) {}
  void Reset() {}
  bool Applied() const { return false; }
};
#endif

inline bool TrySetWindowDpiAwarenessContext(HWND hWnd, DPI_AWARENESS_CONTEXT targetContext)
{
  if (!hWnd || targetContext == nullptr)
    return false;

  if (auto fn = detail::LoadSetWindowDpiAwarenessContext())
  {
    return fn(hWnd, targetContext) != FALSE;
  }

  return false;
}

inline bool TrySetWindowPerMonitorDpiAwareness(HWND hWnd)
{
#ifdef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
  return TrySetWindowDpiAwarenessContext(hWnd, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#else
  (void)hWnd;
  return false;
#endif
}

struct DpiScales
{
  float window = 1.f;
  float monitor = 1.f;
};

inline DpiScales GetDpiScalesForHWND(HWND hWnd)
{
  DpiScales result{};

  const UINT windowDpi = detail::QueryWindowDpi(hWnd);
  result.window = detail::ScaleFromDpi(windowDpi);

  UINT monitorDpi = detail::QueryMonitorDpi(hWnd);
  if (monitorDpi == 0)
    monitorDpi = windowDpi;

  result.monitor = detail::ScaleFromDpi(monitorDpi);

  return result;
}
} // namespace iplug::win

inline float GetScaleForHWND(HWND hWnd)
{
  return iplug::win::GetDpiScalesForHWND(hWnd).window;
}

inline float GetPhysicalScaleForHWND(HWND hWnd)
{
  return iplug::win::GetDpiScalesForHWND(hWnd).monitor;
}

inline float GetBackingScaleForHWND(HWND hWnd)
{
  const auto scales = iplug::win::GetDpiScalesForHWND(hWnd);
  const float denominator = std::max(scales.window, std::numeric_limits<float>::epsilon());
  return scales.monitor / denominator;
}

#endif
