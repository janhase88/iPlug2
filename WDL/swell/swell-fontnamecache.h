#pragma once

#include "../mutex.h"
#include "../assocarray.h"

#include "IPlug/InstanceSeparation.h"

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
