/*****************************************************************************\
*                                                                             *
* dxgipresent.h - imguiapp_impl_win32_d2ddxgi presenter, ported for WGLWindow *
*                                                                             *
* The canonical app-class pipeline (transcribed from the working              *
* imguiapp_impl_win32_d2ddxgi backend / ImmersiveWindow.c):                   *
*  - one composition swapchain at primary desktop resolution, NEVER resized;  *
*    the window draws into the top-left client sub-rect, the window bounds    *
*    clip the rest (buffer contents beyond the client stay transparent);      *
*  - buffer 0 bound once (D3D11 flip model aliases the current back buffer):  *
*    one RTV, one D2D target bitmap, for the swapchain's lifetime;            *
*  - GL pixels enter buffer 0 through WGL_NV_DX_interop2 (dxgi-noflicker §5)  *
*    or, when interop is unavailable, glReadPixels + UpdateSubresource --     *
*    creation therefore cannot fail on any D3D11-capable machine;             *
*  - the caption chrome (dwmframe.c) is drawn in OPENGL into the GL frame     *
*    BEFORE the fill, so buffer 0 receives client + chrome in one copy:       *
*    content + chrome + layout are one present (no D2D anywhere);             *
*  - the two-step present ladder with tearing flags, the R6 content pin on    *
*    the DComp visual, the compositor-frame repaint latch (R4), the D3DKMT    *
*    vblank wait, Commit+WaitForCommitCompletion (R2), and the modal pace     *
*    thread (R5).                                                             *
*                                                                             *
\*****************************************************************************/

#ifndef _INC_DXGIPRESENT
#define _INC_DXGIPRESENT

#if (_MSC_VER >= 1020)
#pragma once
#endif

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DXGIPRESENT DXGIPRESENT;
typedef struct DXGIPRESENTDEVICE DXGIPRESENTDEVICE;

/* Modal pace tick posted by the vblank thread while a modal loop is live
 * (R5); the WndProc repaints on it, waitless and latch-deduped. */
#define DXGIPRESENT_WM_PACE (WM_APP + 0x69)

/* GL-touching calls (Create, FillFromGL) require the GL context that owns
 * the frame's pixels to be current on the calling thread. */

/* The D3D stack presenters render through (device/context/factory).  Owned
 * by the CALLER — create one and pass it to every DxgiPresent_Create, the
 * same way imgui renderer backends take the app's device at Init.  Device
 * creation is expensive; presenters are created mid-drag. */
DXGIPRESENTDEVICE* WINAPI DxgiPresentDevice_Create(VOID);
VOID WINAPI DxgiPresentDevice_Destroy(DXGIPRESENTDEVICE* dev);

DXGIPRESENT* WINAPI DxgiPresent_Create(HWND hWnd, DXGIPRESENTDEVICE* dev);
VOID WINAPI DxgiPresent_Destroy(DXGIPRESENT* p);

/* Copies the GL read buffer's bottom-left (cx, cy) rect into buffer 0
 * (top-down flip included): interop blit when available, otherwise
 * glReadPixels + UpdateSubresource. */
BOOL WINAPI DxgiPresent_FillFromGL(DXGIPRESENT* p, int cx, int cy);

/* The reference two-step present ladder:
 *   Present(0, TEARING? | DO_NOT_WAIT | (fRestart ? RESTART : 0)); then
 *   fRestart||fVsync ? Present(1, DO_NOT_SEQUENCE)
 *                    : Present(0, TEARING? | DO_NOT_WAIT).
 * Unpins the content in the same latch and re-anchors the recorded content
 * origin/size. */
BOOL WINAPI DxgiPresent_Present(DXGIPRESENT* p, int cx, int cy, BOOL fRestart, BOOL fVsync);

/* R4 rationing: stamp at repaint start (client size the repaint draws);
 * ContentCurrent answers "does the upcoming compositor frame already have
 * content at this size?" -- extra repaints are queue churn and a later
 * RESTART can cancel content the compositor was about to sample. */
VOID WINAPI DxgiPresent_StampLatch(DXGIPRESENT* p, int cx, int cy);
BOOL WINAPI DxgiPresent_ContentCurrent(DXGIPRESENT* p, int cx, int cy);

/* TRUE when (cx, cy) differs from the client size of the last present. */
BOOL WINAPI DxgiPresent_SizeChanged(DXGIPRESENT* p, int cx, int cy);

/* R6 content pin: pin the presented content at its render-time screen
 * position via the visual offset (microseconds); the next present unpins in
 * the same compositor latch.  Call from WM_WINDOWPOSCHANGED, before the
 * repaint, when the change resized the window. */
VOID WINAPI DxgiPresent_PinContent(DXGIPRESENT* p, HWND hWnd);

/* R2: Commit + WaitForCommitCompletion -- content committed before this
 * call can never be overtaken by a geometry commit issued after it. */
VOID WINAPI DxgiPresent_WaitForCommit(DXGIPRESENT* p);

/* The reference repaint wait: frame-latency-waitable zero-timeout poll plus
 * the D3DKMT vblank wait (adapter opened on first use, bounded scan spin). */
VOID WINAPI DxgiPresent_WaitForVBlank(DXGIPRESENT* p, HWND hWnd);

/* R5 pace thread: TRUE on WM_ENTERSIZEMOVE/WM_ENTERMENULOOP, FALSE on exit.
 * While live it posts DXGIPRESENT_WM_PACE to the window at the monitor's
 * refresh rate (compositor clock / vblank / hi-res timer tiers).
 * PaceTickHandled clears the coalescing flag from the message handler. */
VOID WINAPI DxgiPresent_SetModalLive(DXGIPRESENT* p, BOOL fLive);
VOID WINAPI DxgiPresent_PaceTickHandled(DXGIPRESENT* p);

#ifdef __cplusplus
}
#endif

#endif /* _INC_DXGIPRESENT */
