#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef BOOL(WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC) (
    HDC hdc, 
    const int* piAttribIList, 
    const FLOAT* pfAttribFList, 
    UINT nMaxFormats, 
    int* piFormats, 
    UINT* nNumFormats);

typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC) (
    HDC hDC, 
    HGLRC hShareContext, 
    const int* attribList);

typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC) (
    int interval);

typedef int (WINAPI* PFNWGLGETSWAPINTERVALEXTPROC) (
    void);

extern PFNWGLCHOOSEPIXELFORMATARBPROC    wglChoosePixelFormatARB;
extern PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;
extern PFNWGLSWAPINTERVALEXTPROC         wglSwapIntervalEXT;
extern PFNWGLGETSWAPINTERVALEXTPROC      wglGetSwapIntervalEXT;

INT
win32_wgl_get_pixel_format(
    UINT msaa);
