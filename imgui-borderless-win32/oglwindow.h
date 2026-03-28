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
#include <gl/wglext.h>
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
WINOGLWINDOWAPI VOID WINAPI MessageFiberProc(void* unused);
WINOGLWINDOWAPI LRESULT CALLBACK DefOGLWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

typedef struct _WGLSURFACE
{
  HDC         pbdc;
  HGLRC       pbrc;
  HPBUFFERARB hpb;

} WGLSURFACE;

#endif // NOOGLWINDOW

#ifdef __cplusplus
}
#endif

#endif // NOIMMERSIVE

#endif  /* _INC_IMMERSIVECTRL */
