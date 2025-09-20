#if defined(_WIN32) && !defined(WDL_WIN32_UTF8_NO_UI_IMPL)
#include <shlobj.h>
#include <commctrl.h>
#endif

#include <stdint.h>

#include "win32_utf8.h"
#include "wdltypes.h"
#include "wdlutf8.h"

#ifdef _WIN32

#if !defined(WDL_UTF8_HAS_SANDBOX_CONTEXT)
  #if defined(__has_include)
    #if __has_include("WdlWindowsSandboxContext.h")
      #include "WdlWindowsSandboxContext.h"
      #define WDL_UTF8_HAS_SANDBOX_CONTEXT 1
    #endif
  #else
    #include "WdlWindowsSandboxContext.h"
    #define WDL_UTF8_HAS_SANDBOX_CONTEXT 1
  #endif
#endif

#ifndef WDL_UTF8_HAS_SANDBOX_CONTEXT
#define WDL_UTF8_HAS_SANDBOX_CONTEXT 0
#endif

#if !defined(WDL_NO_SUPPORT_UTF8)

#ifdef __cplusplus
extern "C" {
#endif


#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  #if defined(_MSC_VER)
    static __declspec(thread) WdlWindowsSandboxContext* g_wdl_utf8_tls_context = NULL;
  #elif defined(__GNUC__)
    static __thread WdlWindowsSandboxContext* g_wdl_utf8_tls_context = NULL;
  #else
    static WdlWindowsSandboxContext* g_wdl_utf8_tls_context = NULL;
  #endif
#else
  struct WdlWindowsSandboxContext;
#endif

void WDL_UTF8_SetSandboxContext(WdlWindowsSandboxContext* context)
{
#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  g_wdl_utf8_tls_context = context;
#else
  (void) context;
#endif
}

WdlWindowsSandboxContext* WDL_UTF8_GetSandboxContext(void)
{
#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  return g_wdl_utf8_tls_context;
#else
  return NULL;
#endif
}

static WdlWindowsSandboxContext* wdl_utf8_current_context(void)
{
  return WDL_UTF8_GetSandboxContext();
}


#ifndef WDL_UTF8_MAXFNLEN
  // only affects calls to GetCurrentDirectoryUTF8() and a few others
  // could make them all dynamic using WIDETOMB_ALLOC() but meh the callers probably never pass more than 4k anyway
#define WDL_UTF8_MAXFNLEN 4096
#endif

#define MBTOWIDE(symbase, src) \
                int symbase##_size; \
                WCHAR symbase##_buf[1024]; \
                WCHAR *symbase = (symbase##_size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,-1,NULL,0)) >= 1000 ? (WCHAR *)malloc(symbase##_size * sizeof(WCHAR) + 24) : symbase##_buf; \
                int symbase##_ok = symbase ? (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,-1,symbase,symbase##_size < 1 ? 1024 : symbase##_size)) : 0

#define MBTOWIDE_FREE(symbase) if (symbase != symbase##_buf) free(symbase)


#define WIDETOMB_ALLOC(symbase, length) \
            WCHAR symbase##_buf[1024]; \
            size_t symbase##_size = sizeof(symbase##_buf); \
            WCHAR *symbase = (length) > 1000 ? (WCHAR *)malloc(symbase##_size = (sizeof(WCHAR)*(length) + 10)) : symbase##_buf

#define WIDETOMB_FREE(symbase) if (symbase != symbase##_buf) free(symbase)

BOOL WDL_HasUTF8(const char *_str)
{
  return WDL_DetectUTF8(_str) > 0;
}

static BOOL WDL_HasUTF8_FILENAME(const char *_str)
{
  return WDL_DetectUTF8(_str) > 0 || (_str && strlen(_str)>=256);
}

static void wdl_utf8_correctlongpath(WCHAR *buf)
{
  const WCHAR *insert;
  WCHAR *wr;
  int skip = 0;
  if (!buf || !buf[0] || wcslen(buf) < 256) return;
  if (buf[1] == ':') insert=L"\\\\?\\";
  else if (buf[0] == '\\' && buf[1] == '\\') { insert = L"\\\\?\\UNC\\"; skip=2; }
  else return;

  wr = buf + wcslen(insert);
  memmove(wr, buf + skip, (wcslen(buf+skip)+1)*2);
  memmove(buf,insert,wcslen(insert)*2);
  while (*wr)
  {
    if (*wr == '/') *wr = '\\';
    wr++;
  }
}

#ifdef AND_IS_NOT_WIN9X
#undef AND_IS_NOT_WIN9X
#endif
#ifdef IS_NOT_WIN9X_AND
#undef IS_NOT_WIN9X_AND
#endif

#ifdef WDL_SUPPORT_WIN9X
#define IS_NOT_WIN9X_AND (GetVersion() < 0x80000000) &&
#define AND_IS_NOT_WIN9X && (GetVersion() < 0x80000000)
#else
#define AND_IS_NOT_WIN9X
#define IS_NOT_WIN9X_AND
#endif

static ATOM g_wdl_utf8_combo_atom;

static ATOM* wdl_utf8_combo_atom_slot_for(WdlWindowsSandboxContext* context)
{
#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  if (context)
    return &context->utf8_hooks.combo_atom;
#else
  (void) context;
#endif
  return &g_wdl_utf8_combo_atom;
}

static ATOM* wdl_utf8_combo_atom_slot(void)
{
  return wdl_utf8_combo_atom_slot_for(wdl_utf8_current_context());
}
#define WDL_UTF8_OLDPROCPROP "WDLUTF8OldProc"

static const char kWdlUtf8ContextProp[] = "WDLUTF8SandboxContext";

const char* WDL_UTF8_SandboxContextPropertyName(void)
{
  return kWdlUtf8ContextProp;
}

struct wdl_utf8_property_key
{
  char stack[128];
  char* heap;
};

static void wdl_utf8_property_key_release(struct wdl_utf8_property_key* key)
{
#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  if (key && key->heap)
  {
    free(key->heap);
    key->heap = NULL;
  }
#else
  (void) key;
#endif
}

static const char* wdl_utf8_property_name(WdlWindowsSandboxContext* context,
                                          struct wdl_utf8_property_key* key)
{
#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  if (context)
  {
    if (!context->utf8_hooks.property_prefix || context->utf8_hooks.property_prefix_length == 0)
    {
      char generated_prefix[64];
      if (WdlWindowsSandboxContext_FormatUtf8PropertyPrefix(context, NULL, generated_prefix, sizeof(generated_prefix)))
      {
        WdlWindowsSandboxContext_SetUtf8PropertyPrefix(context, generated_prefix);
      }
    }

    if (context->utf8_hooks.property_prefix && context->utf8_hooks.property_prefix_length > 0)
    {
      const size_t prefix_len = context->utf8_hooks.property_prefix_length;
      const size_t base_len = sizeof(WDL_UTF8_OLDPROCPROP) - 1;
      const size_t total_len = prefix_len + base_len;

      if (total_len < sizeof(key->stack))
      {
        memcpy(key->stack, context->utf8_hooks.property_prefix, prefix_len);
        memcpy(key->stack + prefix_len, WDL_UTF8_OLDPROCPROP, base_len + 1);
        return key->stack;
      }

      char* combined = (char*) malloc(total_len + 1);
      if (!combined)
      {
        return WDL_UTF8_OLDPROCPROP;
      }

      memcpy(combined, context->utf8_hooks.property_prefix, prefix_len);
      memcpy(combined + prefix_len, WDL_UTF8_OLDPROCPROP, base_len + 1);
      key->heap = combined;
      return combined;
    }
  }
#else
  (void) context;
  (void) key;
#endif

  return WDL_UTF8_OLDPROCPROP;
}

struct lv_tmpbuf_state
{
  WCHAR* buf;
  int buf_sz;
};

struct wdl_utf8_hook
{
#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  WdlWindowsSandboxContext* context;
#endif
  HWND hwnd;
  WNDPROC old_proc;
  WNDPROC old_proc_wide;
  struct lv_tmpbuf_state* list_view_buffer;
};

static struct wdl_utf8_hook* wdl_utf8_hook_get(HWND hwnd)
{
#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  WdlWindowsSandboxContext* context = wdl_utf8_current_context();
  if (!context)
  {
    context = (WdlWindowsSandboxContext*) GetPropA(hwnd, WDL_UTF8_SandboxContextPropertyName());
  }

  if (context)
  {
    struct wdl_utf8_property_key key = {{0}, NULL};
    const char* property = wdl_utf8_property_name(context, &key);
    struct wdl_utf8_hook* hook = (struct wdl_utf8_hook*) GetPropA(hwnd, property);
    wdl_utf8_property_key_release(&key);
    if (hook)
    {
      return hook;
    }
  }
#endif

  return (struct wdl_utf8_hook*) GetPropA(hwnd, WDL_UTF8_OLDPROCPROP);
}

static void wdl_utf8_hook_destroy(struct wdl_utf8_hook* hook)
{
  if (!hook)
  {
    return;
  }

  if (hook->list_view_buffer)
  {
    if (hook->list_view_buffer->buf)
    {
      free(hook->list_view_buffer->buf);
    }
    free(hook->list_view_buffer);
    hook->list_view_buffer = NULL;
  }

  free(hook);
}

static struct wdl_utf8_hook* wdl_utf8_hook_create(WdlWindowsSandboxContext* context, HWND hwnd)
{
  struct wdl_utf8_hook* hook = (struct wdl_utf8_hook*) calloc(1, sizeof(*hook));
  if (!hook)
  {
    return NULL;
  }

#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  hook->context = context;
#else
  (void) context;
#endif
  hook->hwnd = hwnd;
  return hook;
}

static int wdl_utf8_hook_attach(HWND hwnd, struct wdl_utf8_hook* hook)
{
  if (!hook)
  {
    return 0;
  }

  int attached = 0;

#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  if (hook->context)
  {
    struct wdl_utf8_property_key key = {{0}, NULL};
    const char* property = wdl_utf8_property_name(hook->context, &key);
    if (SetPropA(hwnd, property, (HANDLE) hook))
    {
      if (SetPropA(hwnd, WDL_UTF8_SandboxContextPropertyName(), (HANDLE) hook->context))
      {
        attached = 1;
      }
      else
      {
        RemovePropA(hwnd, property);
      }
    }

    if (!attached)
    {
      RemovePropA(hwnd, WDL_UTF8_SandboxContextPropertyName());
    }

    wdl_utf8_property_key_release(&key);
  }
#endif

  if (!attached)
  {
    if (SetPropA(hwnd, WDL_UTF8_OLDPROCPROP, (HANDLE) hook))
    {
      attached = 1;
    }
  }

  return attached;
}

static void wdl_utf8_hook_detach(HWND hwnd)
{
  struct wdl_utf8_hook* hook = wdl_utf8_hook_get(hwnd);
  if (!hook)
  {
    return;
  }

#if WDL_UTF8_HAS_SANDBOX_CONTEXT
  if (hook->context)
  {
    struct wdl_utf8_property_key key = {{0}, NULL};
    const char* property = wdl_utf8_property_name(hook->context, &key);
    RemovePropA(hwnd, property);
    RemovePropA(hwnd, WDL_UTF8_SandboxContextPropertyName());
    wdl_utf8_property_key_release(&key);
  }
#endif

  RemovePropA(hwnd, WDL_UTF8_OLDPROCPROP);
  wdl_utf8_hook_destroy(hook);
}

static WNDPROC wdl_utf8_swap_window_proc(HWND hwnd, WNDPROC new_proc, DWORD* error)
{
  SetLastError(0);
  LONG_PTR previous = SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR) new_proc);
  DWORD last_error = GetLastError();
  if (error)
  {
    *error = last_error;
  }

  if (!previous && last_error != 0)
  {
    return NULL;
  }

  return (WNDPROC) previous;
}

int GetWindowTextUTF8(HWND hWnd, LPTSTR lpString, int nMaxCount)
{
  if (WDL_NOT_NORMALLY(!lpString || nMaxCount < 1)) return 0;
  if (WDL_NOT_NORMALLY(hWnd == NULL))
  {
    *lpString = 0;
    return 0;
  }
  ATOM* combo_atom_slot = wdl_utf8_combo_atom_slot();
  ATOM combo_atom = combo_atom_slot ? *combo_atom_slot : 0;
  if (nMaxCount>0 AND_IS_NOT_WIN9X)
  {
    int alloc_size=nMaxCount;
    WNDPROC restore_wndproc = NULL;
    DWORD restore_error = 0;

    // if a hooked combo box, and has an edit child, ask it directly
    struct wdl_utf8_hook* combo_hook = wdl_utf8_hook_get(hWnd);
    if (combo_atom && combo_atom == GetClassWord(hWnd,GCW_ATOM) && combo_hook)
    {
      HWND h2=FindWindowEx(hWnd,NULL,"Edit",NULL);
      if (h2)
      {
        struct wdl_utf8_hook* edit_hook = wdl_utf8_hook_get(h2);
        if (edit_hook && edit_hook->old_proc)
        {
          restore_wndproc = wdl_utf8_swap_window_proc(h2, edit_hook->old_proc, &restore_error);
          if (restore_error != 0)
          {
            restore_wndproc = NULL;
          }
        }
        hWnd=h2;
      }
      else
      {
        // get via selection
        int sel = (int) SendMessage(hWnd,CB_GETCURSEL,0,0);
        if (sel>=0)
        {
          int len = (int) SendMessage(hWnd,CB_GETLBTEXTLEN,sel,0);
          char *p = lpString;
          if (len > nMaxCount-1) 
          {
            p = (char*)calloc(len+1,1);
            len = nMaxCount-1;
          }
          lpString[0]=0;
          if (p)
          {
            SendMessage(hWnd,CB_GETLBTEXT,sel,(LPARAM)p);
            if (p!=lpString) 
            {
              memcpy(lpString,p,len);
              lpString[len]=0;
              free(p);
            }
            return len;
          }
        }
      }
    }

    // prevent large values of nMaxCount from allocating memory unless the underlying text is big too
    if (alloc_size > 512)  
    {
      int l=GetWindowTextLengthW(hWnd);
      if (l>=0 && l < 512) alloc_size=1000;
    }

    {
      WIDETOMB_ALLOC(wbuf, alloc_size);
      if (wbuf)
      {
        lpString[0]=0;
        if (GetWindowTextW(hWnd,wbuf,(int) (wbuf_size/sizeof(WCHAR))))
        {
          if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpString,nMaxCount,NULL,NULL))
            lpString[nMaxCount-1]=0;
        }

        WIDETOMB_FREE(wbuf);

        if (restore_wndproc)
          SetWindowLongPtr(hWnd,GWLP_WNDPROC,restore_wndproc);

        return (int)strlen(lpString);
      }
    }
    if (restore_wndproc)
      wdl_utf8_swap_window_proc(hWnd, restore_wndproc, NULL);
  }
  return GetWindowTextA(hWnd,lpString,nMaxCount);
}

