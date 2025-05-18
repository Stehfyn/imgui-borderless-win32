#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include <assert.h>
#include <Atlthunk.h>

typedef void(CALLBACK* DRAWPROC)(HWND hWnd);

#define BCSTHUNK_TIMERPROC      (0)

#define Borderless_Thunk(hWnd, ThunkOffset, Proc, FirstParameter)                       \
           do { AtlThunk_InitData((AtlThunkData_t*)GetWindowLongPtr(hWnd, ThunkOffset), \
                   (void*)(uintptr_t)Proc, (size_t)(uintptr_t)FirstParameter); } while(0)

HWND CALLBACK
CreateBorderlessWindow(
    DWORD dwExStyle,
    DWORD dwStyle,
    int nWidth,
    int nHeight,
    DRAWPROC lpfnDrawProc
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