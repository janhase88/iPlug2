#pragma once

/*
 ===============================================================================

  This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

  See LICENSE.txt for  more info.

 ===============================================================================
*/

/**
 * @file
 * @brief Ensures SkTypeface_win.h can be included without LOGFONT collisions.
 *
 * Skia's SkTypeface_win.h unconditionally forward declares the Windows LOGFONT
 * types. If <windows.h> has already been included, those typedefs collide with
 * the real Windows definitions. Include this wrapper instead of the Skia header
 * directly so the include order and temporary remapping are handled in one
 * place.
 */

#if !defined(OS_WIN) && !defined(_WIN32)
  #error "SkTypefaceWinWrapper.h should only be included when building for Windows."
#endif

#if defined(SkTypeface_win_DEFINED)
  #error "SkTypeface_win.h was included before SkTypefaceWinWrapper.h. Include the wrapper first."
#endif

#define IGRAPHICS_SKIA_USING_SKTYPEFACE_WIN_WRAPPER 1

#include <windows.h>

#define LOGFONT  IGRAPHICS_SKIA_WRAPPER_LOGFONT
#define LOGFONTW IGRAPHICS_SKIA_WRAPPER_LOGFONTW
#define LOGFONTA IGRAPHICS_SKIA_WRAPPER_LOGFONTA

#include "include/ports/SkTypeface_win.h"

#undef LOGFONTA
#undef LOGFONTW
#undef LOGFONT