UINT GetDlgItemTextUTF8(HWND hDlg, int nIDDlgItem, LPTSTR lpString, int nMaxCount)
{
  HWND h = GetDlgItem(hDlg,nIDDlgItem);
  if (WDL_NORMALLY(h!=NULL)) return GetWindowTextUTF8(h,lpString,nMaxCount);
  if (lpString && nMaxCount > 0)
    *lpString = 0;
  return 0;
}


BOOL SetDlgItemTextUTF8(HWND hDlg, int nIDDlgItem, LPCTSTR lpString)
{
  HWND h = GetDlgItem(hDlg,nIDDlgItem);
  if (WDL_NOT_NORMALLY(!h)) return FALSE;

#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!lpString)) return FALSE;
#endif

  if (WDL_HasUTF8(lpString) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,lpString);
    if (wbuf_ok)
    {
      BOOL rv = SetWindowTextW(h, wbuf);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }

    MBTOWIDE_FREE(wbuf);
  }

  return SetWindowTextA(h, lpString);

}

static LRESULT WINAPI __forceUnicodeWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_SETTEXT && lParam)
  {
    MBTOWIDE(wbuf,(const char *)lParam);
    if (wbuf_ok)
    {
      LRESULT rv = DefWindowProcW(hwnd, uMsg, wParam, (LPARAM)wbuf);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

BOOL SetWindowTextUTF8(HWND hwnd, LPCTSTR str)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!hwnd || !str)) return FALSE;
#endif
  if (WDL_HasUTF8(str) AND_IS_NOT_WIN9X)
  {
    DWORD pid;
    if (GetWindowThreadProcessId(hwnd,&pid) == GetCurrentThreadId() && 
        pid == GetCurrentProcessId() && 
        !IsWindowUnicode(hwnd) &&
        !(GetWindowLong(hwnd,GWL_STYLE)&WS_CHILD)
        )
    {
      LPARAM tmp = SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LPARAM)__forceUnicodeWndProc);
      BOOL rv = SetWindowTextA(hwnd, str);
      SetWindowLongPtr(hwnd, GWLP_WNDPROC, tmp);
      return rv;
    }
    else
    {
      MBTOWIDE(wbuf,str);
      if (wbuf_ok)
      {
        BOOL rv = SetWindowTextW(hwnd, wbuf);
        MBTOWIDE_FREE(wbuf);
        return rv;
      }

      MBTOWIDE_FREE(wbuf);
    }
  }

  return SetWindowTextA(hwnd,str);
}

int MessageBoxUTF8(HWND hwnd, LPCTSTR lpText, LPCTSTR lpCaption, UINT fl)
{
  if ((WDL_HasUTF8(lpText)||WDL_HasUTF8(lpCaption)) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,lpText);
    if (wbuf_ok)
    {
      MBTOWIDE(wcap,lpCaption?lpCaption:"");
      if (wcap_ok)
      {
        int ret=MessageBoxW(hwnd,wbuf,lpCaption?wcap:NULL,fl);
        MBTOWIDE_FREE(wcap);
        MBTOWIDE_FREE(wbuf);
        return ret;
      }
      MBTOWIDE_FREE(wbuf);
    }
  }
  return MessageBoxA(hwnd,lpText,lpCaption,fl);
}

UINT DragQueryFileUTF8(HDROP hDrop, UINT idx, char *buf, UINT bufsz)
{
  if (buf && bufsz && idx!=-1 AND_IS_NOT_WIN9X)
  {
    const UINT reqsz = DragQueryFileW(hDrop,idx,NULL,0);
    WIDETOMB_ALLOC(wbuf, reqsz+32);
    if (wbuf)
    {
      UINT rv=DragQueryFileW(hDrop,idx,wbuf,(int)(wbuf_size/sizeof(WCHAR)));
      if (rv)
      {
        if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,buf,bufsz,NULL,NULL))
          buf[bufsz-1]=0;
      }
      WIDETOMB_FREE(wbuf);
      return rv;
    }
  }
  return DragQueryFileA(hDrop,idx,buf,bufsz);
}


