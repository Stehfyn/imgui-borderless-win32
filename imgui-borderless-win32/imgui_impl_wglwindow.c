/*
 * dear imgui: Platform Backend glue for WGLWindow — implementation.
 *
 * Mirrors imgui_impl_win32.cpp's MULTI-VIEWPORT / PLATFORM INTERFACE section
 * function-for-function (same ViewportData shape, same style mapping, same
 * Show/Update/Focus/Minimized/Title/Alpha semantics, same capture-transfer
 * on destroy), replacing only:
 *   - the window class (WC_WGLWINDOW + WS_EX_NOREDIRECTIONBITMAP: every
 *     platform window is a full composition-swapchain WGLWindow);
 *   - AdjustWindowRect (no native frame; the composed caption band inside
 *     the client is the only dressing);
 *   - the geometry commit (coalesced move+size, deferred behind the frame's
 *     vblank-latched present — the presenter's content-first protocol;
 *     imgui core commits Pos and Size as two separate SetWindowPos calls,
 *     and a left/top imgui-border drag changes both in one frame).
 *
 * Renderer side is stock imgui_impl_opengl3: all WGLWindow GL contexts are
 * born into one share group, so its Renderer_RenderWindow works unmodified;
 * this module only binds the context (Platform_RenderWindow) and presents
 * (Platform_SwapBuffers), per the contract's division of labor.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>

#include "wglwindow.h"
#include "dwmframe.h"
#include "dxgipresent.h"
#include "imgui_impl_wglwindow.h"

#include "dcimgui.h"
#include "backends/dcimgui_impl_win32.h"

/* ---- backend data (io.BackendPlatformUserData is owned by imgui_impl_win32,
 * the input backend; this module's state lives behind an accessor, one
 * instance per process — same practical scope as the canonical backends,
 * whose own docs call multi-context support "probably dysfunctional"). ---- */

typedef struct ImGui_ImplWGLWindow_ViewportData
{
    HWND  Hwnd;
    HWND  HwndParent;
    BOOL  HwndOwned;
    DWORD DwStyle;
    DWORD DwExStyle;

    /* Coalesced deferred geometry commit (seam 2). */
    BOOL  PendingMove;
    BOOL  PendingSize;
    int   PendingX;
    int   PendingY;
    int   PendingCx;
    int   PendingCy;
} ImGui_ImplWGLWindow_ViewportData;

typedef struct ImGui_ImplWGLWindow_Data
{
    BOOL   Initialized;
    HWND   MainHwnd;
    LPVOID RenderFiber;
    /* imgui_impl_win32 installed its own Platform_DestroyWindow before us;
     * main-viewport destruction chains to it so it can free ITS ViewportData
     * (stored in the main viewport's PlatformUserData). */
    void (*Win32_Platform_DestroyWindow)(ImGuiViewport* viewport);
} ImGui_ImplWGLWindow_Data;

static ImGui_ImplWGLWindow_Data* ImGui_ImplWGLWindow_GetBackendData(void)
{
    static ImGui_ImplWGLWindow_Data data;
    return &data;
}

/* ---- helpers ------------------------------------------------------------ */

static HWND ImGui_ImplWGLWindow_GetHwndFromViewport(ImGuiViewport* viewport)
{
    return viewport ? (HWND)viewport->PlatformHandle : NULL;
}

/* Secondary viewports only: the main viewport's PlatformUserData belongs to
 * imgui_impl_win32 (different layout). */
static ImGui_ImplWGLWindow_ViewportData* ImGui_ImplWGLWindow_GetViewportData(ImGuiViewport* viewport)
{
    ImGui_ImplWGLWindow_ViewportData* vd;

    if (!viewport || viewport == ImGui_GetMainViewport())
        return NULL;
    vd = (ImGui_ImplWGLWindow_ViewportData*)viewport->PlatformUserData;
    if (!vd || vd->Hwnd != (HWND)viewport->PlatformHandle)
        return NULL;
    return vd;
}

static WGLSURFACE* ImGui_ImplWGLWindow_GetViewportSurface(ImGuiViewport* viewport)
{
    HWND hwnd = ImGui_ImplWGLWindow_GetHwndFromViewport(viewport);
    return hwnd ? (WGLSURFACE*)GetWindowLongPtr(hwnd, 0) : NULL;
}

