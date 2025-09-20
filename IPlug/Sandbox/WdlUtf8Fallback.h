#pragma once

#if !defined(_WIN32)
#  error "WdlUtf8Fallback.h requires a Windows build"
#endif

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

#if defined(_MSC_VER)
  #define WDL_WIN32_UTF8_FALLBACK_IMPL static __inline
#else
  #define WDL_WIN32_UTF8_FALLBACK_IMPL static inline
#endif

WDL_WIN32_UTF8_FALLBACK_IMPL void WDL_Fallback_UTF8_SetSandboxContext(struct WdlWindowsSandboxContext* context)
{
  (void) context;
}

WDL_WIN32_UTF8_FALLBACK_IMPL struct WdlWindowsSandboxContext* WDL_Fallback_UTF8_GetSandboxContext(void)
{
  return NULL;
}

WDL_WIN32_UTF8_FALLBACK_IMPL const char* WDL_Fallback_UTF8_SandboxContextPropertyName(void)
{
  return NULL;
}

WDL_WIN32_UTF8_FALLBACK_IMPL DWORD WDL_Fallback_GetCurrentDirectoryUTF8(DWORD bufferLength, LPTSTR buffer)
{
  return GetCurrentDirectoryA(bufferLength, (LPSTR) buffer);
}

WDL_WIN32_UTF8_FALLBACK_IMPL BOOL WDL_Fallback_SetCurrentDirectoryUTF8(LPCTSTR path)
{
  return SetCurrentDirectoryA((LPCSTR) path);
}

WDL_WIN32_UTF8_FALLBACK_IMPL BOOL WDL_Fallback_GetOpenFileNameUTF8(LPOPENFILENAME ofn)
{
  return GetOpenFileNameA((LPOPENFILENAMEA) ofn);
}

WDL_WIN32_UTF8_FALLBACK_IMPL BOOL WDL_Fallback_GetSaveFileNameUTF8(LPOPENFILENAME ofn)
{
  return GetSaveFileNameA((LPOPENFILENAMEA) ofn);
}

WDL_WIN32_UTF8_FALLBACK_IMPL HINSTANCE WDL_Fallback_LoadLibraryUTF8(LPCTSTR path)
{
  return LoadLibraryA((LPCSTR) path);
}

WDL_WIN32_UTF8_FALLBACK_IMPL struct _ITEMIDLIST* WDL_Fallback_SHBrowseForFolderUTF8(struct _browseinfoA* info)
{
  return SHBrowseForFolderA((LPBROWSEINFOA) info);
}

#if defined(_WIN64)
WDL_WIN32_UTF8_FALLBACK_IMPL BOOL WDL_Fallback_SHGetPathFromIDListUTF8(const struct _ITEMIDLIST __unaligned* pidl, LPSTR path, int pathLength)
#else
WDL_WIN32_UTF8_FALLBACK_IMPL BOOL WDL_Fallback_SHGetPathFromIDListUTF8(const struct _ITEMIDLIST* pidl, LPSTR path, int pathLength)
#endif
{
  (void) pathLength;
  return SHGetPathFromIDListA((PCIDLIST_ABSOLUTE) pidl, path);
}

WDL_WIN32_UTF8_FALLBACK_IMPL WCHAR* WDL_Fallback_UTF8ToWC(const char* utf8, BOOL doubleNull, int minimumSize, DWORD* sizeOut)
{
  if (!utf8)
  {
    return NULL;
  }

  int length = 0;
  if (doubleNull)
  {
    while (utf8[length] != '\0' || utf8[length + 1] != '\0')
    {
      ++length;
    }
    length += 2;
  }
  else
  {
    length = -1;
  }

  UINT codePage = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  int required = MultiByteToWideChar(codePage, flags, utf8, length, NULL, 0);

  if (required <= 0)
  {
    codePage = CP_ACP;
    flags = 0;
    required = MultiByteToWideChar(codePage, flags, utf8, length, NULL, 0);
  }

  if (required <= 0)
  {
    return NULL;
  }

  if (minimumSize > required)
  {
    required = minimumSize;
  }

  const int terminators = doubleNull ? 2 : 1;
  const SIZE_T allocation = (SIZE_T) (required + terminators) * sizeof(WCHAR);
  WCHAR* wide = (WCHAR*) LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, allocation);
  if (!wide)
  {
    return NULL;
  }

  const int written = MultiByteToWideChar(codePage, flags, utf8, length, wide, required);
  if (written >= 0 && sizeOut)
  {
    *sizeOut = (DWORD) written;
  }

  return wide;
}