WCHAR *WDL_UTF8ToWC(const char *buf, BOOL doublenull, int minsize, DWORD *sizeout)
{
  if (doublenull)
  {
    int sz=1;
    const char *p = (const char *)buf;
    WCHAR *pout,*ret;

    while (*p)
    {
      int a=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,-1,NULL,0);
      int sp=(int)strlen(p)+1;
      if (a < sp)a=sp; // in case it needs to be ansi mapped
      sz+=a;
      p+=sp;
    }
    if (sz < minsize) sz=minsize;

    pout = (WCHAR *) malloc(sizeof(WCHAR)*(sz+4));
    if (!pout) return NULL;

    ret=pout;
    p = (const char *)buf;
    while (*p)
    {
      int a;
      *pout=0;
      a = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,-1,pout,(int) (sz-(pout-ret)));
      if (!a)
      {
        pout[0]=0;
        a=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,p,-1,pout,(int) (sz-(pout-ret)));
      }
      pout += a;
      p+=strlen(p)+1;
    }
    *pout=0;
    pout[1]=0;
    if (sizeout) *sizeout=sz;
    return ret;
  }
  else
  {
    int srclen = (int)strlen(buf)+1;
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,buf,srclen,NULL,0);
    if (size < srclen)size=srclen; // for ansi code page
    if (size<minsize)size=minsize;

    {
      WCHAR *outbuf = (WCHAR *)malloc(sizeof(WCHAR)*(size+128));
      if (!outbuf) return NULL;

      *outbuf=0;
      if (srclen>1)
      {
        int a=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,buf,srclen,outbuf, size);
        if (!a)
        {
          outbuf[0]=0;
          a=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,buf,srclen,outbuf,size);
        }
      }
      if (sizeout) *sizeout = size;
      return outbuf;
    }
  }
}

#ifndef WDL_WIN32_UTF8_NO_UI_IMPL
static BOOL GetOpenSaveFileNameUTF8(LPOPENFILENAME lpofn, BOOL save)
{

  OPENFILENAMEW tmp={sizeof(tmp),lpofn->hwndOwner,lpofn->hInstance,};
  BOOL ret;

  // allocate, convert input
  if (lpofn->lpstrFilter) tmp.lpstrFilter = WDL_UTF8ToWC(lpofn->lpstrFilter,TRUE,0,0);
  tmp.nFilterIndex = lpofn->nFilterIndex ;

  if (lpofn->lpstrFile) tmp.lpstrFile = WDL_UTF8ToWC(lpofn->lpstrFile,FALSE,lpofn->nMaxFile,&tmp.nMaxFile);
  if (lpofn->lpstrFileTitle) tmp.lpstrFileTitle = WDL_UTF8ToWC(lpofn->lpstrFileTitle,FALSE,lpofn->nMaxFileTitle,&tmp.nMaxFileTitle);
  if (lpofn->lpstrInitialDir) tmp.lpstrInitialDir = WDL_UTF8ToWC(lpofn->lpstrInitialDir,0,0,0);
  if (lpofn->lpstrTitle) tmp.lpstrTitle = WDL_UTF8ToWC(lpofn->lpstrTitle,0,0,0);
  if (lpofn->lpstrDefExt) tmp.lpstrDefExt = WDL_UTF8ToWC(lpofn->lpstrDefExt,0,0,0);
  tmp.Flags = lpofn->Flags;
  tmp.lCustData = lpofn->lCustData;
  tmp.lpfnHook = lpofn->lpfnHook;
  tmp.lpTemplateName  = (const WCHAR *)lpofn->lpTemplateName ;
 
  ret=save ? GetSaveFileNameW(&tmp) : GetOpenFileNameW(&tmp);

  // free, convert output
  if (ret && lpofn->lpstrFile && tmp.lpstrFile)
  {
    if ((tmp.Flags & OFN_ALLOWMULTISELECT) && tmp.lpstrFile[wcslen(tmp.lpstrFile)+1])
    {
      char *op = lpofn->lpstrFile;
      WCHAR *ip = tmp.lpstrFile;
      while (*ip)
      {
        const int bcount = WideCharToMultiByte(CP_UTF8,0,ip,-1,NULL,0,NULL,NULL);

        const int maxout=lpofn->nMaxFile - 2 - (int)(op - lpofn->lpstrFile);
        if (maxout < 2+bcount) break;
        op += WideCharToMultiByte(CP_UTF8,0,ip,-1,op,maxout,NULL,NULL);
        ip += wcslen(ip)+1;
      }
      *op=0;
    }
    else
    {
      int len = WideCharToMultiByte(CP_UTF8,0,tmp.lpstrFile,-1,lpofn->lpstrFile,lpofn->nMaxFile-1,NULL,NULL);
      if (len == 0 && GetLastError()==ERROR_INSUFFICIENT_BUFFER) len = lpofn->nMaxFile-2;
      lpofn->lpstrFile[len]=0;
      if (!len) 
      {
        lpofn->lpstrFile[len+1]=0;
        ret=0;
      }
    }
    // convert 
  }

  lpofn->nFileOffset  = tmp.nFileOffset ;
  lpofn->nFileExtension  = tmp.nFileExtension;
  lpofn->lCustData = tmp.lCustData;

  free((WCHAR *)tmp.lpstrFilter);
  free((WCHAR *)tmp.lpstrFile);
  free((WCHAR *)tmp.lpstrFileTitle);
  free((WCHAR *)tmp.lpstrInitialDir);
  free((WCHAR *)tmp.lpstrTitle);
  free((WCHAR *)tmp.lpstrDefExt );

  lpofn->nFilterIndex  = tmp.nFilterIndex ;
  return ret;
}

BOOL GetOpenFileNameUTF8(LPOPENFILENAME lpofn)
{
#ifdef WDL_SUPPORT_WIN9X
  if (GetVersion()&0x80000000) return GetOpenFileNameA(lpofn);
#endif
  return GetOpenSaveFileNameUTF8(lpofn,FALSE);
}

BOOL GetSaveFileNameUTF8(LPOPENFILENAME lpofn)
{
#ifdef WDL_SUPPORT_WIN9X
  if (GetVersion()&0x80000000) return GetSaveFileNameA(lpofn);
#endif
  return GetOpenSaveFileNameUTF8(lpofn,TRUE);
}

BOOL SHGetSpecialFolderPathUTF8(HWND hwndOwner, LPTSTR lpszPath, int pszPathLen, int csidl, BOOL create)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!lpszPath)) return 0;
#endif
  if (lpszPath AND_IS_NOT_WIN9X)
  {
    WCHAR tmp[4096];
    if (SHGetSpecialFolderPathW(hwndOwner,tmp,csidl,create))
    {
      return WideCharToMultiByte(CP_UTF8,0,tmp,-1,lpszPath,pszPathLen,NULL,NULL) > 0;
    }
  }
  return SHGetSpecialFolderPathA(hwndOwner,lpszPath,csidl,create);
}


#if _MSC_VER > 1700 && defined(_WIN64)
BOOL SHGetPathFromIDListUTF8(const struct _ITEMIDLIST __unaligned *pidl, LPSTR pszPath, int pszPathLen)
#else
BOOL SHGetPathFromIDListUTF8(const struct _ITEMIDLIST *pidl, LPSTR pszPath, int pszPathLen)
#endif
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!pszPath)) return FALSE;
#endif
  if (pszPath AND_IS_NOT_WIN9X)
  {
    const int alloc_sz = pszPathLen < 4096 ? 4096 : pszPathLen;
    WIDETOMB_ALLOC(wfn,alloc_sz);
    if (wfn)
    {
      BOOL b = FALSE;
      if (SHGetPathFromIDListW(pidl,wfn))
      {
        b = WideCharToMultiByte(CP_UTF8,0,wfn,-1,pszPath,pszPathLen,NULL,NULL) > 0;
      }
      WIDETOMB_FREE(wfn);
      return b;
    }
  }
  return SHGetPathFromIDListA(pidl,pszPath);
}

struct _ITEMIDLIST *SHBrowseForFolderUTF8(struct _browseinfoA *bi)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!bi)) return NULL;
#endif
  if (bi && (WDL_HasUTF8(bi->pszDisplayName) || WDL_HasUTF8(bi->lpszTitle)) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wfn,bi->pszDisplayName);
    if (wfn_ok)
    {
      MBTOWIDE(wtxt,bi->lpszTitle);
      if (wtxt_ok)
      {
        BROWSEINFOW biw ={ bi->hwndOwner,bi->pidlRoot,wfn,wtxt,bi->ulFlags,bi->lpfn,(LPARAM)bi->lParam,bi->iImage };
        LPITEMIDLIST idlist = SHBrowseForFolderW(&biw);
        MBTOWIDE_FREE(wfn);
        MBTOWIDE_FREE(wtxt);
        return (struct _ITEMIDLIST *) idlist;
      }
      MBTOWIDE_FREE(wtxt);
    }
    MBTOWIDE_FREE(wfn);
  }
  return (struct _ITEMIDLIST *)SHBrowseForFolderA(bi);
}

int WDL_UTF8_SendBFFM_SETSEL(HWND hwnd, const char *str)
{
  if (IS_NOT_WIN9X_AND WDL_HasUTF8(str))
  {
    MBTOWIDE(wc, str);
    if (wc_ok)
    {
      int r=(int)SendMessage(hwnd, BFFM_SETSELECTIONW, 1, (LPARAM)wc);
      MBTOWIDE_FREE(wc);
      return r;
    }
    MBTOWIDE_FREE(wc);
  }
  return (int) SendMessage(hwnd, BFFM_SETSELECTIONA, 1, (LPARAM)str);
}

#endif

