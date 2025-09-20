#pragma once

// Wrapper that makes the optional WDL sandbox context available to iPlug code
// while providing graceful fallbacks when a downstream project is still using
// an older copy of WDL that predates the sandbox helpers.

#if defined(IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT)
  #undef IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT
#endif

#if defined(_WIN32)
  #if defined(IPLUG_SANDBOX_LINK_WDL_HELPERS) && !IPLUG_SANDBOX_LINK_WDL_HELPERS
    #define IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT 0
  #elif defined(__has_include)
    #if __has_include("../../WDL/WdlWindowsSandboxContext.h")
      #include "../../WDL/WdlWindowsSandboxContext.h"
      #define IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT 1
    #else
      #define IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT 0
    #endif
  #else
    #include "../../WDL/WdlWindowsSandboxContext.h"
    #define IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT 1
  #endif
#else
  #define IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT 0
#endif

#if !defined(IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT)
  #define IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT 0
#endif

#if !IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT

#if defined(_WIN32)
  #include <windows.h>
#endif

#include <stddef.h>

struct WdlWindowsSandboxContext
{
};

static inline void WdlWindowsSandboxContext_Init(WdlWindowsSandboxContext*) {}

static inline void WdlWindowsSandboxContext_Reset(WdlWindowsSandboxContext*) {}

static inline void WdlWindowsSandboxContext_Destroy(WdlWindowsSandboxContext*) {}

static inline int WdlWindowsSandboxContext_FormatUtf8PropertyPrefix(const WdlWindowsSandboxContext*,
                                                                    const char*,
                                                                    char*,
                                                                    size_t)
{
  return 0;
}

static inline int WdlWindowsSandboxContext_SetUtf8PropertyPrefix(WdlWindowsSandboxContext*,
                                                                 const char*)
{
  return 0;
}

#if defined(_WIN32)
static inline bool WDL_ChooseFileForSaveCtx(WdlWindowsSandboxContext* context,
                                            HWND parent,
                                            const char* text,
                                            const char* initialdir,
                                            const char* initialfile,
                                            const char* extlist,
                                            const char* defext,
                                            bool preservecwd,
                                            char* fn,
                                            int fnsize,
                                            const char* dlgid,
                                            void* dlgProc,
                                            HINSTANCE hInstance)
{
  (void) context;
  (void) parent;
  (void) text;
  (void) initialdir;
  (void) initialfile;
  (void) extlist;
  (void) defext;
  (void) preservecwd;
  (void) fn;
  (void) fnsize;
  (void) dlgid;
  (void) dlgProc;
  (void) hInstance;
  return false;
}

static inline char* WDL_ChooseFileForOpen2Ctx(WdlWindowsSandboxContext* context,
                                              HWND parent,
                                              const char* text,
                                              const char* initialdir,
                                              const char* initialfile,
                                              const char* extlist,
                                              const char* defext,
                                              bool preservecwd,
                                              int allowmul,
                                              const char* dlgid,
                                              void* dlgProc,
                                              HINSTANCE hInstance)
{
  (void) context;
  (void) parent;
  (void) text;
  (void) initialdir;
  (void) initialfile;
  (void) extlist;
  (void) defext;
  (void) preservecwd;
  (void) allowmul;
  (void) dlgid;
  (void) dlgProc;
  (void) hInstance;
  return NULL;
}
#endif