/* Canonical style mapping + the WGLWindow class requirement. */
static void ImGui_ImplWGLWindow_GetWin32StyleFromViewportFlags(ImGuiViewportFlags flags, DWORD* out_style, DWORD* out_ex_style)
{
    *out_style = (flags & ImGuiViewportFlags_NoDecoration) ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    *out_ex_style = (flags & ImGuiViewportFlags_NoTaskBarIcon) ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW;
    if (flags & ImGuiViewportFlags_TopMost)
        *out_ex_style |= WS_EX_TOPMOST;

    /* Every platform window is a full WGLWindow on the composition-swapchain
     * presenter (content + geometry latch atomically per compositor frame). */
    *out_ex_style |= WS_EX_NOREDIRECTIONBITMAP;
}

/* Seam 1: content rect -> window rect.  WGLWindows have NO native frame
 * (WM_NCCALCSIZE: client == window); decorated windows carry the composed
 * caption band INSIDE the client. */
static void ImGui_ImplWGLWindow_AdjustWindowRect(ImGuiViewport* viewport, RECT* rect, DWORD style, DWORD ex_style)
{
    UNREFERENCED_PARAMETER(style);
    UNREFERENCED_PARAMETER(ex_style);
    if (viewport == ImGui_GetMainViewport())
        return;
    if (!(viewport->Flags & ImGuiViewportFlags_NoDecoration))
    {
        ImGui_ImplWGLWindow_ViewportData* vd = ImGui_ImplWGLWindow_GetViewportData(viewport);
        rect->top -= (LONG)DwmFrameCaptionHeight(vd ? vd->Hwnd : NULL);
    }
}

static void ImGui_ImplWGLWindow_UpdateWin32StyleFromWindow(ImGui_ImplWGLWindow_ViewportData* vd)
{
    vd->DwStyle = (DWORD)GetWindowLongPtr(vd->Hwnd, GWL_STYLE);
    vd->DwExStyle = (DWORD)GetWindowLongPtr(vd->Hwnd, GWL_EXSTYLE);
}

static LRESULT CALLBACK ImGui_ImplWGLWindow_WndProcHandler_PlatformWindow(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* ---- Platform interface -------------------------------------------------- */

static void ImGui_ImplWGLWindow_CreateWindow(ImGuiViewport* viewport)
{
    ImGui_ImplWGLWindow_Data* bd = ImGui_ImplWGLWindow_GetBackendData();
    ImGui_ImplWGLWindow_ViewportData* vd;
    RECT rect;

    vd = (ImGui_ImplWGLWindow_ViewportData*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*vd));
    if (!vd)
        return;
    viewport->PlatformUserData = vd;

    ImGui_ImplWGLWindow_GetWin32StyleFromViewportFlags(viewport->Flags, &vd->DwStyle, &vd->DwExStyle);
    vd->HwndParent = ImGui_ImplWGLWindow_GetHwndFromViewport(viewport->ParentViewport);

    rect.left   = (LONG)viewport->Pos.x;
    rect.top    = (LONG)viewport->Pos.y;
    rect.right  = (LONG)(viewport->Pos.x + viewport->Size.x);
    rect.bottom = (LONG)(viewport->Pos.y + viewport->Size.y);
    ImGui_ImplWGLWindow_AdjustWindowRect(viewport, &rect, vd->DwStyle, vd->DwExStyle);

    vd->Hwnd = CreateWindowEx(
        vd->DwExStyle, WC_WGLWINDOW, TEXT("Untitled"), vd->DwStyle,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        vd->HwndParent /* Owner */, NULL, GetModuleHandle(NULL), bd->RenderFiber);
    if (!vd->Hwnd)
    {
        viewport->PlatformUserData = NULL;
        HeapFree(GetProcessHeap(), 0, vd);
        return;
    }

    vd->HwndOwned = TRUE;
    viewport->PlatformRequestResize = false;
    viewport->PlatformHandle = viewport->PlatformHandleRaw = vd->Hwnd;

    /* Secondary viewports store their imgui context (canonical), plus the
     * WGLWindow lifecycle props. */
    SetPropA(vd->Hwnd, "IMGUI_CONTEXT", ImGui_GetCurrentContext());
    SetPropA(vd->Hwnd, WGLWINDOW_NO_QUIT_ON_DESTROY_PROP, (HANDLE)1);
    SetPropA(vd->Hwnd, WGLWINDOW_SECONDARY_VIEWPORT_PROP, (HANDLE)1);
    SubclassWindow(vd->Hwnd, ImGui_ImplWGLWindow_WndProcHandler_PlatformWindow);
}

