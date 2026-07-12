/*
 * dwmframe.h -- WGLWindow caption chrome (Win32X dwmframex.c port, drawn
 * with OpenGL).
 *
 * Owns the chrome STATE and DRAWING: caption band, system icon, title,
 * min/max/close + light/dark buttons with uDWM's 160ms crossfades, the
 * FindNCHit-order hit test, WM_NCCALCSIZE / WM_GETMINMAXINFO geometry, and
 * the capture-tracked button press flow.  Drawing is plain OpenGL into the
 * window's GL frame (band and highlights as quads; title, button glyphs and
 * the system icon as GDI-rasterized textures -- the same asset-prep role GDI
 * plays for dwmframex's icon), after the client content and before the
 * present, so chrome + client content are one atomic present.  Input and
 * theme changes only update state, arm the animation timer, and invalidate;
 * the window control renders full frames through its one present path.
 */
#ifndef _INC_DWMFRAME
#define _INC_DWMFRAME

#if (_MSC_VER >= 1020)
#pragma once
#endif

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DWMFRAME DWMFRAME;

/* win32kfull FindNCHit private result for the light/dark caption button. */
#define HTLIGHTDARKBTN  0x0000B002

/* Chrome state + DWM frame dressing for hwnd.  No graphics pipeline is
 * created here (the window's GL context owns the drawing), so creation only
 * fails on allocation failure.  Destroy with the window's GL context current
 * so the cached textures can be released. */
DWMFRAME* WINAPI DwmFrameCreate(HWND hwnd);
VOID WINAPI DwmFrameDestroy(DWMFRAME* f);

/* Draws the caption chrome with OpenGL into the current GL context's draw
 * buffer, in window coordinates, over the (cx, cy) client-sized frame.  Call
 * after the client content is rendered, before the present. */
VOID WINAPI DwmFrameDrawChrome(DWMFRAME* f, HWND hwnd, int cx, int cy);

/* Caption band height in pixels. */
UINT WINAPI DwmFrameCaptionHeight(HWND hwnd);

/* WM_NCCALCSIZE (callable pre-create-state): rgrc[1] = rgrc[2] ("lie to
 * dwm"), maximized client = the monitor work area EXACTLY (pairs with
 * DwmFrameGetMinMaxInfo), otherwise left/right/bottom inset by the invisible
 * resize border -- the top border rides INSIDE the client. */
UINT WINAPI DwmFrameNCCalcSize(HWND hwnd, BOOL fCalcValidRects, NCCALCSIZE_PARAMS* lpcsp);

/* WM_GETMINMAXINFO (stateless): caption-anatomy min track; maximize and
 * max-track EXACTLY the nearest monitor's work area at its work origin;
 * max fields left alone mid move-size loop. */
VOID WINAPI DwmFrameGetMinMaxInfo(HWND hwnd, MINMAXINFO* lpMinMaxInfo);

/* WM_NCHITTEST (screen coords): FindNCHit region order — caption buttons,
 * system-menu icon slot, resize ring (left/right/bottom outside the client,
 * top inside), caption band, client. */
UINT WINAPI DwmFrameHitTest(DWMFRAME* f, HWND hwnd, int x, int y);

/* WM_NCACTIVATE: activation crossfade.  Caller must then return
 * DefWindowProc(hwnd, WM_NCACTIVATE, fActive, -1). */
VOID WINAPI DwmFrameOnNCActivate(DWMFRAME* f, HWND hwnd, BOOL fActive);

/* WM_ACTIVATE: (re-)report the DWM frame dressing (the documented
 * custom-frame contract) and start the activation crossfade. */
VOID WINAPI DwmFrameOnActivate(DWMFRAME* f, HWND hwnd, UINT state);

/* WM_TIMER: TRUE when id was the 160ms animation timer (state advanced and
 * the window invalidated; the paint path renders the frame). */
BOOL WINAPI DwmFrameOnTimer(DWMFRAME* f, HWND hwnd, UINT id);

/* Caption-button hover/press flow (xxxTrackCaptionButton shape). */
VOID WINAPI DwmFrameOnNCMouseMove(DWMFRAME* f, HWND hwnd, UINT codeHitTest);
VOID WINAPI DwmFrameOnNCMouseLeave(DWMFRAME* f, HWND hwnd);
BOOL WINAPI DwmFrameOnNCButtonDown(DWMFRAME* f, HWND hwnd, UINT codeHitTest);
BOOL WINAPI DwmFrameOnMouseMove(DWMFRAME* f, HWND hwnd, int x, int y);
BOOL WINAPI DwmFrameOnLButtonUp(DWMFRAME* f, HWND hwnd, int x, int y);
VOID WINAPI DwmFrameOnCaptureChanged(DWMFRAME* f, HWND hwnd);

/* TRUE while a caption-button press holds the mouse capture; the app's
 * subclass must route WM_MOUSEMOVE/WM_LBUTTONUP/WM_CAPTURECHANGED straight
 * to the window control then. */
BOOL WINAPI DwmFrameButtonPressActive(DWMFRAME* f);

#ifdef __cplusplus
}
#endif

#endif /* _INC_DWMFRAME */
