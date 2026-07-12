/*
 * dear imgui: Platform Backend glue for WGLWindow (composition-swapchain
 * Win32 windows + OpenGL pbuffer rendering).
 *
 * Layered exactly like the official win32+opengl3 multi-viewport example:
 *  - imgui_impl_win32 stays the INPUT backend (mouse/keyboard/monitors/DPI/
 *    gamepad, io.AddMouseViewportEvent, WndProc input handling) on the main
 *    window.
 *  - imgui_impl_opengl3 stays the RENDERER backend untouched: its stock
 *    Renderer_RenderWindow renders every secondary viewport (all WGLWindow
 *    GL contexts share one object group, established at context birth).
 *  - THIS module replaces only the PLATFORM WINDOW interface
 *    (Platform_CreateWindow .. Platform_SwapBuffers), mirroring
 *    imgui_impl_win32's own multi-viewport section function-for-function,
 *    with two documented seams where the composition-swapchain presenter
 *    differs from stock Win32 windows:
 *      1. AdjustWindowRect: WGLWindows have NO native frame (WM_NCCALCSIZE:
 *         client == window); the only dressing is the composed caption band
 *         INSIDE the client of decorated windows.
 *      2. SetWindowPos/SetWindowSize: geometry commits are coalesced and
 *         deferred behind the frame's vblank-latched present (the
 *         presenter's content-first protocol), instead of committed
 *         immediately.
 *
 * Call order (per contract in imgui.h "MULTI-VIEWPORT / PLATFORM INTERFACE"):
 *   init:      cImGui_ImplWin32_Init -> cImGui_ImplOpenGL3_Init ->
 *              ImGui_ImplWGLWindow_Init
 *   frame end: ImGui_UpdatePlatformWindows ->
 *              ImGui_ImplWGLWindow_RenderPlatformWindows
 *   shutdown:  cImGui_ImplOpenGL3_Shutdown -> ImGui_ImplWGLWindow_Shutdown ->
 *              cImGui_ImplWin32_Shutdown
 */
#ifndef _INC_IMGUI_IMPL_WGLWINDOW
#define _INC_IMGUI_IMPL_WGLWINDOW

#if (_MSC_VER >= 1020)
#pragma once
#endif

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* hwnd: the main (application-owned) WGLWindow.  render_fiber: passed as
 * lpCreateParams to secondary platform windows (the WGLWindow render-fiber
 * contract).  Platform windows are subclassed with this module's own
 * WndProcHandler_PlatformWindow (canonical shape). */
BOOL ImGui_ImplWGLWindow_Init(HWND hwnd, LPVOID render_fiber);
VOID ImGui_ImplWGLWindow_Shutdown(VOID);

/* Renders and presents every secondary viewport (the sanctioned custom
 * variant of ImGui::RenderPlatformWindowsDefault: same four hooks, same
 * order), gives priority to sync_resize_hwnd when non-NULL (the WGLWindow
 * synchronous-resize window renders first), then flushes the deferred
 * geometry commits behind the latched presents. */
VOID ImGui_ImplWGLWindow_RenderPlatformWindows(HWND sync_resize_hwnd);

#ifdef __cplusplus
}
#endif

#endif /* _INC_IMGUI_IMPL_WGLWINDOW */