BOOL SetCurrentDirectoryUTF8(LPCTSTR path)
{
  if (WDL_HasUTF8_FILENAME(path) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,path);
    if (wbuf_ok) wdl_utf8_correctlongpath(wbuf);
    if (wbuf_ok)
    {
      BOOL rv=SetCurrentDirectoryW(wbuf);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return SetCurrentDirectoryA(path);
}

BOOL RemoveDirectoryUTF8(LPCTSTR path)
{
  if (WDL_HasUTF8_FILENAME(path) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,path);
    if (wbuf_ok) wdl_utf8_correctlongpath(wbuf);
    if (wbuf_ok)
    {
      BOOL rv=RemoveDirectoryW(wbuf);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return RemoveDirectoryA(path);
}

HINSTANCE LoadLibraryUTF8(LPCTSTR path)
{
  if (WDL_HasUTF8_FILENAME(path) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,path);
    if (wbuf_ok) wdl_utf8_correctlongpath(wbuf);
    if (wbuf_ok)
    {
      HINSTANCE rv=LoadLibraryW(wbuf);
      if (rv)
      {
        MBTOWIDE_FREE(wbuf);
        return rv;
      }
    }
    MBTOWIDE_FREE(wbuf);
  }
  return LoadLibraryA(path);
}

BOOL CreateDirectoryUTF8(LPCTSTR path, LPSECURITY_ATTRIBUTES attr)
{
  if (WDL_HasUTF8_FILENAME(path) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,path);
    if (wbuf_ok) wdl_utf8_correctlongpath(wbuf);
    if (wbuf_ok)
    {
      BOOL rv=CreateDirectoryW(wbuf,attr);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return CreateDirectoryA(path,attr);
}

BOOL DeleteFileUTF8(LPCTSTR path)
{
  if (WDL_HasUTF8_FILENAME(path) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,path);
    if (wbuf_ok) wdl_utf8_correctlongpath(wbuf);
    if (wbuf_ok)
    {
      BOOL rv=DeleteFileW(wbuf);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return DeleteFileA(path);
}

BOOL MoveFileUTF8(LPCTSTR existfn, LPCTSTR newfn)
{
  if ((WDL_HasUTF8_FILENAME(existfn)||WDL_HasUTF8_FILENAME(newfn)) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,existfn);
    if (wbuf_ok) wdl_utf8_correctlongpath(wbuf);
    if (wbuf_ok)
    {
      MBTOWIDE(wbuf2,newfn);
      if (wbuf2_ok) wdl_utf8_correctlongpath(wbuf2);
      if (wbuf2_ok)
      {
        int rv=MoveFileW(wbuf,wbuf2);
        MBTOWIDE_FREE(wbuf2);
        MBTOWIDE_FREE(wbuf);
        return rv;
      }
      MBTOWIDE_FREE(wbuf2);
    }
    MBTOWIDE_FREE(wbuf);
  }
  return MoveFileA(existfn,newfn);
}

BOOL CopyFileUTF8(LPCTSTR existfn, LPCTSTR newfn, BOOL fie)
{
  if ((WDL_HasUTF8_FILENAME(existfn)||WDL_HasUTF8_FILENAME(newfn)) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,existfn);
    if (wbuf_ok) wdl_utf8_correctlongpath(wbuf);
    if (wbuf_ok)
    {
      MBTOWIDE(wbuf2,newfn);
      if (wbuf2_ok) wdl_utf8_correctlongpath(wbuf2);
      if (wbuf2_ok)
      {
        int rv=CopyFileW(wbuf,wbuf2,fie);
        MBTOWIDE_FREE(wbuf2);
        MBTOWIDE_FREE(wbuf);
        return rv;
      }
      MBTOWIDE_FREE(wbuf2);
    }
    MBTOWIDE_FREE(wbuf);
  }
  return CopyFileA(existfn,newfn,fie);
}


DWORD GetModuleFileNameUTF8(HMODULE hModule, LPTSTR lpBuffer, DWORD nBufferLength)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!lpBuffer||!nBufferLength)) return 0;
#endif
  if (lpBuffer && nBufferLength > 1 AND_IS_NOT_WIN9X)
  {

    WCHAR wbuf[WDL_UTF8_MAXFNLEN];
    wbuf[0]=0;
    if (GetModuleFileNameW(hModule,wbuf,WDL_UTF8_MAXFNLEN) && wbuf[0])
    {
      int rv=WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpBuffer,nBufferLength,NULL,NULL);
      if (rv) return rv;
    }
  }
  return GetModuleFileNameA(hModule,lpBuffer,nBufferLength);
}

DWORD GetLongPathNameUTF8(LPCTSTR lpszShortPath, LPSTR lpszLongPath, DWORD cchBuffer)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!lpszShortPath || !lpszLongPath || !cchBuffer)) return 0;
#endif
  if (lpszShortPath && lpszLongPath && cchBuffer > 1 AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(sbuf,lpszShortPath);
    if (sbuf_ok)
    {
      WCHAR wbuf[WDL_UTF8_MAXFNLEN];
      wbuf[0]=0;
      if (GetLongPathNameW(sbuf,wbuf,WDL_UTF8_MAXFNLEN) && wbuf[0])
      {
        int rv=WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpszLongPath,cchBuffer,NULL,NULL);
        if (rv)
        {
          MBTOWIDE_FREE(sbuf);
          return rv;
        }
      }
      MBTOWIDE_FREE(sbuf);
    }
  }
  return GetLongPathNameA(lpszShortPath, lpszLongPath, cchBuffer);
}

UINT GetTempFileNameUTF8(LPCTSTR lpPathName, LPCTSTR lpPrefixString, UINT uUnique, LPSTR lpTempFileName)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!lpPathName || !lpPrefixString || !lpTempFileName)) return 0;
#endif
  if (lpPathName && lpPrefixString && lpTempFileName AND_IS_NOT_WIN9X
      && (WDL_HasUTF8(lpPathName) || WDL_HasUTF8(lpPrefixString)))
  {
    MBTOWIDE(sbuf1,lpPathName);
    if (sbuf1_ok)
    {
      MBTOWIDE(sbuf2,lpPrefixString);
      if (sbuf2_ok)
      {
        int rv;
        WCHAR wbuf[WDL_UTF8_MAXFNLEN];
        wbuf[0]=0;
        rv=GetTempFileNameW(sbuf1,sbuf2,uUnique,wbuf);
        WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpTempFileName,MAX_PATH,NULL,NULL);
        MBTOWIDE_FREE(sbuf1);
        MBTOWIDE_FREE(sbuf2);
        return rv;
      }
      MBTOWIDE_FREE(sbuf1);
    }
  }
  return GetTempFileNameA(lpPathName, lpPrefixString, uUnique, lpTempFileName);
}

DWORD GetTempPathUTF8(DWORD nBufferLength, LPTSTR lpBuffer)
{
  if (lpBuffer && nBufferLength > 1 AND_IS_NOT_WIN9X)
  {

    WCHAR wbuf[WDL_UTF8_MAXFNLEN];
    wbuf[0]=0;
    if (GetTempPathW(WDL_UTF8_MAXFNLEN,wbuf) && wbuf[0])
    {
      int rv=WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpBuffer,nBufferLength,NULL,NULL);
      if (rv) return rv;
    }
  }
  return GetTempPathA(nBufferLength,lpBuffer);
}

DWORD GetCurrentDirectoryUTF8(DWORD nBufferLength, LPTSTR lpBuffer)
{
  if (lpBuffer && nBufferLength > 1 AND_IS_NOT_WIN9X)
  {

    WCHAR wbuf[WDL_UTF8_MAXFNLEN];
    wbuf[0]=0;
    if (GetCurrentDirectoryW(WDL_UTF8_MAXFNLEN,wbuf) && wbuf[0])
    {
      int rv=WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpBuffer,nBufferLength,NULL,NULL);
      if (rv) return rv;
    }
  }
  return GetCurrentDirectoryA(nBufferLength,lpBuffer);
}

HANDLE CreateFileUTF8(LPCTSTR lpFileName,DWORD dwDesiredAccess,DWORD dwShareMode,LPSECURITY_ATTRIBUTES lpSecurityAttributes,DWORD dwCreationDisposition,DWORD dwFlagsAndAttributes,HANDLE hTemplateFile)
{
  if (WDL_HasUTF8_FILENAME(lpFileName) AND_IS_NOT_WIN9X)
  {
    HANDLE h = INVALID_HANDLE_VALUE;
    
    MBTOWIDE(wstr, lpFileName);
    if (wstr_ok) wdl_utf8_correctlongpath(wstr);
    if (wstr_ok) h = CreateFileW(wstr,dwDesiredAccess,dwShareMode,lpSecurityAttributes,dwCreationDisposition,dwFlagsAndAttributes,hTemplateFile);
    MBTOWIDE_FREE(wstr);

    if (h != INVALID_HANDLE_VALUE || (wstr_ok && (dwDesiredAccess & GENERIC_WRITE))) return h;
  }
  return CreateFileA(lpFileName,dwDesiredAccess,dwShareMode,lpSecurityAttributes,dwCreationDisposition,dwFlagsAndAttributes,hTemplateFile);
}


int DrawTextUTF8(HDC hdc, LPCTSTR str, int nc, LPRECT lpRect, UINT format)
{
  WDL_ASSERT((format & DT_SINGLELINE) || !(format & (DT_BOTTOM|DT_VCENTER))); // if DT_BOTTOM or DT_VCENTER used, must have DT_SINGLELINE

  if (WDL_HasUTF8(str) AND_IS_NOT_WIN9X)
  {
    if (nc<0) nc=(int)strlen(str);

    {
      MBTOWIDE(wstr, str);
      if (wstr_ok)
      {
        int rv=DrawTextW(hdc,wstr,-1,lpRect,format);;
        MBTOWIDE_FREE(wstr);
        return rv;
      }
      MBTOWIDE_FREE(wstr);
    }

  }
  return DrawTextA(hdc,str,nc,lpRect,format);
}


BOOL InsertMenuUTF8(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCTSTR str)
{
  if (!(uFlags&(MF_BITMAP|MF_SEPARATOR)) && str && WDL_HasUTF8(str) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,str);
    if (wbuf_ok)
    {
      BOOL rv=InsertMenuW(hMenu,uPosition,uFlags,uIDNewItem,wbuf);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }
  }
  return InsertMenuA(hMenu,uPosition,uFlags,uIDNewItem,str);
}

