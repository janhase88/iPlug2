
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

namespace
{
  using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
  using GetDpiForMonitorFn = HRESULT (WINAPI*)(HMONITOR, UINT, UINT*, UINT*);
  using GetScaleFactorForMonitorFn = HRESULT (WINAPI*)(HMONITOR, UINT*);

  GetDpiForWindowFn LoadGetDpiForWindow()
  {
    static GetDpiForWindowFn sFunc = []() -> GetDpiForWindowFn {
      HMODULE user32 = GetModuleHandleW(L"user32.dll");
      if (!user32)
        user32 = LoadLibraryW(L"user32.dll");
      if (!user32)
        return nullptr;
      return reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
    }();
    return sFunc;
  }

  GetDpiForMonitorFn LoadGetDpiForMonitor()
  {
    static GetDpiForMonitorFn sFunc = []() -> GetDpiForMonitorFn {
      HMODULE shcore = GetModuleHandleW(L"shcore.dll");
      if (!shcore)
        shcore = LoadLibraryW(L"shcore.dll");
      if (!shcore)
        return nullptr;
      return reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"));
    }();
    return sFunc;
  }

  GetScaleFactorForMonitorFn LoadGetScaleFactorForMonitor()
  {
    static GetScaleFactorForMonitorFn sFunc = []() -> GetScaleFactorForMonitorFn {
      HMODULE shcore = GetModuleHandleW(L"shcore.dll");
      if (!shcore)
        shcore = LoadLibraryW(L"shcore.dll");
      if (!shcore)
        return nullptr;
      return reinterpret_cast<GetScaleFactorForMonitorFn>(GetProcAddress(shcore, "GetScaleFactorForMonitor"));
    }();
    return sFunc;
  }
}

float GetScaleForHWND(HWND hWnd)
{
  if (!hWnd)
    return 1.f;

  float scale = 1.f;
  UINT dpi = 0;

  if (auto getDpiForWindow = LoadGetDpiForWindow())
  {
    dpi = getDpiForWindow(hWnd);
    if (dpi > 0 && dpi != USER_DEFAULT_SCREEN_DPI)
      return static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI;
  }

  HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
  if (monitor)
  {
    if (auto getDpiForMonitor = LoadGetDpiForMonitor())
    {
      UINT dpiX = 0;
      UINT dpiY = 0;
      if (SUCCEEDED(getDpiForMonitor(monitor, /*MDT_EFFECTIVE_DPI*/ 0, &dpiX, &dpiY)) && dpiX > 0)
        return static_cast<float>(dpiX) / USER_DEFAULT_SCREEN_DPI;
    }

    if (auto getScaleFactorForMonitor = LoadGetScaleFactorForMonitor())
    {
      UINT scalePercent = 0;
      if (SUCCEEDED(getScaleFactorForMonitor(monitor, &scalePercent)) && scalePercent >= 100)
        return static_cast<float>(scalePercent) / 100.f;
    }
  }

  if (dpi == 0)
  {
    HDC dc = GetDC(hWnd);
    HWND releaseWnd = hWnd;
    if (!dc)
    {
      dc = GetDC(nullptr);
      releaseWnd = nullptr;
    }
    if (dc)
    {
      dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
      ReleaseDC(releaseWnd, dc);
    }
  }

  if (dpi == 0)
    return scale;

  scale = static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI;
  if (scale <= 0.f)
    scale = 1.f;
  return scale;
}

#endif
