#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>

typedef struct win32_window_s win32_window_t;
typedef LRESULT(*win32_wndproc_hook_t)(HWND, UINT, WPARAM, LPARAM);

struct win32_window_s
{
    HWND                 hWnd;
    TCHAR                tcClassName[256];
    win32_wndproc_hook_t msgHook;
    win32_window_t*      pThis;
};

VOID
win32_window_create(
    win32_window_t* w32Window,
    INT             width,
    INT             height,
    UINT            styleWc,
    DWORD           styleEx,
    DWORD           style);

BOOL
win32_window_pump_message_loop(
    win32_window_t* w32Window,
    LPMSG           msg,
    BOOL            pumpHwnd);

VOID
win32_window_run_message_loop(
    win32_window_t* w32Window);