BOOL InsertMenuItemUTF8( HMENU hMenu,UINT uItem, BOOL fByPosition, LPMENUITEMINFO lpmii)
{
  if (!lpmii) return FALSE;
  if ((lpmii->fMask & MIIM_TYPE) && (lpmii->fType&(MFT_SEPARATOR|MFT_STRING|MFT_BITMAP)) == MFT_STRING && lpmii->dwTypeData && WDL_HasUTF8(lpmii->dwTypeData) AND_IS_NOT_WIN9X)
  {
    BOOL rv;
    MENUITEMINFOW tmp = *(MENUITEMINFOW*)lpmii;
    MBTOWIDE(wbuf,lpmii->dwTypeData);
    if (wbuf_ok)
    {

      tmp.cbSize=sizeof(tmp);
      tmp.dwTypeData = wbuf;
      rv=InsertMenuItemW(hMenu,uItem,fByPosition,&tmp);

      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return InsertMenuItemA(hMenu,uItem,fByPosition,lpmii);
}
BOOL SetMenuItemInfoUTF8( HMENU hMenu,UINT uItem, BOOL fByPosition, LPMENUITEMINFO lpmii)
{
  if (!lpmii) return FALSE;
  if ((lpmii->fMask & MIIM_TYPE) && (lpmii->fType&(MFT_SEPARATOR|MFT_STRING|MFT_BITMAP)) == MFT_STRING && lpmii->dwTypeData && WDL_HasUTF8(lpmii->dwTypeData) AND_IS_NOT_WIN9X)
  {
    BOOL rv;
    MENUITEMINFOW tmp = *(MENUITEMINFOW*)lpmii;
    MBTOWIDE(wbuf,lpmii->dwTypeData);
    if (wbuf_ok)
    {
      tmp.cbSize=sizeof(tmp);
      tmp.dwTypeData = wbuf;
      rv=SetMenuItemInfoW(hMenu,uItem,fByPosition,&tmp);

      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return SetMenuItemInfoA(hMenu,uItem,fByPosition,lpmii);
}

BOOL GetMenuItemInfoUTF8( HMENU hMenu,UINT uItem, BOOL fByPosition, LPMENUITEMINFO lpmii)
{
  if (!lpmii) return FALSE;
  if ((lpmii->fMask & MIIM_TYPE) && lpmii->dwTypeData && lpmii->cch AND_IS_NOT_WIN9X)
  {
    MENUITEMINFOW tmp = *(MENUITEMINFOW*)lpmii;
    WIDETOMB_ALLOC(wbuf,lpmii->cch);

    if (wbuf)
    {
      BOOL rv;
      char *otd=lpmii->dwTypeData;
      int osz=lpmii->cbSize;
      tmp.cbSize=sizeof(tmp);
      tmp.dwTypeData = wbuf;
      tmp.cch = (UINT)(wbuf_size/sizeof(WCHAR));
      rv=GetMenuItemInfoW(hMenu,uItem,fByPosition,&tmp);

      if (rv && (tmp.fType&(MFT_SEPARATOR|MFT_STRING|MFT_BITMAP)) == MFT_STRING)
      {
        if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpmii->dwTypeData,lpmii->cch,NULL,NULL))
        {
          lpmii->dwTypeData[lpmii->cch-1]=0;
        }

        *lpmii = *(MENUITEMINFO*)&tmp; // copy results
        lpmii->cbSize=osz; // restore old stuff
        lpmii->dwTypeData = otd;
      }
      else rv=0;

      WIDETOMB_FREE(wbuf);
      if (rv)return rv;
    }
  }
  return GetMenuItemInfoA(hMenu,uItem,fByPosition,lpmii);
}


FILE *fopenUTF8(const char *filename, const char *mode)
{
  if (WDL_HasUTF8_FILENAME(filename) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,filename);
    if (wbuf_ok) wdl_utf8_correctlongpath(wbuf);
    if (wbuf_ok)
    {
      const char *p = mode;
      FILE *rv;
      WCHAR tb[32];      
      tb[0]=0;
      MultiByteToWideChar(CP_UTF8,0,mode,-1,tb,32);
      rv=tb[0] ? _wfopen(wbuf,tb) : NULL;
      MBTOWIDE_FREE(wbuf);
      if (rv) return rv;

      while (*p)
      {
        if (*p == '+' || *p == 'a' || *p == 'w') return NULL;
        p++;
      }
    }
  }
#ifdef fopen
#undef fopen
#endif
  return fopen(filename,mode);
#define fopen fopenUTF8
}

int statUTF8(const char *filename, struct stat *buffer)
{
  if (WDL_HasUTF8_FILENAME(filename) AND_IS_NOT_WIN9X)
  {
    MBTOWIDE(wbuf,filename);
    if (wbuf_ok) wdl_utf8_correctlongpath(wbuf);
    if (wbuf_ok)
    {
      int rv;
      rv=_wstat(wbuf,(struct _stat*)buffer);
      MBTOWIDE_FREE(wbuf);
      if (!rv) return rv;
    }
    else
    {
      MBTOWIDE_FREE(wbuf);
    }
  }
  return _stat(filename,(struct _stat*)buffer);
}

LPSTR GetCommandParametersUTF8()
{
  char *buf;
  int szneeded;
  LPWSTR w=GetCommandLineW();
  if (!w) return NULL;
  szneeded = WideCharToMultiByte(CP_UTF8,0,w,-1,NULL,0,NULL,NULL);
  if (szneeded<1) return NULL;
  buf = (char *)malloc(szneeded+10);
  if (!buf) return NULL;
  if (WideCharToMultiByte(CP_UTF8,0,w,-1,buf,szneeded+9,NULL,NULL)<1) return NULL;
  while (*buf == ' ') buf++;
  if (*buf == '\"')
  {
    buf++;
    while (*buf && *buf != '\"') buf++;
  }
  else
  {
    while (*buf && *buf != ' ') buf++;
  }
  if (*buf) buf++;
  while (*buf == ' ') buf++;
  if (*buf) return buf;

  return NULL;
}

int GetKeyNameTextUTF8(LONG lParam, LPTSTR lpString, int nMaxCount)
{
  if (!lpString) return 0;
  if (nMaxCount>0 AND_IS_NOT_WIN9X)
  {
    WIDETOMB_ALLOC(wbuf, nMaxCount);
    if (wbuf)
    {
      const int v = GetKeyNameTextW(lParam,wbuf,(int) (wbuf_size/sizeof(WCHAR)));

      if (v)
      {
        lpString[0]=0;
        if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpString,nMaxCount,NULL,NULL))
          lpString[nMaxCount-1]=0;
      }
      WIDETOMB_FREE(wbuf);

      return v ? (int)strlen(lpString) : 0;
    }
  }
  return GetKeyNameTextA(lParam,lpString,nMaxCount);
}

HINSTANCE ShellExecuteUTF8(HWND hwnd, LPCTSTR lpOp, LPCTSTR lpFile, LPCTSTR lpParm, LPCTSTR lpDir, INT nShowCmd)
{
  // wdl_utf8_correctlongpath?
  if (IS_NOT_WIN9X_AND (WDL_HasUTF8(lpOp)||WDL_HasUTF8(lpFile)||WDL_HasUTF8(lpParm)||WDL_HasUTF8(lpDir)))
  {
    DWORD sz;
    WCHAR *p1=lpOp ? WDL_UTF8ToWC(lpOp,0,0,&sz) : NULL;
    WCHAR *p2=lpFile ? WDL_UTF8ToWC(lpFile,0,0,&sz) : NULL;
    WCHAR *p3=lpParm ? WDL_UTF8ToWC(lpParm,0,0,&sz) : NULL;
    WCHAR *p4=lpDir ? WDL_UTF8ToWC(lpDir,0,0,&sz) : NULL;
    HINSTANCE rv= p2 ? ShellExecuteW(hwnd,p1,p2,p3,p4,nShowCmd) : NULL;
    free(p1);
    free(p2);
    free(p3);
    free(p4);
    return rv;
  }
  return ShellExecuteA(hwnd,lpOp,lpFile,lpParm,lpDir,nShowCmd);
}

BOOL GetUserNameUTF8(LPTSTR lpString, LPDWORD nMaxCount)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!lpString||!nMaxCount)) return FALSE;
#endif
  if (IS_NOT_WIN9X_AND lpString && nMaxCount)
  {
    WIDETOMB_ALLOC(wtmp,*nMaxCount);
    if (wtmp)
    {
      DWORD sz=(DWORD)(wtmp_size/sizeof(WCHAR));
      BOOL r = GetUserNameW(wtmp, &sz);
      if (r && (!*nMaxCount || (!WideCharToMultiByte(CP_UTF8,0,wtmp,-1,lpString,*nMaxCount,NULL,NULL) && GetLastError()==ERROR_INSUFFICIENT_BUFFER)))
      {
        if (*nMaxCount>0) lpString[*nMaxCount-1]=0;
        *nMaxCount=(int)wcslen(wtmp)+1;
        r=FALSE;
      }
      else
      {
        *nMaxCount=sz;
      }
      WIDETOMB_FREE(wtmp);
      return r;
    }
  }
  return GetUserNameA(lpString, nMaxCount);
}

BOOL GetComputerNameUTF8(LPTSTR lpString, LPDWORD nMaxCount)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!lpString||!nMaxCount)) return 0;
#endif
  if (IS_NOT_WIN9X_AND lpString && nMaxCount)
  {
    WIDETOMB_ALLOC(wtmp,*nMaxCount);
    if (wtmp)
    {
      DWORD sz=(DWORD)(wtmp_size/sizeof(WCHAR));
      BOOL r = GetComputerNameW(wtmp, &sz);
      if (r && (!*nMaxCount || (!WideCharToMultiByte(CP_UTF8,0,wtmp,-1,lpString,*nMaxCount,NULL,NULL) && GetLastError()==ERROR_INSUFFICIENT_BUFFER)))
      {
        if (*nMaxCount>0) lpString[*nMaxCount-1]=0;
        *nMaxCount=(int)wcslen(wtmp)+1;
        r=FALSE;
      }
      else
      {
        *nMaxCount=sz;
      }
      WIDETOMB_FREE(wtmp);
      return r;
    }
  }
  return GetComputerNameA(lpString, nMaxCount);
}