// The UTF-8 helper entry points live in WDL's win32_utf8 translation unit and
// use C linkage. Provide stubs that match the linkage so downstream projects
// can include win32_utf8.h without having to link the implementation when the
// helpers are unavailable.
#if defined(__cplusplus)
extern "C"
{
#endif

static inline void WDL_UTF8_SetSandboxContext(struct WdlWindowsSandboxContext*) {}

static inline struct WdlWindowsSandboxContext* WDL_UTF8_GetSandboxContext(void)
{
  return NULL;
}

static inline const char* WDL_UTF8_SandboxContextPropertyName(void)
{
  return NULL;
}

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // !IPLUG_HAS_WDL_WINDOWS_SANDBOX_CONTEXT

#if defined(_WIN32)
  #include <windows.h>

extern "C"
{
bool WDL_ChooseFileForSave(HWND parent,
                           const char* text,
                           const char* initialdir,
                           const char* initialfile,
                           const char* extlist,
                           const char* defext,
                           bool preservecwd,
                           char* fn,
                           int fnsize,
                           const char* dlgid = NULL,
                           void* dlgProc = NULL,
                           HINSTANCE hInstance = NULL);

char* WDL_ChooseFileForOpen2(HWND parent,
                             const char* text,
                             const char* initialdir,
                             const char* initialfile,
                             const char* extlist,
                             const char* defext,
                             bool preservecwd,
                             int allowmul,
                             const char* dlgid = NULL,
                             void* dlgProc = NULL,
                             HINSTANCE hInstance = NULL);
}

#if defined(__cplusplus)
extern "C"
{
#endif

#if defined(__cplusplus)
bool IPlugSandboxFallback_ChooseFileForSaveCtx(WdlWindowsSandboxContext*,
                                              HWND parent,
                                              const char* text,
                                              const char* initialdir,
                                              const char* initialfile,
                                              const char* extlist,
                                              const char* defext,
                                              bool preservecwd,
                                              char* fn,
                                              int fnsize,
                                              const char* dlgid = NULL,
                                              void* dlgProc = NULL,
                                              HINSTANCE hInstance = NULL);

char* IPlugSandboxFallback_ChooseFileForOpen2Ctx(WdlWindowsSandboxContext*,
                                                HWND parent,
                                                const char* text,
                                                const char* initialdir,
                                                const char* initialfile,
                                                const char* extlist,
                                                const char* defext,
                                                bool preservecwd,
                                                int allowmul,
                                                const char* dlgid = NULL,
                                                void* dlgProc = NULL,
                                                HINSTANCE hInstance = NULL);
#else
bool IPlugSandboxFallback_ChooseFileForSaveCtx(WdlWindowsSandboxContext*,
                                              HWND parent,
                                              const char* text,
                                              const char* initialdir,
                                              const char* initialfile,
                                              const char* extlist,
                                              const char* defext,
                                              bool preservecwd,
                                              char* fn,
                                              int fnsize,
                                              const char* dlgid,
                                              void* dlgProc,
                                              HINSTANCE hInstance);

char* IPlugSandboxFallback_ChooseFileForOpen2Ctx(WdlWindowsSandboxContext*,
                                                HWND parent,
                                                const char* text,
                                                const char* initialdir,
                                                const char* initialfile,
                                                const char* extlist,
                                                const char* defext,
                                                bool preservecwd,
                                                int allowmul,
                                                const char* dlgid,
                                                void* dlgProc,
                                                HINSTANCE hInstance);
#endif

void IPlugSandboxFallback_SetUtf8SandboxContext(struct WdlWindowsSandboxContext* context);

struct WdlWindowsSandboxContext* IPlugSandboxFallback_GetUtf8SandboxContext(void);

const char* IPlugSandboxFallback_SandboxContextPropertyName(void);

#if defined(__cplusplus)
} // extern "C"
#endif

  #if !IPLUG_SANDBOX_LINK_WDL_HELPERS
    #define WDL_ChooseFileForSaveCtx IPlugSandboxFallback_ChooseFileForSaveCtx
    #define WDL_ChooseFileForOpen2Ctx IPlugSandboxFallback_ChooseFileForOpen2Ctx
    #define WDL_UTF8_SetSandboxContext IPlugSandboxFallback_SetUtf8SandboxContext
    #define WDL_UTF8_GetSandboxContext IPlugSandboxFallback_GetUtf8SandboxContext
    #define WDL_UTF8_SandboxContextPropertyName IPlugSandboxFallback_SandboxContextPropertyName
  #endif
#endif

