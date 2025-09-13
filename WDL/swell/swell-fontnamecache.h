#pragma once

#include "../mutex.h"
#include "../assocarray.h"

#ifndef IPLUG_SEPARATE_SWELL_FONTNAME_CACHE
#define IPLUG_SEPARATE_SWELL_FONTNAME_CACHE 0
#endif

struct NSString;

#if IPLUG_SEPARATE_SWELL_FONTNAME_CACHE
struct SWELL_FontNameCache
{
  SWELL_FontNameCache();
  WDL_Mutex mutex;
  WDL_StringKeyedArray<NSString*> cache;
};

SWELL_FontNameCache* SWELL_SetFontNameCache(SWELL_FontNameCache* c);
SWELL_FontNameCache* SWELL_GetFontNameCache();
#endif
