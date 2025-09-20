/*
 ============================================================================== 

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file IPlugSandboxConfig.h
 * @brief Centralized compile-time configuration for the optional Windows sandbox mode.
 *
 * Downstream projects define @c IPLUG_SANDBOX_ALL (and optional overrides) before
 * including any iPlug headers to control whether plug-in instances share process
 * level state. All toggles are boolean (0 or 1) and default to 0 to preserve
 * legacy behaviour.
 */

// Utility for validating boolean-like macro values.
#define IPLUG_SANDBOX_REQUIRE_BOOL(name)                                                                          \
  static_assert((name) == 0 || (name) == 1, "iPlug sandbox macro '" #name "' must be either 0 or 1")

#if defined(_WIN32)
struct WdlWindowsSandboxContext;
#ifdef __cplusplus
extern "C" {
#endif
void WDL_UTF8_SetSandboxContext(struct WdlWindowsSandboxContext* context);
#ifdef __cplusplus
} // extern "C"
#endif
#endif

#ifndef IPLUG_SANDBOX_ALL
#define IPLUG_SANDBOX_ALL 0
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IPLUG_SANDBOX_ALL);

#ifndef IPLUG_SANDBOX_CORE
#define IPLUG_SANDBOX_CORE IPLUG_SANDBOX_ALL
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IPLUG_SANDBOX_CORE);

#ifndef IPLUG_SANDBOX_DLL_ENTRY
#define IPLUG_SANDBOX_DLL_ENTRY IPLUG_SANDBOX_CORE
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IPLUG_SANDBOX_DLL_ENTRY);

#ifndef IPLUG_SANDBOX_HINSTANCE
#define IPLUG_SANDBOX_HINSTANCE IPLUG_SANDBOX_CORE
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IPLUG_SANDBOX_HINSTANCE);

#ifndef IPLUG_SANDBOX_HOST_CACHE
#define IPLUG_SANDBOX_HOST_CACHE IPLUG_SANDBOX_CORE
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IPLUG_SANDBOX_HOST_CACHE);

#ifndef IPLUG_SANDBOX_VST3
#define IPLUG_SANDBOX_VST3 IPLUG_SANDBOX_ALL
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IPLUG_SANDBOX_VST3);

#ifndef IPLUG_SANDBOX_VST3_FACTORY
#define IPLUG_SANDBOX_VST3_FACTORY IPLUG_SANDBOX_VST3
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IPLUG_SANDBOX_VST3_FACTORY);

#ifndef IPLUG_SANDBOX_VST3_PROCESSOR
#define IPLUG_SANDBOX_VST3_PROCESSOR IPLUG_SANDBOX_VST3
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IPLUG_SANDBOX_VST3_PROCESSOR);

#ifndef IPLUG_SANDBOX_VST3_CONTROLLER
#define IPLUG_SANDBOX_VST3_CONTROLLER IPLUG_SANDBOX_VST3
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IPLUG_SANDBOX_VST3_CONTROLLER);

#ifndef IGRAPHICS_SANDBOX_WIN
#define IGRAPHICS_SANDBOX_WIN IPLUG_SANDBOX_ALL
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_WIN);

#ifndef IGRAPHICS_SANDBOX_WDL_WINDOWS
#define IGRAPHICS_SANDBOX_WDL_WINDOWS IGRAPHICS_SANDBOX_WIN
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_WDL_WINDOWS);

#ifndef IGRAPHICS_SANDBOX_WIN_CLASS
#define IGRAPHICS_SANDBOX_WIN_CLASS IGRAPHICS_SANDBOX_WIN
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_WIN_CLASS);

#ifndef IGRAPHICS_SANDBOX_WIN_TIMERS
#define IGRAPHICS_SANDBOX_WIN_TIMERS IGRAPHICS_SANDBOX_WIN
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_WIN_TIMERS);

#ifndef IGRAPHICS_SANDBOX_WIN_FONTS
#define IGRAPHICS_SANDBOX_WIN_FONTS IGRAPHICS_SANDBOX_WIN
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_WIN_FONTS);

