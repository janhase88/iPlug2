/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

/**
 * @file
 * @brief Timer implementation
 */

#include "IPlugTimer.h"
#include <cassert>

using namespace iplug;

#if defined OS_MAC || defined OS_IOS

Timer* Timer::Create(ITimerFunction func, uint32_t intervalMs) { return new Timer_impl(func, intervalMs); }

Timer_impl::Timer_impl(ITimerFunction func, uint32_t intervalMs)
  : mTimerFunc(func)
  , mIntervalMs(intervalMs)
{
  CFRunLoopTimerContext context;
  context.version = 0;
  context.info = this;
  context.retain = nullptr;
  context.release = nullptr;
  context.copyDescription = nullptr;
  CFTimeInterval interval = intervalMs / 1000.0;
  CFRunLoopRef runLoop = CFRunLoopGetMain();
  mOSTimer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent(), interval, 0, 0, TimerProc, &context);
  CFRunLoopAddTimer(runLoop, mOSTimer, kCFRunLoopCommonModes);
}

Timer_impl::~Timer_impl() { Stop(); }

void Timer_impl::Stop()
{
  if (mOSTimer)
  {
    CFRunLoopTimerInvalidate(mOSTimer);
    CFRelease(mOSTimer);
    mOSTimer = nullptr;
  }
}

void Timer_impl::TimerProc(CFRunLoopTimerRef timer, void* info)
{
  Timer_impl* itimer = (Timer_impl*)info;
  itimer->mTimerFunc(*itimer);
}

#elif defined OS_WIN

Timer* Timer::Create(ITimerFunction func, uint32_t intervalMs) { return new Timer_impl(func, intervalMs); }

static const UINT kTimerMsg = WM_APP + 0x100;
static const wchar_t* kTimerWndClass = L"IPlugTimerWnd";

Timer_impl::Timer_impl(ITimerFunction func, uint32_t intervalMs)
  : mTimerFunc(func)
  , mIntervalMs(intervalMs)
{
  if (!mWindowClass)
  {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = Timer_impl::WndProc;
    wc.lpszClassName = kTimerWndClass;
    mWindowClass = RegisterClassW(&wc);
    assert(mWindowClass && "Timer window class registration failed");
  }

  mMsgWnd = CreateWindowExW(0, reinterpret_cast<LPCWSTR>(MAKEINTATOM(mWindowClass)), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), this);
  assert(mMsgWnd && "Timer message window creation failed");

  BOOL ok = CreateTimerQueueTimer(&mTimer, nullptr, Timer_impl::TimerProc, this, mIntervalMs, mIntervalMs, WT_EXECUTEDEFAULT);
  assert(ok && "CreateTimerQueueTimer failed");
}

Timer_impl::~Timer_impl()
{
  Stop();

  if (mWindowClass)
  {
    UnregisterClassW(reinterpret_cast<LPCWSTR>(MAKEINTATOM(mWindowClass)), GetModuleHandle(nullptr));
    mWindowClass = 0;
  }
}

void Timer_impl::Stop()
{
  if (mTimer)
  {
    DeleteTimerQueueTimer(nullptr, mTimer, INVALID_HANDLE_VALUE);
    mTimer = nullptr;
  }

  if (mMsgWnd)
  {
    DestroyWindow(mMsgWnd);
    mMsgWnd = nullptr;
  }
}

VOID CALLBACK Timer_impl::TimerProc(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
  Timer_impl* pTimer = static_cast<Timer_impl*>(lpParam);
  if (pTimer && pTimer->mMsgWnd)
    PostMessage(pTimer->mMsgWnd, kTimerMsg, 0, 0);
}

LRESULT CALLBACK Timer_impl::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW* pcs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pcs->lpCreateParams));
    return 1;
  }

  if (msg == kTimerMsg)
  {
    Timer_impl* pTimer = reinterpret_cast<Timer_impl*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (pTimer)
      pTimer->mTimerFunc(*pTimer);
    return 0;
  }

  return DefWindowProcW(hWnd, msg, wParam, lParam);
}
#elif defined OS_WEB
Timer* Timer::Create(ITimerFunction func, uint32_t intervalMs) { return new Timer_impl(func, intervalMs); }

Timer_impl::Timer_impl(ITimerFunction func, uint32_t intervalMs)
  : mTimerFunc(func)
{
  ID = emscripten_set_interval(TimerProc, intervalMs, this);
}

Timer_impl::~Timer_impl() { Stop(); }

void Timer_impl::Stop() { emscripten_clear_interval(ID); }

void Timer_impl::TimerProc(void* userData)
{
  Timer_impl* itimer = (Timer_impl*)userData;
  itimer->mTimerFunc(*itimer);
}
#endif
