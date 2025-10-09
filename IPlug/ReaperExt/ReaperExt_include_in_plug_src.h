
#ifndef NO_IGRAPHICS
#define BUNDLE_ID ""
#define APP_GROUP_ID ""
#include "IGraphics_include_in_plug_src.h"
#endif

#define REAPERAPI_IMPLEMENT
void (*AttachWindowTopmostButton)(HWND hwnd);
#include "reaper_plugin_functions.h"

#include "resource.h"
#include <vector>
#include <map>

REAPER_PLUGIN_HINSTANCE gHINSTANCE;
HWND gParent;
HWND gHWND = NULL;
std::unique_ptr<PLUG_CLASS_NAME> gPlug;
RECT gPrevBounds;
int gErrorCount = 0;

/** Helper struct for registering Reaper Actions */
struct ReaperAction
{
  int* pToggle = nullptr;
  gaccel_register_t accel = {{0,0,0}, ""};
  std::function<void()> func;
  bool addMenuItem = false;
};

std::vector<ReaperAction> gActions;

//TODO: don't #include cpp here
#include "ReaperExtBase.cpp"

// super nasty looking macro here but allows importing functions from Reaper with simple looking code
#define IMPAPI(x) if (!((*((void **)&(x)) = (void*) pRec->GetFunc(#x)))) gErrorCount++;

#pragma mark - ENTRY POINT
extern "C"
{
  REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* pRec)
  {
    gHINSTANCE = hInstance;
    
    if (pRec)
    {
      if (pRec->caller_version != REAPER_PLUGIN_VERSION || !pRec->GetFunc)
        return 0;
      
      gPlug = std::make_unique<PLUG_CLASS_NAME>(pRec);

      // initialize API function pointers from Reaper
      IMPAPI(Main_OnCommand);
      IMPAPI(GetResourcePath);
      IMPAPI(AddExtensionsMainMenu);
      IMPAPI(AttachWindowTopmostButton);
      IMPAPI(ShowConsoleMsg);
      IMPAPI(DockWindowAdd);
      IMPAPI(DockWindowActivate);
      
      if (gErrorCount > 0)
        return 0;
      
      pRec->Register("hookcommand", (void*) ReaperExtBase::HookCommandProc);
      pRec->Register("toggleaction", (void*) ReaperExtBase::ToggleActionCallback);
      
      AddExtensionsMainMenu();
      
      gParent = pRec->hwnd_main;
      
      HMENU hMenu = GetSubMenu(GetMenu(gParent),
#ifdef OS_WIN
                               8
#else // OS X has one extra menu
                               9
#endif
                               );
      
      int menuIdx = 6;
      
      for(auto& action : gActions)
      {
        if(action.addMenuItem)
        {
          MENUITEMINFO mi={sizeof(MENUITEMINFO),};
          mi.fMask = MIIM_TYPE | MIIM_ID;
          mi.fType = MFT_STRING;
          mi.dwTypeData = LPSTR(action.accel.desc);
          mi.wID = action.accel.accel.cmd;
          InsertMenuItem(hMenu, menuIdx++, TRUE, &mi);
        }
      }

      return 1;
    }
    else
    {
      return 0;
    }
  }
};


#ifndef OS_WIN
#define SWELL_DLG_FLAGS_AUTOGEN SWELL_DLG_WS_FLIPPED//|SWELL_DLG_WS_RESIZABLE
#include "swell-dlggen.h"
#include "main.rc_mac_dlg"
#undef BEGIN
#undef END
#include "swell-menugen.h"
#include "main.rc_mac_menu"
float iplug::GetScaleForHWND(HWND hWnd)
{
  return 1.f;
}
#else

#include <cstdint>

UINT (WINAPI* __GetDpiForWindow)(HWND);
HRESULT (WINAPI* __GetDpiForMonitor)(HMONITOR, unsigned int, unsigned int*, unsigned int*);
void* (WINAPI* __SetThreadDpiAwarenessContext)(void*);
bool __TriedLoadSetThreadDpiAwarenessContext = false;
bool __TriedLoadGetDpiForMonitor = false;

#ifndef MDT_EFFECTIVE_DPI
enum
{
  MDT_EFFECTIVE_DPI = 0
};
#endif

float GetScaleForHWND(HWND hWnd)
{
  if (!__GetDpiForWindow || (!__SetThreadDpiAwarenessContext && !__TriedLoadSetThreadDpiAwarenessContext))
  {
    HINSTANCE h = LoadLibraryA("user32.dll");
    if (h)
    {
      if (!__GetDpiForWindow)
        *(void**)&__GetDpiForWindow = GetProcAddress(h, "GetDpiForWindow");
      if (!__SetThreadDpiAwarenessContext && !__TriedLoadSetThreadDpiAwarenessContext)
        *(void**)&__SetThreadDpiAwarenessContext = GetProcAddress(h, "SetThreadDpiAwarenessContext");
    }

    if (!__TriedLoadSetThreadDpiAwarenessContext)
      __TriedLoadSetThreadDpiAwarenessContext = true;
  }

  void* previousContext = nullptr;
  if (__SetThreadDpiAwarenessContext)
    previousContext = __SetThreadDpiAwarenessContext(reinterpret_cast<void*>(static_cast<std::intptr_t>(-4)));

  unsigned int dpi = 0;
  if (__GetDpiForWindow)
    dpi = __GetDpiForWindow(hWnd);

  if (__SetThreadDpiAwarenessContext && previousContext)
    __SetThreadDpiAwarenessContext(previousContext);

  if ((!dpi || dpi == USER_DEFAULT_SCREEN_DPI) && !__GetDpiForMonitor && !__TriedLoadGetDpiForMonitor)
  {
    HINSTANCE h = LoadLibraryA("shcore.dll");
    if (h)
      *(void**)&__GetDpiForMonitor = GetProcAddress(h, "GetDpiForMonitor");
    __TriedLoadGetDpiForMonitor = true;
  }

  if (__GetDpiForMonitor && (!dpi || dpi == USER_DEFAULT_SCREEN_DPI))
  {
    if (HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST))
    {
      unsigned int dpiX = USER_DEFAULT_SCREEN_DPI;
      unsigned int dpiY = USER_DEFAULT_SCREEN_DPI;
      if (SUCCEEDED(__GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX)
        dpi = dpiX;
    }
  }

  if (dpi && dpi != USER_DEFAULT_SCREEN_DPI)
    return static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI;

  return 1;
}

#endif
