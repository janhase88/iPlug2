#pragma once

#if defined OS_WIN

#include <windows.h>

#include <algorithm>
#include <limits>

namespace iplug::win
{
namespace detail
{
using GetDpiForWindowFunc = UINT(WINAPI*)(HWND);
using GetDpiForMonitorFunc = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);

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
  auto fn = LoadGetDpiForMonitor();
  if (!fn)
    return 0;

  constexpr int kEffectiveDpiType = 0; // MDT_EFFECTIVE_DPI

  HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
  if (!monitor)
    return 0;

  UINT dpiX = 0;
  UINT dpiY = 0;

  if (fn(monitor, kEffectiveDpiType, &dpiX, &dpiY) == S_OK && dpiX != 0)
    return dpiX;

  return 0;
}

inline float ScaleFromDpi(UINT dpi)
{
  if (dpi == 0)
    return 1.f;

  return static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
}
} // namespace detail

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
