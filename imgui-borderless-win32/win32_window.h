#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include <assert.h>

HWND CALLBACK
CreateBorderlessWindow(
    DWORD dwExStyle,
    DWORD dwStyle,
    int nWidth,
    int nHeight
    );

LRESULT CALLBACK
WndProc(
    HWND   hWnd,
    UINT   uMsg,
    WPARAM wParam,
    LPARAM lParam
    );

BOOL
PumpMessageQueue(
  LPMSG msg);