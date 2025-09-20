#include "wdltypes.h"

#ifdef _WIN32
  #if !defined(WDL_WIN32_HIDPI_HAS_SANDBOX_CONTEXT)
    #define WDL_WIN32_HIDPI_HAS_SANDBOX_CONTEXT 0
    #if defined(__has_include)
      #if __has_include("WdlWindowsSandboxContext.h")
        #include "WdlWindowsSandboxContext.h"
        #undef WDL_WIN32_HIDPI_HAS_SANDBOX_CONTEXT
        #define WDL_WIN32_HIDPI_HAS_SANDBOX_CONTEXT 1
      #endif
    #else
      #include "WdlWindowsSandboxContext.h"
      #undef WDL_WIN32_HIDPI_HAS_SANDBOX_CONTEXT
      #define WDL_WIN32_HIDPI_HAS_SANDBOX_CONTEXT 1
    #endif
  #endif
  #if !defined(WDL_WIN32_HIDPI_SANDBOX_DECLARED)
    #define WDL_WIN32_HIDPI_SANDBOX_DECLARED
    struct WdlWindowsSandboxContext;
    #ifdef __cplusplus
    extern "C" {
    #endif
    struct WdlWindowsSandboxContext* WDL_UTF8_GetSandboxContext(void);
    #ifdef __cplusplus
    }
    #endif
  #endif
#endif

#ifndef WDL_WIN32_HIDPI_HAS_SANDBOX_CONTEXT
#define WDL_WIN32_HIDPI_HAS_SANDBOX_CONTEXT 0
#endif

#ifdef _WIN32
static inline FARPROC* wdl_win32_hidpi_awareness_slot(WdlWindowsSandboxContext* context)
{
#if WDL_WIN32_HIDPI_HAS_SANDBOX_CONTEXT
  if (context)
  {
    return &context->dpi.set_thread_awareness;
  }
#endif
  static FARPROC g_wdl_win32_hidpi_awareness = NULL;
  return &g_wdl_win32_hidpi_awareness;
}

static inline char* wdl_win32_hidpi_mm_state_slot(WdlWindowsSandboxContext* context)
{
#if WDL_WIN32_HIDPI_HAS_SANDBOX_CONTEXT
  if (context)
  {
    return &context->dpi.mm_setwindowpos_state;
  }
#endif
  static char g_wdl_win32_hidpi_mm_state = 0;
  return &g_wdl_win32_hidpi_mm_state;
}
#endif

#ifdef _WIN32
  #ifndef SWP__NOMOVETHENSIZE
  #define SWP__NOMOVETHENSIZE (1<<30)
  #endif

#ifdef WDL_WIN32_HIDPI_IMPL
#ifndef _WDL_WIN32_HIDPI_H_IMPL
#define _WDL_WIN32_HIDPI_H_IMPL

WDL_WIN32_HIDPI_IMPL void *_WDL_dpi_func(void *p)
{
#if defined(WDL_NO_SUPPORT_UTF8)
  WdlWindowsSandboxContext* context = NULL;
#else
  WdlWindowsSandboxContext* context = WDL_UTF8_GetSandboxContext();
#endif
  FARPROC* slot = wdl_win32_hidpi_awareness_slot(context);
  FARPROC stored = slot ? *slot : NULL;

  if (!stored)
  {
    HINSTANCE huser32 = LoadLibrary("user32.dll");
    if (huser32)
    {
      stored = GetProcAddress(huser32,"SetThreadDpiAwarenessContext");
    }
    if (!stored)
    {
      stored = (FARPROC) (UINT_PTR) 1;
    }
    if (slot)
    {
      *slot = stored;
    }
  }

  if ((UINT_PTR) stored > 1)
  {
    void *(WINAPI *set_thread_dpi)(void *) = (void *(WINAPI *)(void *)) stored;
    return set_thread_dpi(p);
  }
  return NULL;
}