WDL_WIN32_UTF8_FALLBACK_IMPL int WDL_Fallback_UTF8_SendBFFM_SETSEL(HWND hwnd, const char* selection)
{
  return (int) SendMessageA(hwnd, BFFM_SETSELECTIONA, TRUE, (LPARAM) selection);
}

WDL_WIN32_UTF8_FALLBACK_IMPL LPSTR WDL_Fallback_GetCommandParametersUTF8(void)
{
  LPSTR commandLine = GetCommandLineA();
  if (!commandLine)
  {
    return NULL;
  }

  while (*commandLine == ' ')
  {
    ++commandLine;
  }

  if (*commandLine == '"')
  {
    ++commandLine;
    while (*commandLine && *commandLine != '"')
    {
      ++commandLine;
    }
    if (*commandLine == '"')
    {
      ++commandLine;
    }
  }
  else
  {
    while (*commandLine && *commandLine != ' ')
    {
      ++commandLine;
    }
  }

  while (*commandLine == ' ')
  {
    ++commandLine;
  }

  return *commandLine ? commandLine : NULL;
}

WDL_WIN32_UTF8_FALLBACK_IMPL void WDL_Fallback_UTF8_HookComboBoxCtx(struct WdlWindowsSandboxContext* context, HWND hwnd)
{
  (void) context;
  (void) hwnd;
}

WDL_WIN32_UTF8_FALLBACK_IMPL void WDL_Fallback_UTF8_HookListViewCtx(struct WdlWindowsSandboxContext* context, HWND hwnd)
{
  (void) context;
  (void) hwnd;
}

WDL_WIN32_UTF8_FALLBACK_IMPL void WDL_Fallback_UTF8_HookListBoxCtx(struct WdlWindowsSandboxContext* context, HWND hwnd)
{
  (void) context;
  (void) hwnd;
}

WDL_WIN32_UTF8_FALLBACK_IMPL void WDL_Fallback_UTF8_HookTreeViewCtx(struct WdlWindowsSandboxContext* context, HWND hwnd)
{
  (void) context;
  (void) hwnd;
}

WDL_WIN32_UTF8_FALLBACK_IMPL void WDL_Fallback_UTF8_HookTabCtrlCtx(struct WdlWindowsSandboxContext* context, HWND hwnd)
{
  (void) context;
  (void) hwnd;
}

WDL_WIN32_UTF8_FALLBACK_IMPL void WDL_Fallback_UTF8_ListViewConvertDispInfoToW(void* data)
{
  (void) data;
}

#if defined(WDL_WIN32_UTF8_USE_IPLUG_FALLBACKS)

WDL_WIN32_UTF8_IMPL void WDL_UTF8_SetSandboxContext(struct WdlWindowsSandboxContext* context)
{
  WDL_Fallback_UTF8_SetSandboxContext(context);
}

WDL_WIN32_UTF8_IMPL struct WdlWindowsSandboxContext* WDL_UTF8_GetSandboxContext(void)
{
  return WDL_Fallback_UTF8_GetSandboxContext();
}

WDL_WIN32_UTF8_IMPL const char* WDL_UTF8_SandboxContextPropertyName(void)
{
  return WDL_Fallback_UTF8_SandboxContextPropertyName();
}

WDL_WIN32_UTF8_IMPL DWORD GetCurrentDirectoryUTF8(DWORD bufferLength, LPTSTR buffer)
{
  return WDL_Fallback_GetCurrentDirectoryUTF8(bufferLength, buffer);
}

