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

using namespace iplug;

#if defined OS_MAC || defined OS_IOS

Timer* Timer::Create(ITimerFunction func, uint32_t intervalMs)
{
  return new Timer_impl(func, intervalMs);
}

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

Timer_impl::~Timer_impl()
{
  Stop();
}

void Timer_impl::Stop()
{
  if (mOSTimer)
  {
    CFRunLoopTimerInvalidate(mOSTimer);
    CFRelease(mOSTimer);
    mOSTimer = nullptr;
  }
}

void Timer_impl::TimerProc(CFRunLoopTimerRef timer, void *info)
{
  Timer_impl* itimer = (Timer_impl*) info;
  itimer->mTimerFunc(*itimer);
}

#elif defined OS_WIN

Timer* Timer::Create(ITimerFunction func, uint32_t intervalMs)
{
  return new Timer_impl(func, intervalMs);
}

WDL_Mutex Timer_impl::sMutex;
HWND Timer_impl::sMessageWindow = nullptr;
HANDLE Timer_impl::sTimerQueue = nullptr;
const UINT Timer_impl::kTimerMessage = WM_APP + 0x44F3;
WDL_PtrList<Timer_impl> Timer_impl::sTimers;

Timer_impl::Timer_impl(ITimerFunction func, uint32_t intervalMs)
: mTimerFunc(func)
{
  mMessageWindow = EnsureMessageWindow();

  if (!mMessageWindow)
    return;

  HANDLE queue = EnsureTimerQueue();

  if (!queue)
    return;

  if (CreateTimerQueueTimer(&mTimerHandle, queue, TimerQueueCallback, this, intervalMs, intervalMs, WT_EXECUTEDEFAULT))
  {
    {
      WDL_MutexLock lock(&sMutex);
      sTimers.Add(this);
    }

    mRunning.store(true, std::memory_order_release);
  }
}

Timer_impl::~Timer_impl()
{
  Stop();
}

void Timer_impl::Stop()
{
  if (!mTimerHandle && !mRunning.load(std::memory_order_acquire))
    return;

  mRunning.store(false, std::memory_order_release);

  if (mTimerHandle)
  {
    HANDLE queue = sTimerQueue;

    if (queue)
      DeleteTimerQueueTimer(queue, mTimerHandle, INVALID_HANDLE_VALUE);

    mTimerHandle = nullptr;
  }

  {
    WDL_MutexLock lock(&sMutex);
    sTimers.DeletePtr(this);
  }

  mCallbackPending.store(false, std::memory_order_release);
}

HWND Timer_impl::EnsureMessageWindow()
{
  if (sMessageWindow)
    return sMessageWindow;

  WDL_MutexLock lock(&sMutex);

  if (!sMessageWindow)
  {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MessageWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"iplug2_timer_window";

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return nullptr;

    sMessageWindow = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  }

  return sMessageWindow;
}

HANDLE Timer_impl::EnsureTimerQueue()
{
  if (sTimerQueue)
    return sTimerQueue;

  WDL_MutexLock lock(&sMutex);

  if (!sTimerQueue)
    sTimerQueue = CreateTimerQueue();

  return sTimerQueue;
}

LRESULT CALLBACK Timer_impl::MessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (msg == kTimerMessage)
  {
    auto* pTimer = reinterpret_cast<Timer_impl*>(wParam);

    bool isActive = false;

    if (pTimer)
    {
      WDL_MutexLock lock(&sMutex);
      isActive = sTimers.Find(pTimer) >= 0;
    }

    if (isActive && pTimer->mRunning.load(std::memory_order_acquire))
    {
      pTimer->mCallbackPending.store(false, std::memory_order_release);
      pTimer->mTimerFunc(*pTimer);
    }

    return 0;
  }

  return DefWindowProc(hwnd, msg, wParam, lParam);
}

VOID CALLBACK Timer_impl::TimerQueueCallback(PVOID param, BOOLEAN)
{
  auto* pTimer = static_cast<Timer_impl*>(param);

  if (!pTimer)
    return;

  if (!pTimer->mRunning.load(std::memory_order_acquire))
    return;

  bool expected = false;

  if (!pTimer->mCallbackPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    return;

  if (pTimer->mMessageWindow)
  {
    if (!PostMessageW(pTimer->mMessageWindow, kTimerMessage, reinterpret_cast<WPARAM>(pTimer), 0))
      pTimer->mCallbackPending.store(false, std::memory_order_release);
  }
  else
  {
    pTimer->mCallbackPending.store(false, std::memory_order_release);
  }
}
#elif defined OS_WEB
Timer* Timer::Create(ITimerFunction func, uint32_t intervalMs)
{
  return new Timer_impl(func, intervalMs);
}

Timer_impl::Timer_impl(ITimerFunction func, uint32_t intervalMs)
: mTimerFunc(func)
{
  ID = emscripten_set_interval(TimerProc, intervalMs, this);
}

Timer_impl::~Timer_impl()
{
  Stop();
}

void Timer_impl::Stop()
{
  emscripten_clear_interval(ID);
}

void Timer_impl::TimerProc(void* userData)
{
  Timer_impl* itimer = (Timer_impl*) userData;
  itimer->mTimerFunc(*itimer);
}
#endif
