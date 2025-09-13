/*
 ===============================================================================

 This file is part of the iPlug 2 library.

 See LICENSE.txt for  more info.

 ===============================================================================
*/

#pragma once

// Macros controlling whether certain caches are separated per IGraphics instance
// rather than shared across all instances.

#ifndef IPLUG_SEPARATE_BITMAP_CACHE
  #define IPLUG_SEPARATE_BITMAP_CACHE 0
#endif

#ifndef IPLUG_SEPARATE_SVG_CACHE
  #define IPLUG_SEPARATE_SVG_CACHE 0
#endif

#ifndef IPLUG_SEPARATE_FONT_CACHE_WIN
  #define IPLUG_SEPARATE_FONT_CACHE_WIN 0
#endif

#ifndef IPLUG_SEPARATE_NANOVG_FONT_CACHE
  #define IPLUG_SEPARATE_NANOVG_FONT_CACHE 0
#endif

#ifndef IPLUG_SEPARATE_SKIA_FONT_CACHE
  #define IPLUG_SEPARATE_SKIA_FONT_CACHE 0
#endif

#ifndef IPLUG_SEPARATE_IOS_TEXTURE_CACHE
  #define IPLUG_SEPARATE_IOS_TEXTURE_CACHE 0
#endif

#ifndef IPLUG_SEPARATE_EEL_IMAGE_CACHE
  #define IPLUG_SEPARATE_EEL_IMAGE_CACHE 0
#endif

#ifndef IPLUG_SEPARATE_WIN_WINDOWING
  #define IPLUG_SEPARATE_WIN_WINDOWING 0
#endif

#ifndef IPLUG_SEPARATE_VBLANK
  #define IPLUG_SEPARATE_VBLANK 0
#endif
