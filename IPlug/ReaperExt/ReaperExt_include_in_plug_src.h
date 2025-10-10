
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
#include <algorithm>
#include <cmath>

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

UINT(WINAPI* __GetDpiForWindow)(HWND);

float GetScaleForHWND(HWND hWnd)
{
  if (!__GetDpiForWindow)
  {
    HINSTANCE h = LoadLibraryW(L"user32.dll");
    if (h)
      *(void**)&__GetDpiForWindow = GetProcAddress(h, "GetDpiForWindow");

    if (!__GetDpiForWindow)
      return 1.f;
  }

  const int windowDpi = __GetDpiForWindow(hWnd);
  float dpiScale = 1.f;

  if (windowDpi != USER_DEFAULT_SCREEN_DPI)
    dpiScale = static_cast<float>(windowDpi) / USER_DEFAULT_SCREEN_DPI;

  float virtualizationScale = 1.f;
  HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

  if (hMonitor)
  {
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);

    if (GetMonitorInfoW(hMonitor, &monitorInfo))
    {
      const LONG logicalWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
      const LONG logicalHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;

      if (logicalWidth > 0 && logicalHeight > 0)
      {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);

        if (EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode))
        {
          if (mode.dmPelsWidth > 0 && mode.dmPelsHeight > 0)
          {
            const float widthRatio = static_cast<float>(mode.dmPelsWidth) / static_cast<float>(logicalWidth);
            const float heightRatio = static_cast<float>(mode.dmPelsHeight) / static_cast<float>(logicalHeight);
            const float candidate = std::max(widthRatio, heightRatio);

            if (candidate > 0.f && std::isfinite(candidate))
              virtualizationScale = candidate;
          }
        }
      }
    }
  }

  const float finalScale = dpiScale * virtualizationScale;
  return (finalScale > 0.f && std::isfinite(finalScale)) ? finalScale : 1.f;
}

#endif
