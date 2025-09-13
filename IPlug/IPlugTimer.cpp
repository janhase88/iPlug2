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
#include <atomic>

using namespace iplug;

namespace
{
  // Number of active timers across all owners
  std::atomic<int> sTimerCount{0};
}

Timer::Timer(void* owner)
  : mOwner(owner)
{
  sTimerCount.fetch_add(1, std::memory_order_relaxed);
}

Timer::~Timer()
{
  sTimerCount.fetch_sub(1, std::memory_order_relaxed);
}

int Timer::GetActiveTimerCount()
{
  return sTimerCount.load(std::memory_order_relaxed);
}

#if defined OS_MAC || defined OS_IOS

Timer* Timer::Create(void* owner, ITimerFunction func, uint32_t intervalMs) { return new Timer_impl(owner, func, intervalMs); }

Timer_impl::Timer_impl(void* owner, ITimerFunction func, uint32_t intervalMs)
  : Timer(owner)
  , mTimerFunc(func)
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

Timer* Timer::Create(void* owner, ITimerFunction func, uint32_t intervalMs) { return new Timer_impl(owner, func, intervalMs); }

Timer_impl::Timer_impl(void* owner, ITimerFunction func, uint32_t intervalMs)
  : Timer(owner)
  , mTimerFunc(func)
  , mIntervalMs(intervalMs)

{
  ID = SetTimer(0, reinterpret_cast<UINT_PTR>(this), intervalMs, TimerProc);
}

Timer_impl::~Timer_impl() { Stop(); }

void Timer_impl::Stop()
{
  if (ID)
  {
    KillTimer(0, ID);
    ID = 0;
  }
}

void CALLBACK Timer_impl::TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
  Timer_impl* pTimer = reinterpret_cast<Timer_impl*>(idEvent);

  if (pTimer)
  {
    pTimer->mTimerFunc(*pTimer);
  }
}
#elif defined OS_WEB
Timer* Timer::Create(void* owner, ITimerFunction func, uint32_t intervalMs) { return new Timer_impl(owner, func, intervalMs); }

Timer_impl::Timer_impl(void* owner, ITimerFunction func, uint32_t intervalMs)
  : Timer(owner)
  , mTimerFunc(func)
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
