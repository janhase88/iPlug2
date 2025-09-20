#pragma once

#if defined(_WIN32)

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WdlWindowsSandboxContext
{
  uint64_t instance_id;
  HINSTANCE module_handle;

  struct
  {
    ATOM combo_atom;
    char* property_prefix;
    size_t property_prefix_length;
    wchar_t* list_view_tmpbuf;
    int list_view_tmpbuf_size;
  } utf8_hooks;

  struct
  {
    int use_legacy_dialogs;
    void* dialog;
    FARPROC shell_create_item_from_path;
  } file_dialogs;

  struct
  {
    FARPROC set_thread_awareness;
    char mm_setwindowpos_state;
  } dpi;

  struct
  {
    HMENU* popup_handles;
    size_t popup_count;
    size_t popup_capacity;
  } menus;

  struct
  {
    int helper_class_registered;
    WNDPROC dialog_host_proc;
    int suppress_cursor_warp;
  } virtual_windows;

  struct
  {
    HMENU active_menu;
  } selection;
} WdlWindowsSandboxContext;

static inline uintptr_t WdlWindowsSandboxContext_Utf8PrefixKey(const WdlWindowsSandboxContext* context)
{
  if (!context)
  {
    return 0;
  }

  if (context->instance_id != 0)
  {
    return (uintptr_t) context->instance_id;
  }

  return (uintptr_t) context;
}

static inline int WdlWindowsSandboxContext_FormatUtf8PropertyPrefix(const WdlWindowsSandboxContext* context,
                                                                    const char* base,
                                                                    char* buffer,
                                                                    size_t buffer_size)
{
  if (!buffer || buffer_size == 0)
  {
    return 0;
  }

  const char* prefix_base = base ? base : "WDLUTF8Sandbox/";
  const size_t base_length = strlen(prefix_base);

  if (base_length + 2 > buffer_size)
  {
    return 0;
  }

  memcpy(buffer, prefix_base, base_length);
  size_t offset = base_length;

  uintptr_t value = WdlWindowsSandboxContext_Utf8PrefixKey(context);
  static const char kHexDigits[] = "0123456789ABCDEF";
  char digits[sizeof(uintptr_t) * 2];
  size_t digit_count = 0;

  if (value == 0)
  {
    digits[digit_count++] = '0';
  }
  else
  {
    while (value && digit_count < sizeof(digits))
    {
      digits[digit_count++] = kHexDigits[value & 0xF];
      value >>= 4;
    }

    if (value != 0)
    {
      return 0;
    }
  }

  if (offset + digit_count + 2 > buffer_size)
  {
    return 0;
  }

  while (digit_count > 0)
  {
    buffer[offset++] = digits[--digit_count];
  }

  buffer[offset++] = '/';
  buffer[offset] = '\0';
  return 1;
}

static inline void WdlWindowsSandboxContext_Init(WdlWindowsSandboxContext* context)
{
  if (!context)
  {
    return;
  }

  context->instance_id = 0;
  context->module_handle = NULL;

  context->utf8_hooks.combo_atom = 0;
  context->utf8_hooks.property_prefix = NULL;
  context->utf8_hooks.property_prefix_length = 0;
  context->utf8_hooks.list_view_tmpbuf = NULL;
  context->utf8_hooks.list_view_tmpbuf_size = 0;

  context->file_dialogs.use_legacy_dialogs = -1;
  context->file_dialogs.dialog = NULL;
  context->file_dialogs.shell_create_item_from_path = NULL;

  context->dpi.set_thread_awareness = NULL;
  context->dpi.mm_setwindowpos_state = 0;

  context->menus.popup_handles = NULL;
  context->menus.popup_count = 0;
  context->menus.popup_capacity = 0;

  context->virtual_windows.helper_class_registered = 0;
  context->virtual_windows.dialog_host_proc = NULL;
  context->virtual_windows.suppress_cursor_warp = -1;

  context->selection.active_menu = NULL;
}

static inline void WdlWindowsSandboxContext_Reset(WdlWindowsSandboxContext* context)
{
  if (!context)
  {
    return;
  }

  if (context->utf8_hooks.property_prefix)
  {
    free(context->utf8_hooks.property_prefix);
    context->utf8_hooks.property_prefix = NULL;
  }
  context->utf8_hooks.property_prefix_length = 0;

  if (context->utf8_hooks.list_view_tmpbuf)
  {
    free(context->utf8_hooks.list_view_tmpbuf);
    context->utf8_hooks.list_view_tmpbuf = NULL;
  }
  context->utf8_hooks.list_view_tmpbuf_size = 0;

  if (context->menus.popup_handles)
  {
    free(context->menus.popup_handles);
    context->menus.popup_handles = NULL;
  }
  context->menus.popup_count = 0;
  context->menus.popup_capacity = 0;

  context->file_dialogs.use_legacy_dialogs = -1;
  context->file_dialogs.dialog = NULL;
  context->file_dialogs.shell_create_item_from_path = NULL;
  context->dpi.set_thread_awareness = NULL;
  context->dpi.mm_setwindowpos_state = 0;
  context->virtual_windows.helper_class_registered = 0;
  context->virtual_windows.dialog_host_proc = NULL;
  context->virtual_windows.suppress_cursor_warp = -1;
  context->selection.active_menu = NULL;
}

static inline void WdlWindowsSandboxContext_Destroy(WdlWindowsSandboxContext* context)
{
  if (!context)
  {
    return;
  }

  WdlWindowsSandboxContext_Reset(context);
  context->instance_id = 0;
  context->module_handle = NULL;
  context->utf8_hooks.combo_atom = 0;
}

static inline int WdlWindowsSandboxContext_SetUtf8PropertyPrefix(WdlWindowsSandboxContext* context,
                                                                 const char* prefix)
{
  if (!context)
  {
    return 0;
  }

  if (!prefix || !prefix[0])
  {
    if (context->utf8_hooks.property_prefix)
    {
      free(context->utf8_hooks.property_prefix);
      context->utf8_hooks.property_prefix = NULL;
    }
    context->utf8_hooks.property_prefix_length = 0;
    return 1;
  }

  size_t length = strlen(prefix);
  char* buffer = (char*) malloc(length + 1);
  if (!buffer)
  {
    return 0;
  }

  memcpy(buffer, prefix, length);
  buffer[length] = '\0';

  if (context->utf8_hooks.property_prefix)
  {
    free(context->utf8_hooks.property_prefix);
  }

  context->utf8_hooks.property_prefix = buffer;
  context->utf8_hooks.property_prefix_length = length;
  return 1;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // defined(_WIN32)
