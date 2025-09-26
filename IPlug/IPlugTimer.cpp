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

#include <algorithm>

#if defined OS_WIN
  #include <windows.h>
#endif

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
WDL_PtrList<Timer_impl> Timer_impl::sTimers;

Timer_impl::Timer_impl(ITimerFunction func, uint32_t intervalMs)
: mTimerFunc(func)
, mIntervalMs(intervalMs)

{
  ID = SetTimer(0, 0, intervalMs, TimerProc); //TODO: timer ID correct?

  if (ID)
  {
    WDL_MutexLock lock(&sMutex);
    sTimers.Add(this);
    mNextTick = GetTickCount64() + std::max<uint32_t>(1, mIntervalMs);
  }
}

Timer_impl::~Timer_impl()
{
  Stop();
}

void Timer_impl::Stop()
{
  if (ID)
  {
    KillTimer(0, ID);
    WDL_MutexLock lock(&sMutex);
    sTimers.DeletePtr(this);
    ID = 0;
    mNextTick = 0;
  }
}

void CALLBACK Timer_impl::TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
  WDL_MutexLock lock(&sMutex);
  const uint64_t now = GetTickCount64();

  for (auto i = 0; i < sTimers.GetSize(); i++)
  {
    Timer_impl* pTimer = sTimers.Get(i);

    if (pTimer->ID == idEvent)
    {
      if (now + 1ULL < pTimer->mNextTick)
      {
        return;
      }

      pTimer->mNextTick = now + std::max<uint32_t>(1, pTimer->mIntervalMs);
      pTimer->mTimerFunc(*pTimer);
      return;
    }
  }
}

void Timer_impl::DispatchDueTimers()
{
  WDL_MutexLock lock(&sMutex);

  if (sTimers.GetSize() == 0)
  {
    return;
  }

  const uint64_t now = GetTickCount64();

  for (int i = 0; i < sTimers.GetSize();)
  {
    Timer_impl* pTimer = sTimers.Get(i);

    if (!pTimer || pTimer->ID == 0)
    {
      ++i;
      continue;
    }

    if (now + 1ULL < pTimer->mNextTick)
    {
      ++i;
      continue;
    }

    pTimer->mNextTick = now + std::max<uint32_t>(1, pTimer->mIntervalMs);
    Timer_impl* dispatched = pTimer;
    pTimer->mTimerFunc(*pTimer);

    if (i >= sTimers.GetSize())
    {
      break;
    }

    if (sTimers.Get(i) == dispatched)
    {
      ++i;
    }
    // If the timer removed itself, the next element has shifted into index i, so
    // continue without incrementing to process the new occupant.
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

void Timer::DispatchDueTimers()
{
#if defined OS_WIN
  Timer_impl::DispatchDueTimers();
#else
  // Other platforms deliver timers via their native run loops.
#endif
}
