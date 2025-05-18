#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windef.h>

typedef PROC(APIENTRY* PFNWGLGETPROCADDRESS)(LPCSTR);
typedef BOOL(WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC) (HDC hdc, const int* piAttribIList, const FLOAT* pfAttribFList, UINT nMaxFormats, int* piFormats, UINT* nNumFormats);
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC) (HDC hDC, HGLRC hShareContext, const int* attribList);
typedef BOOL(WINAPI* PFNWGLMAKECONTEXTCURRENTARBPROC) (HDC hDrawDC, HDC hReadDC, HGLRC hglrc);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC) (int interval);

extern PFNWGLGETPROCADDRESS              __wglGetProcAddress;
extern PFNWGLCHOOSEPIXELFORMATARBPROC    __wglChoosePixelFormatARB;
extern PFNWGLCREATECONTEXTATTRIBSARBPROC __wglCreateContextAttribsARB;
extern PFNWGLMAKECONTEXTCURRENTARBPROC   __wglMakeContextCurrentARB;
extern PFNWGLSWAPINTERVALEXTPROC         __wglSwapIntervalEXT;

#define wglGetProcAddress           __wglGetProcAddress
#define wglChoosePixelFormatARB     __wglChoosePixelFormatARB
#define wglCreateContextAttribsARB  __wglCreateContextAttribsARB
#define wglMakeContextCurrentARB    __wglMakeContextCurrentARB
#define wglSwapIntervalEXT          __wglSwapIntervalEXT

int WINAPI wglGetPixelFormat(HDC, const int*);

EXTERN_C
BOOL CFORCEINLINE CALLBACK
wglWaitForVerticalBlank(
    HWND hWnd
    );