#ifndef IGRAPHICS_SANDBOX_VULKAN
#define IGRAPHICS_SANDBOX_VULKAN IPLUG_SANDBOX_ALL
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_VULKAN);

#ifndef IGRAPHICS_SANDBOX_VK_DEVICE
#define IGRAPHICS_SANDBOX_VK_DEVICE IGRAPHICS_SANDBOX_VULKAN
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_VK_DEVICE);

#ifndef IGRAPHICS_SANDBOX_VK_SWAPCHAIN
#define IGRAPHICS_SANDBOX_VK_SWAPCHAIN IGRAPHICS_SANDBOX_VULKAN
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_VK_SWAPCHAIN);

#ifndef IGRAPHICS_SANDBOX_VK_CONTEXT
#define IGRAPHICS_SANDBOX_VK_CONTEXT IGRAPHICS_SANDBOX_VULKAN
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_VK_CONTEXT);

#ifndef IGRAPHICS_SANDBOX_DRAW
#define IGRAPHICS_SANDBOX_DRAW IPLUG_SANDBOX_ALL
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_DRAW);

#ifndef IGRAPHICS_SANDBOX_TEXTURE_CACHE
#define IGRAPHICS_SANDBOX_TEXTURE_CACHE IGRAPHICS_SANDBOX_DRAW
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_TEXTURE_CACHE);

#ifndef IGRAPHICS_SANDBOX_FONT_FACTORY
#define IGRAPHICS_SANDBOX_FONT_FACTORY IGRAPHICS_SANDBOX_DRAW
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_FONT_FACTORY);

#ifndef IGRAPHICS_SANDBOX_UNICODE_HELPER
#define IGRAPHICS_SANDBOX_UNICODE_HELPER IGRAPHICS_SANDBOX_DRAW
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_UNICODE_HELPER);

#ifndef IGRAPHICS_SANDBOX_LOGGING
#define IGRAPHICS_SANDBOX_LOGGING IPLUG_SANDBOX_ALL
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_LOGGING);

#ifndef IGRAPHICS_SANDBOX_VK_LOGGER
#define IGRAPHICS_SANDBOX_VK_LOGGER IGRAPHICS_SANDBOX_LOGGING
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_VK_LOGGER);

#ifndef IGRAPHICS_SANDBOX_VK_LOG_LEVEL
#define IGRAPHICS_SANDBOX_VK_LOG_LEVEL IGRAPHICS_SANDBOX_LOGGING
#endif
IPLUG_SANDBOX_REQUIRE_BOOL(IGRAPHICS_SANDBOX_VK_LOG_LEVEL);

#undef IPLUG_SANDBOX_REQUIRE_BOOL

// Validate hierarchy propagation so child toggles cannot enable isolation
// when their parent family has been explicitly disabled.
#define IPLUG_SANDBOX_ENSURE_CHILD(child, parent)                                                                    \
  static_assert(!(child) || (parent),                                                                                \
                "iPlug sandbox macro '" #child "' requires parent toggle '" #parent "' to be enabled")

IPLUG_SANDBOX_ENSURE_CHILD(IPLUG_SANDBOX_CORE, IPLUG_SANDBOX_ALL);
IPLUG_SANDBOX_ENSURE_CHILD(IPLUG_SANDBOX_DLL_ENTRY, IPLUG_SANDBOX_CORE);
IPLUG_SANDBOX_ENSURE_CHILD(IPLUG_SANDBOX_HINSTANCE, IPLUG_SANDBOX_CORE);
IPLUG_SANDBOX_ENSURE_CHILD(IPLUG_SANDBOX_HOST_CACHE, IPLUG_SANDBOX_CORE);

IPLUG_SANDBOX_ENSURE_CHILD(IPLUG_SANDBOX_VST3, IPLUG_SANDBOX_ALL);
IPLUG_SANDBOX_ENSURE_CHILD(IPLUG_SANDBOX_VST3_FACTORY, IPLUG_SANDBOX_VST3);
IPLUG_SANDBOX_ENSURE_CHILD(IPLUG_SANDBOX_VST3_PROCESSOR, IPLUG_SANDBOX_VST3);
IPLUG_SANDBOX_ENSURE_CHILD(IPLUG_SANDBOX_VST3_CONTROLLER, IPLUG_SANDBOX_VST3);

IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_WIN, IPLUG_SANDBOX_ALL);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_WDL_WINDOWS, IGRAPHICS_SANDBOX_WIN);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_WIN_CLASS, IGRAPHICS_SANDBOX_WIN);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_WIN_TIMERS, IGRAPHICS_SANDBOX_WIN);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_WIN_FONTS, IGRAPHICS_SANDBOX_WIN);

IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_VULKAN, IPLUG_SANDBOX_ALL);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_VK_DEVICE, IGRAPHICS_SANDBOX_VULKAN);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_VK_SWAPCHAIN, IGRAPHICS_SANDBOX_VULKAN);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_VK_CONTEXT, IGRAPHICS_SANDBOX_VULKAN);

IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_DRAW, IPLUG_SANDBOX_ALL);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_TEXTURE_CACHE, IGRAPHICS_SANDBOX_DRAW);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_FONT_FACTORY, IGRAPHICS_SANDBOX_DRAW);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_UNICODE_HELPER, IGRAPHICS_SANDBOX_DRAW);

IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_LOGGING, IPLUG_SANDBOX_ALL);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_VK_LOGGER, IGRAPHICS_SANDBOX_LOGGING);
IPLUG_SANDBOX_ENSURE_CHILD(IGRAPHICS_SANDBOX_VK_LOG_LEVEL, IGRAPHICS_SANDBOX_LOGGING);

#undef IPLUG_SANDBOX_ENSURE_CHILD

/** Helper macro for evaluating sandbox toggles in preprocessor conditionals. */
#define IPLUG_SANDBOX_ENABLED(flag) (flag)

/** Keyword helper for accessing the sandboxed WDL Windows context pointer. */
#if defined(_WIN32)
  #if IGRAPHICS_SANDBOX_WDL_WINDOWS
namespace iplug
{
namespace sandbox
{
inline WdlWindowsSandboxContext*& SandboxSharedWdlWindowsContext()
{
  thread_local WdlWindowsSandboxContext* context = nullptr;
  return context;
}
} // namespace sandbox
} // namespace iplug

    #define IPLUG_SANDBOX_WDL_WINDOWS_STORAGE thread_local
    #define IPLUG_SANDBOX_WDL_WINDOWS_EXTERN extern thread_local
    #define IPLUG_SANDBOX_WDL_WINDOWS_INIT ::iplug::sandbox::SandboxSharedWdlWindowsContext()
    #define IPLUG_SANDBOX_WDL_WINDOWS_CONTEXT() ::iplug::sandbox::SandboxSharedWdlWindowsContext()
    #define IPLUG_SANDBOX_SET_WDL_WINDOWS_CONTEXT(ctx)                                                              \
      do                                                                                                            \
      {                                                                                                             \
        ::iplug::sandbox::SandboxSharedWdlWindowsContext() = (ctx);                                                 \
        WDL_UTF8_SetSandboxContext(ctx);                                                                            \
      } while (false)
  #else
    #define IPLUG_SANDBOX_WDL_WINDOWS_STORAGE
    #define IPLUG_SANDBOX_WDL_WINDOWS_EXTERN extern
    #define IPLUG_SANDBOX_WDL_WINDOWS_INIT nullptr
    #define IPLUG_SANDBOX_WDL_WINDOWS_CONTEXT() static_cast<WdlWindowsSandboxContext*>(nullptr)
    #define IPLUG_SANDBOX_SET_WDL_WINDOWS_CONTEXT(ctx)                                                              \
      do                                                                                                            \
      {                                                                                                             \
        WDL_UTF8_SetSandboxContext(ctx);                                                                            \
      } while (false)
  #endif
