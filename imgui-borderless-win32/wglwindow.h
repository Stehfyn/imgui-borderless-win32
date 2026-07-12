/*****************************************************************************\
*                                                                             *
* WGLWindow.h -  OpenGL Macro and control APIs                                *
*                                                                             *
\*****************************************************************************/

#ifndef _INC_WGLWINDOW
#define _INC_WGLWINDOW

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
#ifndef WGLWINDOW_WGLEXT_SUBSET
#define WGLWINDOW_WGLEXT_SUBSET

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

#endif /* WGLWINDOW_WGLEXT_SUBSET */
#ifndef NOWGLWINDOW


//
// Define API decoration for direct importing of DLL references.
//
#ifndef WINWGLWINDOWAPI
#if defined(_WIN32) && !defined(WINWGLWINDOWAPI_INLINE)
#define WINWGLWINDOWAPI 
#else
#define WINWGLWINDOWAPI FORCEINLINE
#endif
#endif // WINWGLWINDOWAPI

#ifdef __cplusplus
extern "C" {            /* Assume C declarations for C++ */
#endif /* __cplusplus */

//
// Users of this header may define any number of these constants to avoid
// the definitions of each functional group.
//
//    NOWGLWINDOW           Customizable colloquially-borderless window control.
//    NOHWNDSERVER          Customizable Async-capable HWND Server.
//
//=============================================================================

WINWGLWINDOWAPI VOID WINAPI InitWGLControls(VOID);

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//  ============================ OpenGL Window Control ============================
//---------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------

#ifndef NOWGLWINDOW
#ifdef _WIN32

#define WGLWINDOW_CLASSW       L"WGLWindow"          
#define WGLWINDOW_CLASSA       "WGLWindow"
#ifdef UNICODE
#define WGLWINDOW_CLASS        WGLWINDOW_CLASSW
#else
#define WGLWINDOW_CLASS        WGLWINDOW_CLASSA
#endif

#else
#define WGLWINDOW_CLASS        "WGLWindow"
#endif
//#define BCS_WINDOW              (WS_DLGFRAME | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_SYSMENU | WS_POPUP)
/* melak47/BorderlessWindow aero_borderless, verbatim: WS_CAPTION stays — the
 * DWM treats captioned windows as real top-levels (shadow, rounding, snap,
 * minimize animations); its visuals never draw because WM_NCCALCSIZE eats
 * the whole frame (client == window). */
#define BCS_WINDOW              (WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX)

  /*
   * OpenGL window class
   */
#define WC_WGLWINDOW           (WGLWINDOW_CLASS)
#define WGLWINDOW_NO_QUIT_ON_DESTROY_PROP "WGLWindow_NoQuitOnDestroy"
#define WGLWINDOW_SECONDARY_VIEWPORT_PROP "WGLWindow_SecondaryViewport"

   //---------------------------------------------------------------------------------------
   // OpenGL Window Control Styles
   //---------------------------------------------------------------------------------------
   // begin_r_commctrl

//#define BCS_CENTERED            0x00000001L

// end_r_commctrl
//#define WM_INITD2D      (WM_USER + 0x78)
typedef void(__stdcall* RENDERPROC)(HWND hwnd);
#define WGLWindow_Create(lpszTitle, hwndP, id, dwStyle, X, Y, nWidth, nHeight, hInstance, renderproc)   \
           CreateWindow(WC_WGLWINDOW, lpszTitle,                                            \
               dwStyle, X, Y, nWidth, nHeight, hwndP, (HMENU)(id), hInstance, renderproc)

/* Default (no special ex-style): redirection-surface present — GL renders to
 * the pbuffer, the frame is blitted into the window DC inside WM_PAINT.
 * During a live resize DWM holds the new geometry until that repaint
 * completes, so content + geometry always compose atomically (the mechanism
 * every flicker-free GDI app rides).
 * WS_EX_LAYERED instead opts into the UpdateLayeredWindowIndirect present
 * path: content + geometry latch in one window-manager transaction, at the
 * cost of DWM nonclient rendering. */
#define WGLWindow_CreateEx(dwExStyle, lpszTitle, hwndP, id, dwStyle, X, Y, nWidth, nHeight, hInstance, renderproc)   \
           CreateWindowEx(dwExStyle, WC_WGLWINDOW, lpszTitle,                               \
               dwStyle, X, Y, nWidth, nHeight, hwndP, (HMENU)(id), hInstance, renderproc)

EXTERN_C NTSTATUS PFORCEINLINE WINAPI D3DKMTInitVerticalBlankEvent(HDC hdc, D3DKMT_WAITFORVERTICALBLANKEVENT* pVbe);
//WINWGLWINDOWAPI BOOL WINAPI WGLWindowPaintInit(HWND hwnd);
WINWGLWINDOWAPI HDC WINAPI BeginWGLWindowPaint(HWND hWnd);
WINWGLWINDOWAPI BOOL WINAPI EndWGLWindowPaint(HDC hDC);
WINWGLWINDOWAPI BOOL WINAPI PresentWGLWindow(HWND hWnd);
/* TRUE while a caption-button press (dwmframe chrome) holds the capture; the
 * app's subclass must bypass imgui's mouse handler for
 * WM_MOUSEMOVE/WM_LBUTTONUP/WM_CAPTURECHANGED then. */
WINWGLWINDOWAPI BOOL WINAPI IsWGLWindowCaptionPressActive(HWND hWnd);
/* The client size the frame being rendered must use: the driven (pending)
 * rgrc[0] size during the WM_NCCALCSIZE pending-rect repaint, the live
 * client rect otherwise.  The app's frame sizes imgui from this, never from
 * GetClientRect, so the pre-geometry frame lays out at the size the window
 * is ABOUT to have. */
WINWGLWINDOWAPI BOOL WINAPI GetWGLWindowDrivenClientSize(HWND hWnd, SIZE* psz);
WINWGLWINDOWAPI BOOL WINAPI IsWGLWindowInSynchronousResizeRender(HWND hWnd);
WINWGLWINDOWAPI HWND WINAPI GetWGLWindowSynchronousResizeHwnd(HWND hWnd);
WINWGLWINDOWAPI BOOL WINAPI IsWGLWindowInModalSizeMove(HWND hWnd);
WINWGLWINDOWAPI VOID WINAPI MessageFiberProc(void* unused);
WINWGLWINDOWAPI LRESULT CALLBACK DefWGLWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

typedef struct DWMFRAME DWMFRAME;         /* dwmframe.h: caption chrome */
typedef struct DXGIPRESENT DXGIPRESENT;   /* dxgipresent.h: composition-swapchain presenter */

typedef struct _WGLSURFACE
{
  HDC          pbdc;
  HGLRC        pbrc;
  HPBUFFERARB  hpb;
  BYTE*        pixels;
  SIZE_T       pixels_capacity;
  int          width;
  int          height;
  BOOL         rendered_during_windowpos;
  BOOL         in_modal_size_move;
  BOOL         in_windowpos_changed;      /* inside WM_WINDOWPOSCHANGED forward */
  UINT         sizing_edge;               /* WMSZ_* for the live size loop */
  HDC          paint_hdc;                 /* BeginPaint DC while inside WM_PAINT */
  BOOL         paint_presented;
  LONGLONG     last_present_ticks;
  LONGLONG     last_resize_render_ticks;
  HDC          uldc;        /* layered-present memory DC (DIB section) */
  HBITMAP      ulbmp;
  HBITMAP      ulbmp_prev;
  VOID*        ulbits;
  DXGIPRESENT* dxgi;        /* composition-swapchain presenter
                             * (WS_EX_NOREDIRECTIONBITMAP windows) */
  DWMFRAME*    frame;       /* caption chrome state, drawn into the
                             * presenter's buffer each present */
  int          pending_cx;  /* driven client size while the WM_NCCALCSIZE
                             * pending-rect repaint is on the stack: the
                             * incoming rgrc[0], rendered BEFORE the window
                             * manager commits the geometry (ImmersiveWindow
                             * pending-rect present; the render never
                             * queries the window).  0 otherwise. */
  int          pending_cy;
  BOOL         in_frame;    /* app frame on the stack (viewport churn sends
                             * messages here synchronously mid-frame; modal
                             * repaints must not re-enter the frame) */
  BOOL         in_modal_repaint;        /* WndProc repaint re-entrancy latch */
  BOOL         moving;                  /* WM_MOVING seen this modal loop */
  BOOL         modal_present_active;    /* present flavor override in effect */
  BOOL         modal_restart;
  BOOL         modal_vsync;

} WGLSURFACE;

#endif // NOWGLWINDOW

#ifdef __cplusplus
}
#endif

#endif // NOIMMERSIVE

#endif  /* _INC_IMMERSIVECTRL */