WDL_WIN32_HIDPI_IMPL void WDL_mmSetWindowPos(HWND hwnd, HWND hwndAfter, int x, int y, int w, int h, UINT f)
{
#ifdef SetWindowPos
#undef SetWindowPos
#endif
  WDL_ASSERT((f & SWP_NOSIZE) || w>=0);
  WDL_ASSERT((f & SWP_NOSIZE) || h>=0);

#if defined(WDL_NO_SUPPORT_UTF8)
  WdlWindowsSandboxContext* context = NULL;
#else
  WdlWindowsSandboxContext* context = WDL_UTF8_GetSandboxContext();
#endif
  char* init_slot = wdl_win32_hidpi_mm_state_slot(context);
  char init = init_slot ? *init_slot : 0;

  if (!init)
  {
    char computed_init = 1;
    HINSTANCE h = GetModuleHandle("user32.dll");
    if (h)
    {
      BOOL (WINAPI *__AreDpiAwarenessContextsEqual)(void *, void *);
      void * (WINAPI *__GetThreadDpiAwarenessContext )();
      *(void **)&__GetThreadDpiAwarenessContext = GetProcAddress(h,"GetThreadDpiAwarenessContext");
      *(void **)&__AreDpiAwarenessContextsEqual = GetProcAddress(h,"AreDpiAwarenessContextsEqual");
      if (__GetThreadDpiAwarenessContext && __AreDpiAwarenessContextsEqual)
      {
        if (__AreDpiAwarenessContextsEqual(__GetThreadDpiAwarenessContext(),(void*)(INT_PTR)-4))
          computed_init = 2;
      }
    }
    init = computed_init;
    if (init_slot)
    {
      *init_slot = init;
    }
  }

  if (WDL_NOT_NORMALLY(hwnd == NULL)) return;

  if (init == 2 &&
    !(f&(SWP_NOMOVE|SWP_NOSIZE|SWP__NOMOVETHENSIZE|SWP_ASYNCWINDOWPOS)) &&
    !(GetWindowLong(hwnd,GWL_STYLE)&WS_CHILD))
  {
    SetWindowPos(hwnd,NULL,x,y,0,0,SWP_NOREDRAW|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_DEFERERASE);
    f |= SWP_NOMOVE;
  }
  SetWindowPos(hwnd,hwndAfter,x,y,w,h,f&~SWP__NOMOVETHENSIZE);
#define SetWindowPos WDL_mmSetWindowPos
}


#endif // end of _WDL_WIN32_HIDPI_H_IMPL
#else // if !WDL_WIN32_HIDPI_IMPL
void WDL_mmSetWindowPos(HWND hwnd, HWND hwndAfter, int x, int y, int w, int h, UINT f);
void *_WDL_dpi_func(void *p);
#ifdef SetWindowPos
#undef SetWindowPos
#endif
#define SetWindowPos WDL_mmSetWindowPos
#endif

#else // !_WIN32
  #ifndef SWP__NOMOVETHENSIZE
  #define SWP__NOMOVETHENSIZE 0
  #endif
#endif

#ifndef _WDL_WIN32_HIDPI_H_
#define _WDL_WIN32_HIDPI_H_

static WDL_STATICFUNC_UNUSED void *WDL_dpi_enter_aware(int mode) // -1 = DPI_AWARENESS_CONTEXT_UNAWARE, -2=aware, -3=mm aware, -4=mmaware2, -5=gdiscaled
{
#ifdef _WIN32
  return _WDL_dpi_func((void *)(INT_PTR)mode);
#else
  return NULL;
#endif
}
static WDL_STATICFUNC_UNUSED void WDL_dpi_leave_aware(void **p)
{
#ifdef _WIN32
  if (p)
  {
    if (*p) _WDL_dpi_func(*p);
    *p = NULL;
  }
#endif
}

#ifdef __cplusplus
struct WDL_dpi_aware_scope {
#ifdef _WIN32
  WDL_dpi_aware_scope(int mode=-1) { c = mode ? WDL_dpi_enter_aware(mode) : NULL; }
  ~WDL_dpi_aware_scope() { if (c) WDL_dpi_leave_aware(&c); }
  void *c;
#else
  WDL_dpi_aware_scope() { }
#endif
};
#endif

#endif
