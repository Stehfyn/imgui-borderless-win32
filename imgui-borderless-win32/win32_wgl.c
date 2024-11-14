#include "win32_wgl.h"
#include <tchar.h>
#include <GL\GL.h>
#include <GL\GLU.h>
//#include <GL\glext.h>
#include <GL\wglext.h>
#define RETURN_FALSE_IF_NULL(p) if (!p) return FALSE;

static BOOL                       s_initializedWGL           = FALSE;
PFNWGLCHOOSEPIXELFORMATARBPROC    wglChoosePixelFormatARB    = NULL;
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;
PFNWGLSWAPINTERVALEXTPROC         wglSwapIntervalEXT         = NULL;
PFNWGLGETSWAPINTERVALEXTPROC      wglGetSwapIntervalEXT      = NULL;

static BOOL
win32_wgl_init(
    VOID)
{
    if (!s_initializedWGL)
    {
        wglChoosePixelFormatARB    = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
        wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
        wglSwapIntervalEXT         = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
        wglGetSwapIntervalEXT      = (PFNWGLGETSWAPINTERVALEXTPROC)wglGetProcAddress("wglGetSwapIntervalEXT");
        RETURN_FALSE_IF_NULL(wglChoosePixelFormatARB);
        RETURN_FALSE_IF_NULL(wglCreateContextAttribsARB);
        RETURN_FALSE_IF_NULL(wglSwapIntervalEXT);
        RETURN_FALSE_IF_NULL(wglGetSwapIntervalEXT);

        s_initializedWGL = TRUE;
    }

    return TRUE;
}

INT 
win32_wgl_get_pixel_format(
    UINT msaa)
{
    HWND       hWnd;
    HDC        hDC;
    HGLRC      hRC;
    WNDCLASS   wc;
    INT        pfMSAA;

    PIXELFORMATDESCRIPTOR pfd;
    SecureZeroMemory(&wc, sizeof(WNDCLASS));
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpfnWndProc = DefWindowProc;
    wc.lpszClassName = _T("GLEW");
    if (0 == RegisterClass(&wc)) return GL_TRUE;
    hWnd = CreateWindow(_T("GLEW"), _T("GLEW"), WS_CLIPSIBLINGS | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (NULL == hWnd) return GL_TRUE;
    hDC = GetDC(hWnd);
    if (NULL == hDC) return GL_TRUE;
    ZeroMemory(&pfd, sizeof(PIXELFORMATDESCRIPTOR));
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW |    // Format Must Support Window
        PFD_SUPPORT_OPENGL |              // Format Must Support OpenGL
        PFD_SUPPORT_COMPOSITION |         // Format Must Support Composition
        PFD_GENERIC_ACCELERATED |
        PFD_DOUBLEBUFFER;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cColorBits = 32;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfMSAA = ChoosePixelFormat(hDC, &pfd);
    if (pfMSAA == 0) return GL_TRUE;
    if(0 == SetPixelFormat(hDC, pfMSAA, &pfd)) return GL_TRUE;
    hRC = wglCreateContext(hDC);
    wglMakeCurrent(hDC, hRC);
 
    win32_wgl_init();

    {
        while (msaa > 0)
        {
            UINT num_formats = 0;
            int pfAttribs[] = {
                WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
                WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
                WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
                WGL_COLOR_BITS_ARB, 32,
                WGL_DEPTH_BITS_ARB, 24,
                WGL_ALPHA_BITS_ARB, 8,
                WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
                WGL_SAMPLE_BUFFERS_ARB, GL_TRUE,
                WGL_SAMPLES_ARB, (INT)msaa,
                0
            };
            if (wglChoosePixelFormatARB(hDC, pfAttribs, NULL, 1, &pfMSAA, &num_formats))
            {
                if (num_formats > 0)
                {
                    break;
                }
            }
            msaa--;
        }
    }

    if (NULL != hRC) wglMakeCurrent(NULL, NULL);
    if (NULL != hRC) wglDeleteContext(hRC);
    if (NULL != hWnd && NULL != hDC) ReleaseDC(hWnd, hDC);
    if (NULL != hWnd) DestroyWindow(hWnd);
    UnregisterClass(_T("GLEW"), GetModuleHandle(NULL));
    return pfMSAA;
}