#define MBTOWIDE_NULLOK(symbase, src) \
                int symbase##_size; \
                WCHAR symbase##_buf[256]; \
                WCHAR *symbase = (src)==NULL ? NULL : ((symbase##_size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,-1,NULL,0)) >= 248 ? (WCHAR *)malloc(symbase##_size * sizeof(WCHAR) + 10) : symbase##_buf); \
                int symbase##_ok = symbase ? (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,-1,symbase,symbase##_size < 1 ? 256 : symbase##_size)) : (src)==NULL


// these only bother using Wide versions if the filename has wide chars
// (for now)
#define PROFILESTR_COMMON_BEGIN(ret_type) \
  if (IS_NOT_WIN9X_AND fnStr && WDL_HasUTF8(fnStr)) \
  { \
    BOOL do_rv = 0; \
    ret_type rv = 0; \
    MBTOWIDE(wfn,fnStr); \
    MBTOWIDE_NULLOK(wapp,appStr); \
    MBTOWIDE_NULLOK(wkey,keyStr); \
    if (wfn_ok && wapp_ok && wkey_ok) {

#define PROFILESTR_COMMON_END } /* wfn_ok etc */ \
    MBTOWIDE_FREE(wfn); \
    MBTOWIDE_FREE(wapp); \
    MBTOWIDE_FREE(wkey); \
    if (do_rv) return rv; \
  } /* if has utf8 etc */ \

UINT GetPrivateProfileIntUTF8(LPCTSTR appStr, LPCTSTR keyStr, INT def, LPCTSTR fnStr)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!fnStr || !keyStr || !appStr)) return 0;
#endif

  PROFILESTR_COMMON_BEGIN(UINT)

  rv = GetPrivateProfileIntW(wapp,wkey,def,wfn);
  do_rv = 1;

  PROFILESTR_COMMON_END
  return GetPrivateProfileIntA(appStr,keyStr,def,fnStr);
}

DWORD GetPrivateProfileStringUTF8(LPCTSTR appStr, LPCTSTR keyStr, LPCTSTR defStr, LPTSTR retStr, DWORD nSize, LPCTSTR fnStr)
{
  PROFILESTR_COMMON_BEGIN(DWORD)
  MBTOWIDE_NULLOK(wdef, defStr);

  WIDETOMB_ALLOC(buf, nSize);

  if (wdef_ok && buf)
  {
    const DWORD nullsz = (!wapp || !wkey) ? 2 : 1;
    rv = GetPrivateProfileStringW(wapp,wkey,wdef,buf,(DWORD) (buf_size / sizeof(WCHAR)),wfn);
    if (nSize<=nullsz)
    {
      memset(retStr,0,nSize);
      rv=0;
    }
    else
    {
      // rv does not include null character(s)
      if (rv>0) rv = WideCharToMultiByte(CP_UTF8,0,buf,rv,retStr,nSize-nullsz,NULL,NULL);
      if (rv > nSize-nullsz) rv=nSize-nullsz;
      memset(retStr + rv,0,nullsz);
    }
    do_rv = 1;
  }
  
  MBTOWIDE_FREE(wdef);
  WIDETOMB_FREE(buf);
  PROFILESTR_COMMON_END
  return GetPrivateProfileStringA(appStr,keyStr,defStr,retStr,nSize,fnStr);
}

BOOL WritePrivateProfileStringUTF8(LPCTSTR appStr, LPCTSTR keyStr, LPCTSTR str, LPCTSTR fnStr)
{
  PROFILESTR_COMMON_BEGIN(BOOL)
  MBTOWIDE_NULLOK(wval, str);
  if (wval_ok)
  {
    rv = WritePrivateProfileStringW(wapp,wkey,wval,wfn);
    do_rv = 1;
  }
  MBTOWIDE_FREE(wval);

  PROFILESTR_COMMON_END
  return WritePrivateProfileStringA(appStr,keyStr,str,fnStr);
}

BOOL GetPrivateProfileStructUTF8(LPCTSTR appStr, LPCTSTR keyStr, LPVOID pStruct, UINT uSize, LPCTSTR fnStr)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!fnStr || !keyStr || !appStr)) return 0;
#endif
  PROFILESTR_COMMON_BEGIN(BOOL)

  rv = GetPrivateProfileStructW(wapp,wkey,pStruct,uSize,wfn);
  do_rv = 1;

  PROFILESTR_COMMON_END
  return GetPrivateProfileStructA(appStr,keyStr,pStruct,uSize,fnStr);
}

BOOL WritePrivateProfileStructUTF8(LPCTSTR appStr, LPCTSTR keyStr, LPVOID pStruct, UINT uSize, LPCTSTR fnStr)
{
#ifdef _DEBUG
  if (WDL_NOT_NORMALLY(!fnStr || !keyStr || !appStr)) return 0;
#endif

  PROFILESTR_COMMON_BEGIN(BOOL)

  rv = WritePrivateProfileStructW(wapp,wkey,pStruct,uSize,wfn);
  do_rv = 1;

  PROFILESTR_COMMON_END
  return WritePrivateProfileStructA(appStr,keyStr,pStruct,uSize,fnStr);
}


#undef PROFILESTR_COMMON_BEGIN
#undef PROFILESTR_COMMON_END


BOOL CreateProcessUTF8(LPCTSTR lpApplicationName,
  LPTSTR lpCommandLine, 
  LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
  BOOL bInheritHandles,
  DWORD dwCreationFlags, LPVOID lpEnvironment,  // pointer to new environment block
  LPCTSTR lpCurrentDirectory,
  LPSTARTUPINFO lpStartupInfo,
  LPPROCESS_INFORMATION lpProcessInformation )
{
  // wdl_utf8_correctlongpath?

  // special case ver
  if (IS_NOT_WIN9X_AND (
        WDL_HasUTF8(lpApplicationName) ||
        WDL_HasUTF8(lpCommandLine) ||
        WDL_HasUTF8(lpCurrentDirectory)
        )
      )
  {
    MBTOWIDE_NULLOK(appn, lpApplicationName);
    MBTOWIDE_NULLOK(cmdl, lpCommandLine);
    MBTOWIDE_NULLOK(curd, lpCurrentDirectory);

    if (appn_ok && cmdl_ok && curd_ok)
    {
      BOOL rv;
      WCHAR *free1=NULL, *free2=NULL;
      char *save1=NULL, *save2=NULL;

      if (lpStartupInfo && lpStartupInfo->cb >= sizeof(STARTUPINFO))
      {
        if (lpStartupInfo->lpDesktop)
          lpStartupInfo->lpDesktop = (char *) (free1 = WDL_UTF8ToWC(save1 = lpStartupInfo->lpDesktop,FALSE,0,NULL));
        if (lpStartupInfo->lpTitle)
          lpStartupInfo->lpTitle = (char*) (free2 = WDL_UTF8ToWC(save2 = lpStartupInfo->lpTitle,FALSE,0,NULL));
      }

      rv=CreateProcessW(appn,cmdl,lpProcessAttributes,lpThreadAttributes,bInheritHandles,dwCreationFlags,
        lpEnvironment,curd,(STARTUPINFOW*)lpStartupInfo,lpProcessInformation);

      if (lpStartupInfo && lpStartupInfo->cb >= sizeof(STARTUPINFO))
      {
        lpStartupInfo->lpDesktop = save1;
        lpStartupInfo->lpTitle = save2;
        free(free1);
        free(free2);
      }

      MBTOWIDE_FREE(appn);
      MBTOWIDE_FREE(cmdl);
      MBTOWIDE_FREE(curd);
      return rv;
    }
    MBTOWIDE_FREE(appn);
    MBTOWIDE_FREE(cmdl);
    MBTOWIDE_FREE(curd);
  }

  return CreateProcessA(lpApplicationName,lpCommandLine,lpProcessAttributes,lpThreadAttributes,bInheritHandles,dwCreationFlags,lpEnvironment,lpCurrentDirectory,lpStartupInfo,lpProcessInformation);
}


#if (defined(WDL_WIN32_UTF8_IMPL_NOTSTATIC) || defined(WDL_WIN32_UTF8_IMPL_STATICHOOKS)) && !defined(WDL_WIN32_UTF8_NO_UI_IMPL)

static LRESULT WINAPI cb_newProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  struct wdl_utf8_hook* hook = wdl_utf8_hook_get(hwnd);
  WNDPROC oldproc = hook ? hook->old_proc : NULL;
  if (!oldproc) return 0;

  WNDPROC oldprocW = (hook && hook->old_proc_wide) ? hook->old_proc_wide : oldproc;

  if (msg==WM_NCDESTROY)
  {
    wdl_utf8_swap_window_proc(hwnd, oldproc, NULL);
    wdl_utf8_hook_detach(hwnd);
  }
  else if (msg == CB_ADDSTRING ||
           msg == CB_INSERTSTRING ||
           msg == CB_FINDSTRINGEXACT ||
           msg == CB_FINDSTRING ||
           msg == LB_ADDSTRING ||
           msg == LB_INSERTSTRING)
  {
    char *str=(char*)lParam;
    if (lParam && WDL_HasUTF8(str))
    {
      MBTOWIDE(wbuf,str);
      if (wbuf_ok)
      {
        LRESULT rv=CallWindowProcW(oldprocW,hwnd,msg,wParam,(LPARAM)wbuf);
        MBTOWIDE_FREE(wbuf);
        return rv;
      }

      MBTOWIDE_FREE(wbuf);
    }
  }
  else if ((msg == CB_GETLBTEXT || msg == LB_GETTEXTUTF8) && lParam)
  {
    LRESULT l = CallWindowProcW(oldprocW,hwnd,msg == CB_GETLBTEXT ? CB_GETLBTEXTLEN : LB_GETTEXTLEN,wParam,0);

    if (l != CB_ERR)
    {
      WIDETOMB_ALLOC(tmp,l+1);
      if (tmp)
      {
        LRESULT rv=CallWindowProcW(oldprocW,hwnd,msg & ~0x8000,wParam,(LPARAM)tmp);
        if (rv>=0)
        {
          *(char *)lParam=0;
          rv=WideCharToMultiByte(CP_UTF8,0,tmp,-1,(char *)lParam,((int)l)*4 + 32,NULL,NULL);
          if (rv>0) rv--;
        }
        WIDETOMB_FREE(tmp);

        return rv;
      }
    }
  }
  else if (msg == CB_GETLBTEXTLEN || msg == LB_GETTEXTLENUTF8)
  {
    return CallWindowProcW(oldprocW,hwnd,msg & ~0x8000,wParam,lParam) * 4 + 32; // make sure caller allocates a lot extra
  }

  return CallWindowProc(oldproc,hwnd,msg,wParam,lParam);
}
static int compareUTF8ToFilteredASCII(const char *utf, const char *ascii)
{
  for (;;)
  {
    unsigned char c1 = (unsigned char)*ascii++;
    int c2;
    if (!*utf || !c1) return *utf || c1;
    utf += wdl_utf8_parsechar(utf, &c2);
    if (c1 != c2)
    {
      if (c2 < 128) return 1; // if not UTF-8 character, strings differ
      if (c1 != '?') return 1; // if UTF-8 and ASCII is not ?, strings differ
    }
  }
}

