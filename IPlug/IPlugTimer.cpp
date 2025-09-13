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

Timer* Timer::Create(ITimerFunction func, uint32_t intervalMs
#if IPLUG_SEPARATE_TIMER_MANAGER
                     , TimerManager* pTimerManager
#endif
                     )
{
  return new Timer_impl(func, intervalMs
#if IPLUG_SEPARATE_TIMER_MANAGER
                        , pTimerManager
#endif
                        );
}

#if IPLUG_SEPARATE_TIMER_MANAGER
WDL_Mutex TimerManager::sMgrMutex;
WDL_PtrList<TimerManager> TimerManager::sManagers;

TimerManager::TimerManager()
{
  WDL_MutexLock lock(&sMgrMutex);
  sManagers.Add(this);
}

TimerManager::~TimerManager()
{
  WDL_MutexLock lock(&sMgrMutex);
  sManagers.DeletePtr(this);
}

TimerManager& TimerManager::Global()
{
  static TimerManager gGlobal;
  return gGlobal;
}

Timer_impl* TimerManager::FindTimer(UINT_PTR id)
{
  WDL_MutexLock mgrLock(&sMgrMutex);

  for (auto m = 0; m < sManagers.GetSize(); m++)
  {
    TimerManager* pMgr = sManagers.Get(m);
    WDL_MutexLock lock(&pMgr->mMutex);

    for (auto i = 0; i < pMgr->mTimers.GetSize(); i++)
    {
      Timer_impl* pTimer = pMgr->mTimers.Get(i);

      if (pTimer->ID == id)
        return pTimer;
    }
  }

  return nullptr;
}
#else
WDL_Mutex Timer_impl::sMutex;
WDL_PtrList<Timer_impl> Timer_impl::sTimers;
#endif

Timer_impl::Timer_impl(ITimerFunction func, uint32_t intervalMs
#if IPLUG_SEPARATE_TIMER_MANAGER
                       , TimerManager* pTimerManager
#endif
                       )
: mTimerFunc(func)
, mIntervalMs(intervalMs)
#if IPLUG_SEPARATE_TIMER_MANAGER
, mManager(pTimerManager ? pTimerManager : &TimerManager::Global())
#endif
{
  ID = SetTimer(0, 0, intervalMs, TimerProc); //TODO: timer ID correct?

  if (ID)
  {
#if IPLUG_SEPARATE_TIMER_MANAGER
    WDL_MutexLock lock(&mManager->mMutex);
    mManager->mTimers.Add(this);
#else
    WDL_MutexLock lock(&sMutex);
    sTimers.Add(this);
#endif
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
#if IPLUG_SEPARATE_TIMER_MANAGER
    WDL_MutexLock lock(&mManager->mMutex);
    mManager->mTimers.DeletePtr(this);
#else
    WDL_MutexLock lock(&sMutex);
    sTimers.DeletePtr(this);
#endif
    ID = 0;
  }
}

void CALLBACK Timer_impl::TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
#if IPLUG_SEPARATE_TIMER_MANAGER
  if (Timer_impl* pTimer = TimerManager::FindTimer(idEvent))
    pTimer->mTimerFunc(*pTimer);
#else
  WDL_MutexLock lock(&sMutex);

  for (auto i = 0; i < sTimers.GetSize(); i++)
  {
    Timer_impl* pTimer = sTimers.Get(i);

    if (pTimer->ID == idEvent)
    {
      pTimer->mTimerFunc(*pTimer);
      return;
    }
  }
#endif
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