static void ImGui_ImplWGLWindow_DestroyWindow(ImGuiViewport* viewport)
{
    ImGui_ImplWGLWindow_Data* bd = ImGui_ImplWGLWindow_GetBackendData();
    ImGui_ImplWGLWindow_ViewportData* vd;

    /* Main viewport: chain to imgui_impl_win32 (owner of its ViewportData). */
    if (viewport == ImGui_GetMainViewport())
    {
        if (bd->Win32_Platform_DestroyWindow)
            bd->Win32_Platform_DestroyWindow(viewport);
        return;
    }

    vd = (ImGui_ImplWGLWindow_ViewportData*)viewport->PlatformUserData;
    if (vd)
    {
        /* Transfer capture so a drag started from a disappearing window still
         * delivers the MOUSEUP (canonical imgui_impl_win32 behavior). */
        if (GetCapture() == vd->Hwnd)
        {
            ReleaseCapture();
            SetCapture(bd->MainHwnd);
        }
        if (vd->Hwnd && vd->HwndOwned)
            DestroyWindow(vd->Hwnd);
        vd->Hwnd = NULL;
        HeapFree(GetProcessHeap(), 0, vd);
    }
    viewport->PlatformUserData = viewport->PlatformHandle = NULL;
}

static void ImGui_ImplWGLWindow_ShowWindow(ImGuiViewport* viewport)
{
    ImGui_ImplWGLWindow_ViewportData* vd = ImGui_ImplWGLWindow_GetViewportData(viewport);
    BOOL avoid_bringing_parent_to_front;

    if (!vd || !vd->Hwnd)
        return;

    /* ShowWindow even with SW_SHOWNA also brings the OWNER to front; detach
     * it for the call (canonical, imgui issues #7354/#8669). */
    avoid_bringing_parent_to_front = vd->HwndParent != NULL &&
        (viewport->Flags & (ImGuiViewportFlags_NoFocusOnAppearing | ImGuiViewportFlags_NoTaskBarIcon)) != 0;
    if (avoid_bringing_parent_to_front)
        SetWindowLongPtr(vd->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)NULL);

    ShowWindow(vd->Hwnd, (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing) ? SW_SHOWNA : SW_SHOW);

    if (avoid_bringing_parent_to_front)
        SetWindowLongPtr(vd->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)vd->HwndParent);
}

static void ImGui_ImplWGLWindow_UpdateWindow(ImGuiViewport* viewport)
{
    ImGui_ImplWGLWindow_ViewportData* vd = ImGui_ImplWGLWindow_GetViewportData(viewport);
    HWND  next_parent;
    DWORD next_style;
    DWORD next_ex_style;

    if (!vd || !vd->Hwnd)
        return;

    /* Owner update (canonical: GWLP_HWNDPARENT, never ::SetParent). */
    next_parent = ImGui_ImplWGLWindow_GetHwndFromViewport(viewport->ParentViewport);
    if (next_parent != vd->HwndParent)
    {
        if (!IsWGLWindowInModalSizeMove(vd->Hwnd))
        {
            vd->HwndParent = next_parent;
            SetWindowLongPtr(vd->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)vd->HwndParent);
        }
    }

    /* Style refresh when viewport flags changed (canonical). */
    ImGui_ImplWGLWindow_GetWin32StyleFromViewportFlags(viewport->Flags, &next_style, &next_ex_style);
    if (vd->DwStyle != next_style || vd->DwExStyle != next_ex_style)
    {
        BOOL top_most_changed;
        HWND insert_after;
        UINT swp_flags;
        RECT rect;

        if (IsWGLWindowInModalSizeMove(vd->Hwnd))
            return;
        top_most_changed = (vd->DwExStyle & WS_EX_TOPMOST) != (next_ex_style & WS_EX_TOPMOST);
        insert_after = top_most_changed ? ((viewport->Flags & ImGuiViewportFlags_TopMost) ? HWND_TOPMOST : HWND_NOTOPMOST) : NULL;
        swp_flags = top_most_changed ? 0 : SWP_NOZORDER;

        rect.left   = (LONG)viewport->Pos.x;
        rect.top    = (LONG)viewport->Pos.y;
        rect.right  = (LONG)(viewport->Pos.x + viewport->Size.x);
        rect.bottom = (LONG)(viewport->Pos.y + viewport->Size.y);

        vd->DwStyle = next_style;
        vd->DwExStyle = next_ex_style;
        SetWindowLongPtr(vd->Hwnd, GWL_STYLE, vd->DwStyle);
        SetWindowLongPtr(vd->Hwnd, GWL_EXSTYLE, vd->DwExStyle);
        ImGui_ImplWGLWindow_AdjustWindowRect(viewport, &rect, vd->DwStyle, vd->DwExStyle);
        SetWindowPos(vd->Hwnd, insert_after, rect.left, rect.top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     swp_flags | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        ShowWindow(vd->Hwnd, SW_SHOWNA);   /* necessary after style change */
        viewport->PlatformRequestMove = viewport->PlatformRequestResize = true;
    }
}