static LRESULT WINAPI cbedit_newProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  struct wdl_utf8_hook* hook = wdl_utf8_hook_get(hwnd);
  WNDPROC oldproc = hook ? hook->old_proc : NULL;
  if (!oldproc) return 0;

  WNDPROC oldprocW = (hook && hook->old_proc_wide) ? hook->old_proc_wide : oldproc;

  if (msg==WM_NCDESTROY)
  {
    wdl_utf8_swap_window_proc(hwnd, oldproc, NULL);
    wdl_utf8_hook_detach(hwnd);
  }
  else if (msg == WM_SETTEXT && lParam && *(const char *)lParam)
  {
    HWND par = GetParent(hwnd);

    int sel = (int) SendMessage(par,CB_GETCURSEL,0,0);
    if (sel>=0)
    {
      const int len = (int) SendMessage(par,CB_GETLBTEXTLEN,sel,0);
      char tmp[1024], *p = (len+1) <= sizeof(tmp) ? tmp : (char*)calloc(len+1,1);
      if (p)
      {
        SendMessage(par,CB_GETLBTEXT,sel,(LPARAM)p);
        if (WDL_DetectUTF8(p)>0 && !compareUTF8ToFilteredASCII(p,(const char *)lParam))
        {
          MBTOWIDE(wbuf,p);
          if (wbuf_ok)
          {
            LRESULT ret = CallWindowProcW(oldprocW,hwnd,msg,wParam,(LPARAM)wbuf);
            MBTOWIDE_FREE(wbuf);
            if (p != tmp) free(p);
            return ret;
          }
          MBTOWIDE_FREE(wbuf);
        }
        if (p != tmp) free(p);
      }
    }
  }

  return CallWindowProc(oldproc,hwnd,msg,wParam,lParam);
}

void WDL_UTF8_HookListBoxCtx(WdlWindowsSandboxContext* context, HWND h)
{
  if (WDL_NOT_NORMALLY(!h) ||
    #ifdef WDL_SUPPORT_WIN9X
      GetVersion()>=0x80000000||
    #endif
    wdl_utf8_hook_get(h)) return;

  struct wdl_utf8_hook* hook = wdl_utf8_hook_create(context, h);
  if (!hook)
  {
    return;
  }

  hook->old_proc_wide = (WNDPROC) GetWindowLongPtrW(h,GWLP_WNDPROC);
  DWORD swap_error = 0;
  hook->old_proc = wdl_utf8_swap_window_proc(h, cb_newProc, &swap_error);
  if (!hook->old_proc && swap_error != 0)
  {
    wdl_utf8_hook_destroy(hook);
    return;
  }

  if (!wdl_utf8_hook_attach(h, hook))
  {
    wdl_utf8_swap_window_proc(h, hook->old_proc, NULL);
    wdl_utf8_hook_destroy(hook);
  }
}

void WDL_UTF8_HookComboBoxCtx(WdlWindowsSandboxContext* context, HWND h)
{
  WDL_UTF8_HookListBoxCtx(context, h);
  ATOM* combo_atom_slot = wdl_utf8_combo_atom_slot_for(context);
  if (h && combo_atom_slot && !*combo_atom_slot) *combo_atom_slot = (ATOM)GetClassWord(h,GCW_ATOM);

  if (h)
  {
    h = FindWindowEx(h,NULL,"Edit",NULL);
    if (h && !wdl_utf8_hook_get(h))
    {
      struct wdl_utf8_hook* edit_hook = wdl_utf8_hook_create(context, h);
      if (!edit_hook)
      {
        return;
      }

      edit_hook->old_proc_wide = (WNDPROC) GetWindowLongPtrW(h,GWLP_WNDPROC);
      DWORD swap_error = 0;
      edit_hook->old_proc = wdl_utf8_swap_window_proc(h, cbedit_newProc, &swap_error);
      if (!edit_hook->old_proc && swap_error != 0)
      {
        wdl_utf8_hook_destroy(edit_hook);
        return;
      }

      if (!wdl_utf8_hook_attach(h, edit_hook))
      {
        wdl_utf8_swap_window_proc(h, edit_hook->old_proc, NULL);
        wdl_utf8_hook_destroy(edit_hook);
      }
    }
  }
}

static LRESULT WINAPI tc_newProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  struct wdl_utf8_hook* hook = wdl_utf8_hook_get(hwnd);
  WNDPROC oldproc = hook ? hook->old_proc : NULL;
  if (!oldproc) return 0;

  if (msg==WM_NCDESTROY)
  {
    wdl_utf8_swap_window_proc(hwnd, oldproc, NULL);
    wdl_utf8_hook_detach(hwnd);
  }
  else if (msg == TCM_INSERTITEMA)
  {
    LPTCITEM pItem = (LPTCITEM) lParam;
    char *str;
    if (pItem && (str=pItem->pszText) && (pItem->mask&TCIF_TEXT) && WDL_HasUTF8(str))
    {
      MBTOWIDE(wbuf,str);
      if (wbuf_ok)
      {
        LRESULT rv;
        pItem->pszText=(char*)wbuf; // set new buffer
        rv=CallWindowProc(oldproc,hwnd,TCM_INSERTITEMW,wParam,lParam);
        pItem->pszText = str; // restore old pointer
        MBTOWIDE_FREE(wbuf);
        return rv;
      }

      MBTOWIDE_FREE(wbuf);
    }
  }


  return CallWindowProc(oldproc,hwnd,msg,wParam,lParam);
}


static LRESULT WINAPI tv_newProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  struct wdl_utf8_hook* hook = wdl_utf8_hook_get(hwnd);
  WNDPROC oldproc = hook ? hook->old_proc : NULL;
  if (!oldproc) return 0;

  if (msg==WM_NCDESTROY)
  {
    wdl_utf8_swap_window_proc(hwnd, oldproc, NULL);
    wdl_utf8_hook_detach(hwnd);
  }
  else if (msg == TVM_INSERTITEMA || msg == TVM_SETITEMA)
  {
    LPTVITEM pItem = msg == TVM_INSERTITEMA ? &((LPTVINSERTSTRUCT)lParam)->item : (LPTVITEM) lParam;
    char *str;
    if (pItem && (str=pItem->pszText) && (pItem->mask&TVIF_TEXT) && WDL_HasUTF8(str))
    {
      MBTOWIDE(wbuf,str);
      if (wbuf_ok)
      {
        LRESULT rv;
        pItem->pszText=(char*)wbuf; // set new buffer
        rv=CallWindowProc(oldproc,hwnd,msg == TVM_INSERTITEMA ? TVM_INSERTITEMW : TVM_SETITEMW,wParam,lParam);
        pItem->pszText = str; // restore old pointer
        MBTOWIDE_FREE(wbuf);
        return rv;
      }

      MBTOWIDE_FREE(wbuf);
    }
  }
  else if (msg==TVM_GETITEMA)
  {
    LPTVITEM pItem = (LPTVITEM) lParam;
    char *obuf;
    if (pItem && (pItem->mask & TVIF_TEXT) && (obuf=pItem->pszText) && pItem->cchTextMax > 3)
    {
      WIDETOMB_ALLOC(wbuf,pItem->cchTextMax);
      if (wbuf)
      {
        LRESULT rv;
        int oldsz=pItem->cchTextMax;
        *wbuf=0;
        *obuf=0;
        pItem->cchTextMax=(int) (wbuf_size/sizeof(WCHAR));
        pItem->pszText = (char *)wbuf;
        rv=CallWindowProc(oldproc,hwnd,TVM_GETITEMW,wParam,lParam);

        if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,obuf,oldsz,NULL,NULL))
          obuf[oldsz-1]=0;

        pItem->cchTextMax=oldsz;
        pItem->pszText=obuf;
        WIDETOMB_FREE(wbuf);

        if (obuf[0]) return rv;
      }
    }
  }

  return CallWindowProc(oldproc,hwnd,msg,wParam,lParam);
}

