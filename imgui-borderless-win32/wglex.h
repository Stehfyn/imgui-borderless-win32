#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windef.h>

typedef PROC(APIENTRY* PFNWGLGETPROCADDRESS)(LPCSTR);
typedef BOOL(WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC) (HDC hdc, const int* piAttribIList, const FLOAT* pfAttribFList, UINT nMaxFormats, int* piFormats, UINT* nNumFormats);
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC) (HDC hDC, HGLRC hShareContext, const int* attribList);
typedef BOOL(WINAPI* PFNWGLMAKECONTEXTCURRENTARBPROC) (HDC hDrawDC, HDC hReadDC, HGLRC hglrc);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC) (int interval);

typedef void (APIENTRY* MYPFNGLADDSWAPHINTRECTWINPROC) (int x, int y, int width, int height);
//typedef void (APIENTRY* PFNGLWINDOWRECTANGLESEXTPROC) (GLenum mode, GLsizei count, const GLint* box);
//typedef VOID(WINAPI* PFNWGLBLITCONTEXTFRAMEBUFFERAMDPROC) (HGLRC dstCtx, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
extern PFNWGLGETPROCADDRESS              __wglGetProcAddress;
extern PFNWGLCHOOSEPIXELFORMATARBPROC    __wglChoosePixelFormatARB;
extern PFNWGLCREATECONTEXTATTRIBSARBPROC __wglCreateContextAttribsARB;
extern PFNWGLMAKECONTEXTCURRENTARBPROC   __wglMakeContextCurrentARB;
extern PFNWGLSWAPINTERVALEXTPROC         __wglSwapIntervalEXT;
extern MYPFNGLADDSWAPHINTRECTWINPROC     __glAddSwapHintRectWIN;

#define wglGetProcAddress           __wglGetProcAddress
#define wglChoosePixelFormatARB     __wglChoosePixelFormatARB
#define wglCreateContextAttribsARB  __wglCreateContextAttribsARB
#define wglMakeContextCurrentARB    __wglMakeContextCurrentARB
#define wglSwapIntervalEXT          __wglSwapIntervalEXT
#define glAddSwapHintRectWIN        __glAddSwapHintRectWIN;

int WINAPI wglGetPixelFormat(HDC, const int*);

EXTERN_C
BOOL CFORCEINLINE CALLBACK
wglWaitForVerticalBlank(
    HWND hWnd
    );

EXTERN_C
BOOL CFORCEINLINE CALLBACK
wglCheckOcclusion(
    HWND hWnd
    );

EXTERN_C
BOOL CFORCEINLINE CALLBACK
wglWaitForVerticalBlank2(
    HWND hWnd
    );