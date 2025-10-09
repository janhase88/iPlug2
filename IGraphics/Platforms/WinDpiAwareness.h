#pragma once

#if defined OS_WIN

#include <windows.h>
#include <cstdint>

namespace iplug::igraphics
{
namespace detail
{
using SetThreadDpiAwarenessContextFunc = void* (WINAPI*)(void*);

inline SetThreadDpiAwarenessContextFunc LoadSetThreadDpiAwarenessContext()
{
  static SetThreadDpiAwarenessContextFunc setter = []() -> SetThreadDpiAwarenessContextFunc {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
      user32 = LoadLibraryW(L"user32.dll");
    if (!user32)
      return nullptr;

    return reinterpret_cast<SetThreadDpiAwarenessContextFunc>(
      GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
  }();

  return setter;
}

inline void* GetPerMonitorAwareV2Context()
{
#ifdef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
  return reinterpret_cast<void*>(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#else
  return reinterpret_cast<void*>(static_cast<intptr_t>(-4));
#endif
}
} // namespace detail

class ScopedPerMonitorDpiAwareness
{
public:
  ScopedPerMonitorDpiAwareness()
  {
    mSetter = detail::LoadSetThreadDpiAwarenessContext();
    if (mSetter)
    {
      mPrevious = mSetter(detail::GetPerMonitorAwareV2Context());
    }
  }

  ~ScopedPerMonitorDpiAwareness()
  {
    if (mSetter && mPrevious)
    {
      mSetter(mPrevious);
    }
  }

  ScopedPerMonitorDpiAwareness(const ScopedPerMonitorDpiAwareness&) = delete;
  ScopedPerMonitorDpiAwareness& operator=(const ScopedPerMonitorDpiAwareness&) = delete;

private:
  detail::SetThreadDpiAwarenessContextFunc mSetter = nullptr;
  void* mPrevious = nullptr;
};

} // namespace iplug::igraphics

#endif