WDL_WIN32_UTF8_IMPL BOOL SetCurrentDirectoryUTF8(LPCTSTR path)
{
  return WDL_Fallback_SetCurrentDirectoryUTF8(path);
}

WDL_WIN32_UTF8_IMPL BOOL GetOpenFileNameUTF8(LPOPENFILENAME ofn)
{
  return WDL_Fallback_GetOpenFileNameUTF8(ofn);
}

WDL_WIN32_UTF8_IMPL BOOL GetSaveFileNameUTF8(LPOPENFILENAME ofn)
{
  return WDL_Fallback_GetSaveFileNameUTF8(ofn);
}

WDL_WIN32_UTF8_IMPL HINSTANCE LoadLibraryUTF8(LPCTSTR path)
{
  return WDL_Fallback_LoadLibraryUTF8(path);
}

WDL_WIN32_UTF8_IMPL struct _ITEMIDLIST* SHBrowseForFolderUTF8(struct _browseinfoA* info)
{
  return WDL_Fallback_SHBrowseForFolderUTF8(info);
}

#if defined(_WIN64)
WDL_WIN32_UTF8_IMPL BOOL SHGetPathFromIDListUTF8(const struct _ITEMIDLIST __unaligned* pidl, LPSTR path, int pathLength)
#else
WDL_WIN32_UTF8_IMPL BOOL SHGetPathFromIDListUTF8(const struct _ITEMIDLIST* pidl, LPSTR path, int pathLength)
#endif
{
  return WDL_Fallback_SHGetPathFromIDListUTF8(pidl, path, pathLength);
}

WDL_WIN32_UTF8_IMPL WCHAR* WDL_UTF8ToWC(const char* utf8, BOOL doubleNull, int minimumSize, DWORD* sizeOut)
{
  return WDL_Fallback_UTF8ToWC(utf8, doubleNull, minimumSize, sizeOut);
}

WDL_WIN32_UTF8_IMPL int WDL_UTF8_SendBFFM_SETSEL(HWND hwnd, const char* selection)
{
  return WDL_Fallback_UTF8_SendBFFM_SETSEL(hwnd, selection);
}

WDL_WIN32_UTF8_IMPL LPSTR GetCommandParametersUTF8(void)
{
  return WDL_Fallback_GetCommandParametersUTF8();
}

WDL_WIN32_UTF8_IMPL void WDL_UTF8_HookComboBoxCtx(struct WdlWindowsSandboxContext* context, HWND hwnd)
{
  WDL_Fallback_UTF8_HookComboBoxCtx(context, hwnd);
}

WDL_WIN32_UTF8_IMPL void WDL_UTF8_HookListViewCtx(struct WdlWindowsSandboxContext* context, HWND hwnd)
{
  WDL_Fallback_UTF8_HookListViewCtx(context, hwnd);
}

WDL_WIN32_UTF8_IMPL void WDL_UTF8_HookListBoxCtx(struct WdlWindowsSandboxContext* context, HWND hwnd)
{
  WDL_Fallback_UTF8_HookListBoxCtx(context, hwnd);
}

WDL_WIN32_UTF8_IMPL void WDL_UTF8_HookTreeViewCtx(struct WdlWindowsSandboxContext* context, HWND hwnd)
{
  WDL_Fallback_UTF8_HookTreeViewCtx(context, hwnd);
}

WDL_WIN32_UTF8_IMPL void WDL_UTF8_HookTabCtrlCtx(struct WdlWindowsSandboxContext* context, HWND hwnd)
{
  WDL_Fallback_UTF8_HookTabCtrlCtx(context, hwnd);
}

WDL_WIN32_UTF8_IMPL void WDL_UTF8_ListViewConvertDispInfoToW(void* data)
{
  WDL_Fallback_UTF8_ListViewConvertDispInfoToW(data);
}

#endif

#undef WDL_WIN32_UTF8_FALLBACK_IMPL