static LRESULT WINAPI lv_newProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  struct wdl_utf8_hook* hook = wdl_utf8_hook_get(hwnd);
  WNDPROC oldproc = hook ? hook->old_proc : NULL;
  if (!oldproc) return 0;

  struct lv_tmpbuf_state *buf = hook ? hook->list_view_buffer : NULL;

  if (msg==WM_NCDESTROY)
  {
    wdl_utf8_swap_window_proc(hwnd, oldproc, NULL);
    wdl_utf8_hook_detach(hwnd);
  }
  else if (msg == LVM_INSERTCOLUMNA || msg==LVM_SETCOLUMNA)
  {
    LPLVCOLUMNA pCol = (LPLVCOLUMNA) lParam;
    char *str;
    if (pCol && (str=pCol->pszText) && (pCol->mask & LVCF_TEXT) && WDL_HasUTF8(str))
    {
      MBTOWIDE(wbuf,str);
      if (wbuf_ok)
      {
        LRESULT rv;
        pCol->pszText=(char*)wbuf; // set new buffer
        rv=CallWindowProc(oldproc,hwnd,msg==LVM_INSERTCOLUMNA?LVM_INSERTCOLUMNW:LVM_SETCOLUMNW,wParam,lParam);
        pCol->pszText = str; // restore old pointer
        MBTOWIDE_FREE(wbuf);
        return rv;
      }

    }
  }
  else if (msg == LVM_GETCOLUMNA)
  {
    LPLVCOLUMNA pCol = (LPLVCOLUMNA) lParam;
    char *str;
    if (pCol && (str=pCol->pszText) && pCol->cchTextMax>0 &&
        (pCol->mask & LVCF_TEXT))
    {
      WIDETOMB_ALLOC(wbuf, pCol->cchTextMax);
      if (wbuf)
      {
        LRESULT rv;
        pCol->pszText=(char*)wbuf; // set new buffer
        rv=CallWindowProc(oldproc,hwnd,LVM_GETCOLUMNW,wParam,lParam);
        if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,str,pCol->cchTextMax,NULL,NULL))
          str[pCol->cchTextMax-1]=0;

        pCol->pszText = str; // restore old pointer
        WIDETOMB_FREE(wbuf);
        return rv;
      }
    }
  }
  else if (msg == LVM_INSERTITEMA || msg == LVM_SETITEMA || msg == LVM_SETITEMTEXTA)
  {
    LPLVITEMA pItem = (LPLVITEMA) lParam;
    char *str;
    if (pItem &&
        pItem->pszText != LPSTR_TEXTCALLBACK &&
        (str=pItem->pszText) &&
        (msg==LVM_SETITEMTEXTA || (pItem->mask&LVIF_TEXT)) &&
        WDL_HasUTF8(str))
    {
      MBTOWIDE(wbuf,str);
      if (wbuf_ok)
      {
        LRESULT rv;
        pItem->pszText=(char*)wbuf; // set new buffer
        rv=CallWindowProc(oldproc,hwnd,msg == LVM_INSERTITEMA ? LVM_INSERTITEMW : msg == LVM_SETITEMA ? LVM_SETITEMW : LVM_SETITEMTEXTW,wParam,lParam);
        pItem->pszText = str; // restore old pointer
        MBTOWIDE_FREE(wbuf);
        return rv;
      }

      MBTOWIDE_FREE(wbuf);
    }
  }
  else if (msg==LVM_GETITEMA||msg==LVM_GETITEMTEXTA)
  {
    LPLVITEMA pItem = (LPLVITEMA) lParam;
    char *obuf;
    if (pItem && (msg == LVM_GETITEMTEXTA || (pItem->mask & LVIF_TEXT)) && (obuf=pItem->pszText) && pItem->cchTextMax > 3)
    {
      WIDETOMB_ALLOC(wbuf,pItem->cchTextMax);
      if (wbuf)
      {
        LRESULT rv;
        int oldsz=pItem->cchTextMax;
        *wbuf=0;
        *obuf=0;
        pItem->cchTextMax=(int) (wbuf_size/sizeof(WCHAR));
        pItem->pszText = (char *)wbuf;
        rv=CallWindowProc(oldproc,hwnd,msg==LVM_GETITEMTEXTA ? LVM_GETITEMTEXTW : LVM_GETITEMW,wParam,lParam);

        if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,obuf,oldsz,NULL,NULL))
          obuf[oldsz-1]=0;

        pItem->cchTextMax=oldsz;
        pItem->pszText=obuf;
        WIDETOMB_FREE(wbuf);

        if (obuf[0]) return rv;
      }
    }
  }

  return CallWindowProc(oldproc,hwnd,msg,wParam,lParam);
}

void WDL_UTF8_HookListViewCtx(WdlWindowsSandboxContext* context, HWND h)
{
  if (WDL_NOT_NORMALLY(!h) ||
    #ifdef WDL_SUPPORT_WIN9X
      GetVersion()>=0x80000000||
    #endif
    wdl_utf8_hook_get(h)) return;

  struct wdl_utf8_hook* hook = wdl_utf8_hook_create(context, h);
  if (!hook)
  {
    return;
  }

  DWORD swap_error = 0;
  hook->old_proc = wdl_utf8_swap_window_proc(h, lv_newProc, &swap_error);
  if (!hook->old_proc && swap_error != 0)
  {
    wdl_utf8_hook_destroy(hook);
    return;
  }

  hook->list_view_buffer = (struct lv_tmpbuf_state*) calloc(1, sizeof(struct lv_tmpbuf_state));
  if (!hook->list_view_buffer)
  {
    wdl_utf8_swap_window_proc(h, hook->old_proc, NULL);
    wdl_utf8_hook_destroy(hook);
    return;
  }

  if (!wdl_utf8_hook_attach(h, hook))
  {
    wdl_utf8_swap_window_proc(h, hook->old_proc, NULL);
    wdl_utf8_hook_destroy(hook);
  }
}

void WDL_UTF8_HookTreeViewCtx(WdlWindowsSandboxContext* context, HWND h)
{
  if (WDL_NOT_NORMALLY(!h) ||
    #ifdef WDL_SUPPORT_WIN9X
      GetVersion()>=0x80000000||
    #endif
    wdl_utf8_hook_get(h)) return;

  struct wdl_utf8_hook* hook = wdl_utf8_hook_create(context, h);
  if (!hook)
  {
    return;
  }

  DWORD swap_error = 0;
  hook->old_proc = wdl_utf8_swap_window_proc(h, tv_newProc, &swap_error);
  if (!hook->old_proc && swap_error != 0)
  {
    wdl_utf8_hook_destroy(hook);
    return;
  }

  if (!wdl_utf8_hook_attach(h, hook))
  {
    wdl_utf8_swap_window_proc(h, hook->old_proc, NULL);
    wdl_utf8_hook_destroy(hook);
  }
}

void WDL_UTF8_HookTabCtrlCtx(WdlWindowsSandboxContext* context, HWND h)
{
  if (WDL_NOT_NORMALLY(!h) ||
    #ifdef WDL_SUPPORT_WIN9X
      GetVersion()>=0x80000000||
    #endif
    wdl_utf8_hook_get(h)) return;

  struct wdl_utf8_hook* hook = wdl_utf8_hook_create(context, h);
  if (!hook)
  {
    return;
  }

  DWORD swap_error = 0;
  hook->old_proc = wdl_utf8_swap_window_proc(h, tc_newProc, &swap_error);
  if (!hook->old_proc && swap_error != 0)
  {
    wdl_utf8_hook_destroy(hook);
    return;
  }

  if (!wdl_utf8_hook_attach(h, hook))
  {
    wdl_utf8_swap_window_proc(h, hook->old_proc, NULL);
    wdl_utf8_hook_destroy(hook);
  }
}

void WDL_UTF8_ListViewConvertDispInfoToW(void *_di)
{
  NMLVDISPINFO *di = (NMLVDISPINFO *)_di;
  if (di && (di->item.mask & LVIF_TEXT) && di->item.pszText && di->item.cchTextMax>0)
  {
    const char *src = (const char *)di->item.pszText;
    const size_t src_sz = strlen(src);
    struct wdl_utf8_hook* hook = wdl_utf8_hook_get(di->hdr.hwndFrom);
    struct lv_tmpbuf_state *sb = hook ? hook->list_view_buffer : NULL;

    WdlWindowsSandboxContext* context = NULL;
    int using_context_buf = 0;
#if WDL_UTF8_HAS_SANDBOX_CONTEXT
    context = hook ? hook->context : wdl_utf8_current_context();
    if (!context)
    {
      context = (WdlWindowsSandboxContext*) GetPropA(di->hdr.hwndFrom, WDL_UTF8_SandboxContextPropertyName());
    }

    struct lv_tmpbuf_state context_buf = {0};
    if (!sb && context)
    {
      context_buf.buf = (WCHAR*) context->utf8_hooks.list_view_tmpbuf;
      context_buf.buf_sz = context->utf8_hooks.list_view_tmpbuf_size;
      sb = &context_buf;
      using_context_buf = 1;
    }
#else
    (void) context;
#endif

    struct lv_tmpbuf_state temp_buf = {0};
    int using_temp_buf = 0;
    if (!sb)
    {
      sb = &temp_buf;
      using_temp_buf = 1;
    }

    if (!sb->buf || sb->buf_sz < (int) src_sz)
    {
      const int newsz = (int) wdl_min(src_sz * 2 + 256, 0x7fffFFFF);
      if (!sb->buf || sb->buf_sz < newsz)
      {
        free(sb->buf);
        sb->buf = (WCHAR *)malloc((sb->buf_sz = newsz) * sizeof(WCHAR));
      }
    }

    if (WDL_NOT_NORMALLY(!sb->buf))
    {
      di->item.pszText = (char*)L"";
    }
    else
    {
      di->item.pszText = (char*)sb->buf;

      if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,-1,sb->buf,sb->buf_sz))
      {
        if (WDL_NOT_NORMALLY(GetLastError()==ERROR_INSUFFICIENT_BUFFER))
        {
          sb->buf[sb->buf_sz-1] = 0;
        }
        else
        {
          if (!MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,src,-1,sb->buf,sb->buf_sz))
            sb->buf[sb->buf_sz-1] = 0;
        }
      }
    }

#if WDL_UTF8_HAS_SANDBOX_CONTEXT
    if (using_context_buf && context)
    {
      context->utf8_hooks.list_view_tmpbuf = (wchar_t*) sb->buf;
      context->utf8_hooks.list_view_tmpbuf_size = sb->buf_sz;
    }
#endif

    if (using_temp_buf)
    {
      free(sb->buf);
      sb->buf = NULL;
      sb->buf_sz = 0;
    }
  }
}

#endif

#ifdef __cplusplus
};
#endif

#endif

#endif //_WIN32
