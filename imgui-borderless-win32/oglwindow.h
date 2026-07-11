/*****************************************************************************\
*                                                                             *
* OGLWindow.h -  OpenGL Macro and control APIs                                *
*                                                                             *
\*****************************************************************************/

#ifndef _INC_OGLWINDOW
#define _INC_OGLWINDOW

#if (_MSC_VER >= 1020)
#pragma once
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winapifamily.h>
#include <Atlthunk.h>
#include <intrin.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <gl/gl.h>

/* Minimal WGL extension subset used by this project.  The Windows SDK does
 * not ship gl/wglext.h, so keep the few required ARB tokens local. */
#ifndef OGLWINDOW_WGLEXT_SUBSET
#define OGLWINDOW_WGLEXT_SUBSET

#ifndef WGL_DRAW_TO_WINDOW_ARB
#define WGL_DRAW_TO_WINDOW_ARB           0x2001
#endif
#ifndef WGL_SUPPORT_OPENGL_ARB
#define WGL_SUPPORT_OPENGL_ARB           0x2010
#endif
#ifndef WGL_PIXEL_TYPE_ARB
#define WGL_PIXEL_TYPE_ARB               0x2013
#endif
#ifndef WGL_TYPE_RGBA_ARB
#define WGL_TYPE_RGBA_ARB                0x202B
#endif

#ifndef WGL_ARB_pbuffer
#define WGL_ARB_pbuffer 1
DECLARE_HANDLE(HPBUFFERARB);
#endif

#ifndef WGL_DRAW_TO_PBUFFER_ARB
#define WGL_DRAW_TO_PBUFFER_ARB          0x202D
#endif

#ifndef WGL_BIND_TO_TEXTURE_RGBA_ARB
#define WGL_BIND_TO_TEXTURE_RGBA_ARB     0x2071
#endif
#ifndef WGL_TEXTURE_FORMAT_ARB
#define WGL_TEXTURE_FORMAT_ARB           0x2072
#endif
#ifndef WGL_TEXTURE_TARGET_ARB
#define WGL_TEXTURE_TARGET_ARB           0x2073
#endif
#ifndef WGL_TEXTURE_RGBA_ARB
#define WGL_TEXTURE_RGBA_ARB             0x2076
#endif
#ifndef WGL_TEXTURE_2D_ARB
#define WGL_TEXTURE_2D_ARB               0x207A
#endif
#ifndef WGL_FRONT_LEFT_ARB
#define WGL_FRONT_LEFT_ARB               0x2083
#endif

#endif /* OGLWINDOW_WGLEXT_SUBSET */
#ifndef NOOGLWINDOW


//
// Define API decoration for direct importing of DLL references.
//
#ifndef WINOGLWINDOWAPI
#if defined(_WIN32) && !defined(WINOGLWINDOWAPI_INLINE)
#define WINOGLWINDOWAPI 
#else
#define WINOGLWINDOWAPI FORCEINLINE
#endif
#endif // WINOGLWINDOWAPI

#ifdef __cplusplus
extern "C" {            /* Assume C declarations for C++ */
#endif /* __cplusplus */

//
// Users of this header may define any number of these constants to avoid
// the definitions of each functional group.
//
//    NOOGLWINDOW           Customizable colloquially-borderless window control.
//    NOHWNDSERVER          Customizable Async-capable HWND Server.
//
//=============================================================================

WINOGLWINDOWAPI VOID WINAPI InitOGLControls(VOID);

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//  ============================ OpenGL Window Control ============================
//---------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------

#ifndef NOOGLWINDOW
#ifdef _WIN32

#define OGLWINDOW_CLASSW       L"OGLWindow"          
#define OGLWINDOW_CLASSA       "OGLWindow"
#ifdef UNICODE
#define OGLWINDOW_CLASS        OGLWINDOW_CLASSW
#else
#define OGLWINDOW_CLASS        OGLWINDOW_CLASSA
#endif

#else
#define OGLWINDOW_CLASS        "OGLWindow"
#endif
//#define BCS_WINDOW              (WS_DLGFRAME | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_SYSMENU | WS_POPUP)
#define BCS_WINDOW              (WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_SYSMENU)

  /*
   * OpenGL window class
   */
#define WC_OGLWINDOW           (OGLWINDOW_CLASS)
#define OGLWINDOW_NO_QUIT_ON_DESTROY_PROP "OGLWindow_NoQuitOnDestroy"
#define OGLWINDOW_SECONDARY_VIEWPORT_PROP "OGLWindow_SecondaryViewport"

   //---------------------------------------------------------------------------------------
   // OpenGL Window Control Styles
   //---------------------------------------------------------------------------------------
   // begin_r_commctrl

//#define BCS_CENTERED            0x00000001L

// end_r_commctrl
//#define WM_INITD2D      (WM_USER + 0x78)
typedef void(__stdcall* RENDERPROC)(HWND hwnd);
#define OGLWindow_Create(lpszTitle, hwndP, id, dwStyle, X, Y, nWidth, nHeight, hInstance, renderproc)   \
           CreateWindow(WC_OGLWINDOW, lpszTitle,                                            \
               dwStyle, X, Y, nWidth, nHeight, hwndP, (HMENU)(id), hInstance, renderproc)

EXTERN_C NTSTATUS PFORCEINLINE WINAPI D3DKMTInitVerticalBlankEvent(HDC hdc, D3DKMT_WAITFORVERTICALBLANKEVENT* pVbe);
//WINOGLWINDOWAPI BOOL WINAPI OGLWindowPaintInit(HWND hwnd);
WINOGLWINDOWAPI HDC WINAPI BeginOGLWindowPaint(HWND hWnd);
WINOGLWINDOWAPI BOOL WINAPI EndOGLWindowPaint(HDC hDC);
WINOGLWINDOWAPI BOOL WINAPI PresentOGLWindow(HWND hWnd);
WINOGLWINDOWAPI BOOL WINAPI IsOGLWindowInSynchronousResizeRender(VOID);
WINOGLWINDOWAPI HWND WINAPI GetOGLWindowSynchronousResizeHwnd(VOID);
WINOGLWINDOWAPI BOOL WINAPI IsOGLWindowInModalSizeMove(HWND hWnd);
WINOGLWINDOWAPI VOID WINAPI MessageFiberProc(void* unused);
WINOGLWINDOWAPI LRESULT CALLBACK DefOGLWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

typedef struct _WGLSURFACE
{
  HDC         pbdc;
  HGLRC       pbrc;
  HPBUFFERARB hpb;
  BYTE*       pixels;
  SIZE_T      pixels_capacity;
  int         width;
  int         height;
  BOOL        rendered_during_windowpos;
  BOOL        in_modal_size_move;

} WGLSURFACE;

#endif // NOOGLWINDOW

#ifdef __cplusplus
}
#endif

#endif // NOIMMERSIVE

#endif  /* _INC_IMMERSIVECTRL */