/* The caption-band inset applies to SECONDARY decorated viewports only:
 * their viewport is the content area below the band.  The MAIN viewport
 * spans the whole client — its band is handled through the work-area inset
 * (BuildWorkInsetMin), so offsetting its origin would shift the entire
 * coordinate system (hit tests, overlays) by the caption height. */
static LONG ImGui_ImplWGLWindow_GetChromeInset(ImGuiViewport* viewport, HWND hwnd)
{
    WGLSURFACE* surface;

    if (!hwnd || viewport == ImGui_GetMainViewport())
        return 0;
    surface = (WGLSURFACE*)GetWindowLongPtr(hwnd, 0);
    return (surface && surface->frame) ? (LONG)DwmFrameCaptionHeight(hwnd) : 0;
}

static void ImGui_ImplWGLWindow_GetWindowPos(ImGuiViewport* viewport, ImVec2* out_pos)
{
    HWND hwnd = ImGui_ImplWGLWindow_GetHwndFromViewport(viewport);
    POINT pos = { 0, 0 };

    out_pos->x = 0.0f;
    out_pos->y = 0.0f;
    if (hwnd && ClientToScreen(hwnd, &pos))
    {
        out_pos->x = (float)pos.x;
        out_pos->y = (float)(pos.y + ImGui_ImplWGLWindow_GetChromeInset(viewport, hwnd));
    }
}

