#pragma once

#include "../mutex.h"

#ifndef IPLUG_SEPARATE_SWELL_TIMER_QUEUE
#define IPLUG_SEPARATE_SWELL_TIMER_QUEUE 0
#endif

struct TimerInfoRec;

#if IPLUG_SEPARATE_SWELL_TIMER_QUEUE
struct SWELL_TimerQueue
{
  TimerInfoRec* timer_list = nullptr;
  TimerInfoRec* spare_timers = nullptr;
  WDL_Mutex timermutex;
  bool pmq_init = false;
  WDL_Mutex pmq_mutex;
};

SWELL_TimerQueue* SWELL_SetTimerQueue(SWELL_TimerQueue* q);
SWELL_TimerQueue* SWELL_GetTimerQueue();
#endif