#else
  static_assert(IGRAPHICS_SANDBOX_WDL_WINDOWS == 0,
                "IGRAPHICS_SANDBOX_WDL_WINDOWS is only supported on Windows targets");
  #define IPLUG_SANDBOX_WDL_WINDOWS_STORAGE
  #define IPLUG_SANDBOX_WDL_WINDOWS_EXTERN extern
  #define IPLUG_SANDBOX_WDL_WINDOWS_INIT nullptr
  #define IPLUG_SANDBOX_WDL_WINDOWS_CONTEXT() nullptr
  #define IPLUG_SANDBOX_SET_WDL_WINDOWS_CONTEXT(ctx)                                                                \
    do                                                                                                              \
    {                                                                                                               \
      (void) (ctx);                                                                                                 \
    } while (false)
#endif

/** Keyword helper for declaring sandbox-aware globals tied to the Windows HINSTANCE. */
#if defined(_WIN32)
  #if IPLUG_SANDBOX_HINSTANCE
namespace iplug
{
namespace sandbox
{
inline void*& SandboxSharedHInstance()
{
  static void* handle = nullptr;
  return handle;
}
} // namespace sandbox
} // namespace iplug

    #define IPLUG_SANDBOX_HINSTANCE_STORAGE thread_local
    #define IPLUG_SANDBOX_HINSTANCE_EXTERN extern thread_local
    #define IPLUG_SANDBOX_HINSTANCE_INIT static_cast<HINSTANCE>(::iplug::sandbox::SandboxSharedHInstance())
    #define IPLUG_SANDBOX_SET_HINSTANCE(instance)                                                                     \
      do                                                                                                              \
      {                                                                                                               \
        ::iplug::sandbox::SandboxSharedHInstance() = (instance);                                                      \
        gHINSTANCE = (instance);                                                                                      \
      } while (false)
  #else
    #define IPLUG_SANDBOX_HINSTANCE_STORAGE
    #define IPLUG_SANDBOX_HINSTANCE_EXTERN extern
    #define IPLUG_SANDBOX_HINSTANCE_INIT 0
    #define IPLUG_SANDBOX_SET_HINSTANCE(instance)                                                                     \
      do                                                                                                              \
      {                                                                                                               \
        gHINSTANCE = (instance);                                                                                      \
      } while (false)
  #endif
#else
  static_assert(IPLUG_SANDBOX_HINSTANCE == 0, "IPLUG_SANDBOX_HINSTANCE is only supported on Windows targets");
  #define IPLUG_SANDBOX_HINSTANCE_STORAGE
  #define IPLUG_SANDBOX_HINSTANCE_EXTERN extern
  #define IPLUG_SANDBOX_HINSTANCE_INIT 0
  #define IPLUG_SANDBOX_SET_HINSTANCE(instance)                                                                       \
    do                                                                                                                \
    {                                                                                                                 \
      (void) (instance);                                                                                              \
    } while (false)
#endif

#if defined(_MSC_VER)
  #if (IPLUG_SANDBOX_CORE != IPLUG_SANDBOX_ALL) || (IPLUG_SANDBOX_VST3 != IPLUG_SANDBOX_ALL) ||                             \
      (IGRAPHICS_SANDBOX_WIN != IPLUG_SANDBOX_ALL) || (IGRAPHICS_SANDBOX_WDL_WINDOWS != IPLUG_SANDBOX_ALL) ||               \
      (IGRAPHICS_SANDBOX_VULKAN != IPLUG_SANDBOX_ALL) || (IGRAPHICS_SANDBOX_DRAW != IPLUG_SANDBOX_ALL) ||                  \
      (IGRAPHICS_SANDBOX_LOGGING != IPLUG_SANDBOX_ALL)
    #pragma message("iPlug2 sandbox: partial sandbox configuration detected (Windows build)")
  #elif IPLUG_SANDBOX_ALL
    #pragma message("iPlug2 sandbox: full sandbox configuration enabled (Windows build)")
  #endif
#endif