static void ImGui_ImplWGLWindow_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
{
    ImGui_ImplWGLWindow_ViewportData* vd = ImGui_ImplWGLWindow_GetViewportData(viewport);
    WGLSURFACE* surface;
    RECT rect;

    if (!vd || !vd->Hwnd)
        return;
    if (IsWGLWindowInModalSizeMove(vd->Hwnd))
        return;

    rect.left = rect.right  = (LONG)pos.x;
    rect.top  = rect.bottom = (LONG)pos.y;
    if (viewport->Flags & ImGuiViewportFlags_OwnedByApp)
        ImGui_ImplWGLWindow_UpdateWin32StyleFromWindow(vd);
    ImGui_ImplWGLWindow_AdjustWindowRect(viewport, &rect, vd->DwStyle, vd->DwExStyle);

    /* Seam 2: defer, so a same-frame Size change (left/top imgui-border
     * drag mutates Pos AND Size in one frame) commits as ONE geometry
     * transaction behind the latched present. */
    surface = (WGLSURFACE*)GetWindowLongPtr(vd->Hwnd, 0);
    if (surface && surface->dxgi)
    {
        vd->PendingMove = TRUE;
        vd->PendingX = rect.left;
        vd->PendingY = rect.top;
        return;
    }
    SetWindowPos(vd->Hwnd, NULL, rect.left, rect.top, 0, 0,
                 SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void ImGui_ImplWGLWindow_GetWindowSize(ImGuiViewport* viewport, ImVec2* out_size)
{
    HWND hwnd = ImGui_ImplWGLWindow_GetHwndFromViewport(viewport);
    SIZE size;

    out_size->x = 0.0f;
    out_size->y = 0.0f;
    /* Driven size (pending rgrc[0] during the pre-geometry repaint, live
     * client otherwise), minus the composed caption band on secondary
     * decorated windows — their viewport is the CONTENT area (seam 1). */
    if (hwnd && GetWGLWindowDrivenClientSize(hwnd, &size))
    {
        size.cy -= ImGui_ImplWGLWindow_GetChromeInset(viewport, hwnd);
        if (size.cy < 1)
            size.cy = 1;
        out_size->x = (float)size.cx;
        out_size->y = (float)size.cy;
    }
}

static void ImGui_ImplWGLWindow_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
    ImGui_ImplWGLWindow_ViewportData* vd = ImGui_ImplWGLWindow_GetViewportData(viewport);
    WGLSURFACE* surface;
    RECT rect;

    if (!vd || !vd->Hwnd)
        return;
    if (IsWGLWindowInModalSizeMove(vd->Hwnd))
        return;

    rect.left = rect.top = 0;
    rect.right  = (LONG)size.x;
    rect.bottom = (LONG)size.y;
    if (viewport->Flags & ImGuiViewportFlags_OwnedByApp)
        ImGui_ImplWGLWindow_UpdateWin32StyleFromWindow(vd);
    ImGui_ImplWGLWindow_AdjustWindowRect(viewport, &rect, vd->DwStyle, vd->DwExStyle);

    /* Seam 2 (content first, geometry second): the frame's presents run
     * after UpdatePlatformWindows — setting the DRIVEN size makes them fill
     * and stamp the swapchain at the NEW size; the geometry commit is
     * deferred to ImGui_ImplWGLWindow_RenderPlatformWindows, where
     * ContentCurrent short-circuits the NCCALCSIZE repaint and geometry
     * joins the already-latched content in one compositor frame. */
    surface = (WGLSURFACE*)GetWindowLongPtr(vd->Hwnd, 0);
    if (surface && surface->dxgi)
    {
        surface->pending_cx = rect.right - rect.left;
        surface->pending_cy = rect.bottom - rect.top;
        vd->PendingSize = TRUE;
        vd->PendingCx = rect.right - rect.left;
        vd->PendingCy = rect.bottom - rect.top;
        return;
    }
    SetWindowPos(vd->Hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

static void ImGui_ImplWGLWindow_SetWindowFocus(ImGuiViewport* viewport)
{
    ImGui_ImplWGLWindow_ViewportData* vd = ImGui_ImplWGLWindow_GetViewportData(viewport);
    if (!vd || !vd->Hwnd)
        return;
    BringWindowToTop(vd->Hwnd);
    SetForegroundWindow(vd->Hwnd);
    SetFocus(vd->Hwnd);
}

static bool ImGui_ImplWGLWindow_GetWindowFocus(ImGuiViewport* viewport)
{
    HWND hwnd = ImGui_ImplWGLWindow_GetHwndFromViewport(viewport);
    return hwnd && GetForegroundWindow() == hwnd;
}

static bool ImGui_ImplWGLWindow_GetWindowMinimized(ImGuiViewport* viewport)
{
    HWND hwnd = ImGui_ImplWGLWindow_GetHwndFromViewport(viewport);
    return hwnd && IsIconic(hwnd) != 0;
}

static void ImGui_ImplWGLWindow_SetWindowTitle(ImGuiViewport* viewport, const char* title)
{
    ImGui_ImplWGLWindow_ViewportData* vd = ImGui_ImplWGLWindow_GetViewportData(viewport);
    wchar_t  stack_title[256];
    wchar_t* wide_title = stack_title;
    int      count;

    if (!vd || !vd->Hwnd)
        return;
    if (!title)
        title = "";

    count = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    if (count <= 0)
        return;
    if (count > ARRAYSIZE(stack_title))
    {
        wide_title = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)count * sizeof(wchar_t));
        if (!wide_title)
            return;
    }
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title, count);
    /* DefWindowProcW delivery works in ANSI projects too (canonical). */
    DefWindowProcW(vd->Hwnd, WM_SETTEXT, 0, (LPARAM)wide_title);
    if (wide_title != stack_title)
        HeapFree(GetProcessHeap(), 0, wide_title);
}

static void ImGui_ImplWGLWindow_SetWindowAlpha(ImGuiViewport* viewport, float alpha)
{
    ImGui_ImplWGLWindow_ViewportData* vd = ImGui_ImplWGLWindow_GetViewportData(viewport);
    DWORD ex_style;

    if (!vd || !vd->Hwnd)
        return;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    /* NOTE: WS_EX_LAYERED is incompatible with WS_EX_NOREDIRECTIONBITMAP;
     * the presenter composes per-pixel alpha already, so global transparency
     * (docking transparent payload) is honored by the presenter's visual
     * opacity when available; the layered fallback applies to any non-noredir
     * window (canonical behavior otherwise). */
    ex_style = (DWORD)GetWindowLongPtr(vd->Hwnd, GWL_EXSTYLE);
    if (ex_style & WS_EX_NOREDIRECTIONBITMAP)
        return;
    if (alpha < 1.0f)
    {
        SetWindowLongPtr(vd->Hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
        SetLayeredWindowAttributes(vd->Hwnd, 0, (BYTE)(255.0f * alpha), LWA_ALPHA);
    }
    else
    {
        SetWindowLongPtr(vd->Hwnd, GWL_EXSTYLE, ex_style & ~WS_EX_LAYERED);
    }
}

static float ImGui_ImplWGLWindow_GetWindowDpiScale(ImGuiViewport* viewport)
{
    HWND hwnd = ImGui_ImplWGLWindow_GetHwndFromViewport(viewport);
    return hwnd ? cImGui_ImplWin32_GetDpiScaleForHwnd((void*)hwnd) : 1.0f;
}

static void ImGui_ImplWGLWindow_GetWindowFramebufferScale(ImGuiViewport* viewport, ImVec2* out_scale)
{
    /* "Always 1,1 on Windows" (imgui.h Platform_GetWindowFramebufferScale
     * contract: MUST BE INTEGER VALUES). */
    UNREFERENCED_PARAMETER(viewport);
    out_scale->x = 1.0f;
    out_scale->y = 1.0f;
}

/* ---- render hooks (called by ImGui_ImplWGLWindow_RenderPlatformWindows) -- */

static void ImGui_ImplWGLWindow_RenderWindow(ImGuiViewport* viewport, void* render_arg)
{
    WGLSURFACE* surface = ImGui_ImplWGLWindow_GetViewportSurface(viewport);
    UNREFERENCED_PARAMETER(render_arg);
    if (surface)
        wglMakeCurrent(surface->pbdc, surface->pbrc);
}

static void ImGui_ImplWGLWindow_SwapBuffers(ImGuiViewport* viewport, void* render_arg)
{
    ImGui_ImplWGLWindow_ViewportData* vd = ImGui_ImplWGLWindow_GetViewportData(viewport);
    WGLSURFACE* surface = ImGui_ImplWGLWindow_GetViewportSurface(viewport);
    UNREFERENCED_PARAMETER(render_arg);

    if (!vd || !vd->Hwnd)
        return;
    if (surface)
        wglMakeCurrent(surface->pbdc, surface->pbrc);

    /* Pending resize: align this present to the vblank like the WM_NCCALCSIZE
     * pending-rect repaint — the present is latched before the deferred
     * geometry commit can reach the compositor. */
    if (vd->PendingSize && surface && surface->dxgi)
        DxgiPresent_WaitForVBlank(surface->dxgi, vd->Hwnd);

    PresentWGLWindow(vd->Hwnd);
}

/* ---- Renderer_CreateWindow/DestroyWindow (renderer-side viewport data) --- */

static void ImGui_ImplWGLWindow_Renderer_CreateWindow(ImGuiViewport* viewport)
{
    /* GL object sharing is established by the window control at context
     * creation (one share group for every WGLWindow context) — the stock
     * imgui_impl_opengl3 Renderer_RenderWindow therefore works unmodified. */
    viewport->RendererUserData = ImGui_ImplWGLWindow_GetViewportSurface(viewport);
}

static void ImGui_ImplWGLWindow_Renderer_DestroyWindow(ImGuiViewport* viewport)
{
    viewport->RendererUserData = NULL;
}

/* ---- platform-window WndProc (canonical
 * ImGui_ImplWin32_WndProcHandler_PlatformWindow shape: context from the
 * window prop, shared input handler first, then the viewport message
 * cases, then the window class's DefWindowProc). ------------------------- */

static LRESULT CALLBACK ImGui_ImplWGLWindow_WndProcHandler_PlatformWindow(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ImGuiContext*  ctx;
    ImGuiViewport* viewport;

    ctx = (ImGuiContext*)GetPropA(hWnd, "IMGUI_CONTEXT");
    if (ctx)
        ImGui_SetCurrentContext(ctx);
    if (!ImGui_GetCurrentContext())
        return DefWGLWindowProc(hWnd, uMsg, wParam, lParam);

    /* A capture-tracked caption-button press (composed chrome) owns the
     * mouse stream BEFORE imgui sees it. */
    if (IsWGLWindowCaptionPressActive(hWnd) &&
        (uMsg == WM_MOUSEMOVE || uMsg == WM_LBUTTONUP || uMsg == WM_CAPTURECHANGED))
        return DefWGLWindowProc(hWnd, uMsg, wParam, lParam);

    if (cImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return 1;

    viewport = ImGui_FindViewportByPlatformHandle((void*)hWnd);
    if (viewport)
    {
        switch (uMsg)
        {
        case WM_CLOSE:
            /* Swallow: imgui closes the window itself next frame. */
            viewport->PlatformRequestClose = true;
            return 0;
        case WM_MOVE:
            viewport->PlatformRequestMove = true;
            break;
        case WM_SIZE:
            viewport->PlatformRequestResize = true;
            break;
        case WM_MOUSEACTIVATE:
            if (viewport->Flags & ImGuiViewportFlags_NoFocusOnClick)
                return MA_NOACTIVATE;
            break;
        case WM_NCHITTEST:
            /* Let mouse pass through while this viewport is being dragged,
             * so WindowFromPoint / AddMouseViewportEvent see the window
             * beneath (what makes HasMouseHoveredViewport legal). */
            if (viewport->Flags & ImGuiViewportFlags_NoInputs)
                return HTTRANSPARENT;
            break;
        default:
            break;
        }
    }

    return DefWGLWindowProc(hWnd, uMsg, wParam, lParam);
}

/* ---- render-all-windows + deferred commit flush -------------------------- */

static void ImGui_ImplWGLWindow_RenderOneWindow(ImGuiPlatformIO* platform_io, ImGuiViewport* viewport)
{
    if (viewport->Flags & ImGuiViewportFlags_IsMinimized)
        return;
    if (platform_io->Platform_RenderWindow) platform_io->Platform_RenderWindow(viewport, NULL);
    if (platform_io->Renderer_RenderWindow) platform_io->Renderer_RenderWindow(viewport, NULL);
}

static void ImGui_ImplWGLWindow_SwapOneWindow(ImGuiPlatformIO* platform_io, ImGuiViewport* viewport)
{
    if (viewport->Flags & ImGuiViewportFlags_IsMinimized)
        return;
    if (platform_io->Platform_SwapBuffers) platform_io->Platform_SwapBuffers(viewport, NULL);
    if (platform_io->Renderer_SwapBuffers) platform_io->Renderer_SwapBuffers(viewport, NULL);
}

VOID ImGui_ImplWGLWindow_RenderPlatformWindows(HWND sync_resize_hwnd)
{
    ImGuiPlatformIO* platform_io = ImGui_GetPlatformIO();
    ImGuiViewport* priority = NULL;
    int i;

    if (sync_resize_hwnd)
        priority = ImGui_FindViewportByPlatformHandle((void*)sync_resize_hwnd);
    if (priority == ImGui_GetMainViewport())
        priority = NULL;

    /* Same two-pass structure as ImGui::RenderPlatformWindowsDefault, with
     * the synchronous-resize window rendered and presented first. */
    if (priority)
        ImGui_ImplWGLWindow_RenderOneWindow(platform_io, priority);
    for (i = 1; i < platform_io->Viewports.Size; ++i)
        if (platform_io->Viewports.Data[i] != priority)
            ImGui_ImplWGLWindow_RenderOneWindow(platform_io, platform_io->Viewports.Data[i]);

    if (priority)
        ImGui_ImplWGLWindow_SwapOneWindow(platform_io, priority);
    for (i = 1; i < platform_io->Viewports.Size; ++i)
        if (platform_io->Viewports.Data[i] != priority)
            ImGui_ImplWGLWindow_SwapOneWindow(platform_io, platform_io->Viewports.Data[i]);

    /* Deferred geometry commits: presents above are latched; commit each
     * window's move+size as ONE transaction (imgui core issues Pos and Size
     * separately, and left/top border drags change both per frame). */
    for (i = 1; i < platform_io->Viewports.Size; ++i)
    {
        ImGuiViewport* viewport = platform_io->Viewports.Data[i];
        ImGui_ImplWGLWindow_ViewportData* vd = ImGui_ImplWGLWindow_GetViewportData(viewport);
        WGLSURFACE* surface;
        UINT flags;

        if (!vd || (!vd->PendingMove && !vd->PendingSize) || !vd->Hwnd)
            continue;

        surface = (WGLSURFACE*)GetWindowLongPtr(vd->Hwnd, 0);
        if (surface)
        {
            surface->pending_cx = 0;
            surface->pending_cy = 0;
            if (surface->dxgi && vd->PendingSize)
                DxgiPresent_WaitForCommit(surface->dxgi);
        }

        flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS;
        if (!vd->PendingMove) flags |= SWP_NOMOVE;
        if (!vd->PendingSize) flags |= SWP_NOSIZE;
        SetWindowPos(vd->Hwnd, NULL, vd->PendingX, vd->PendingY, vd->PendingCx, vd->PendingCy, flags);

        vd->PendingMove = FALSE;
        vd->PendingSize = FALSE;
    }
}

/* ---- init / shutdown ------------------------------------------------------ */

static void ImGui_ImplWGLWindow_InitMultiViewportSupport(void);

BOOL ImGui_ImplWGLWindow_Init(HWND hwnd, LPVOID render_fiber)
{
    ImGui_ImplWGLWindow_Data* bd = ImGui_ImplWGLWindow_GetBackendData();

    if (bd->Initialized)
        return FALSE;

    bd->Initialized = TRUE;
    bd->MainHwnd = hwnd;
    bd->RenderFiber = render_fiber;
    ImGui_ImplWGLWindow_InitMultiViewportSupport();
    return TRUE;
}

static void ImGui_ImplWGLWindow_InitMultiViewportSupport(void)
{
    ImGui_ImplWGLWindow_Data* bd = ImGui_ImplWGLWindow_GetBackendData();
    ImGuiPlatformIO* platform_io = ImGui_GetPlatformIO();

    /* imgui_impl_win32 (input backend) installed its platform interface at
     * its Init; save its DestroyWindow so main-viewport destruction can
     * chain to the owner of the main viewport's ViewportData, then replace
     * the window-management set with ours. */
    bd->Win32_Platform_DestroyWindow = platform_io->Platform_DestroyWindow;

    platform_io->Platform_CreateWindow = ImGui_ImplWGLWindow_CreateWindow;
    platform_io->Platform_DestroyWindow = ImGui_ImplWGLWindow_DestroyWindow;
    platform_io->Platform_ShowWindow = ImGui_ImplWGLWindow_ShowWindow;
    platform_io->Platform_SetWindowPos = ImGui_ImplWGLWindow_SetWindowPos;
    ImGuiPlatformIO_SetPlatform_GetWindowPos(ImGui_ImplWGLWindow_GetWindowPos);
    platform_io->Platform_SetWindowSize = ImGui_ImplWGLWindow_SetWindowSize;
    ImGuiPlatformIO_SetPlatform_GetWindowSize(ImGui_ImplWGLWindow_GetWindowSize);
    ImGuiPlatformIO_SetPlatform_GetWindowFramebufferScale(ImGui_ImplWGLWindow_GetWindowFramebufferScale);
    platform_io->Platform_SetWindowFocus = ImGui_ImplWGLWindow_SetWindowFocus;
    platform_io->Platform_GetWindowFocus = ImGui_ImplWGLWindow_GetWindowFocus;
    platform_io->Platform_GetWindowMinimized = ImGui_ImplWGLWindow_GetWindowMinimized;
    platform_io->Platform_SetWindowTitle = ImGui_ImplWGLWindow_SetWindowTitle;
    platform_io->Platform_SetWindowAlpha = ImGui_ImplWGLWindow_SetWindowAlpha;
    platform_io->Platform_UpdateWindow = ImGui_ImplWGLWindow_UpdateWindow;
    platform_io->Platform_RenderWindow = ImGui_ImplWGLWindow_RenderWindow;
    platform_io->Platform_SwapBuffers = ImGui_ImplWGLWindow_SwapBuffers;
    platform_io->Platform_GetWindowDpiScale = ImGui_ImplWGLWindow_GetWindowDpiScale;

    platform_io->Renderer_CreateWindow = ImGui_ImplWGLWindow_Renderer_CreateWindow;
    platform_io->Renderer_DestroyWindow = ImGui_ImplWGLWindow_Renderer_DestroyWindow;
    /* Renderer_RenderWindow: stock imgui_impl_opengl3 (installed by its own
     * Init) — untouched by design. */
}

static void ImGui_ImplWGLWindow_ShutdownMultiViewportSupport(void)
{
    ImGui_DestroyPlatformWindows();
}

VOID ImGui_ImplWGLWindow_Shutdown(VOID)
{
    ImGui_ImplWGLWindow_Data* bd = ImGui_ImplWGLWindow_GetBackendData();

    if (!bd->Initialized)
        return;
    ImGui_ImplWGLWindow_ShutdownMultiViewportSupport();
    bd->Initialized = FALSE;
    bd->MainHwnd = NULL;
    bd->RenderFiber = NULL;
    bd->Win32_Platform_DestroyWindow = NULL;
}
