#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <tchar.h>
#include <assert.h>
#include <GL/gl.h>
#include "oglwindow.h"
#include "swcadef.h"     

#include "dcimgui.h"
#include "dcimgui_internal.h"
#include "backends/dcimgui_impl_win32.h"
#include "backends/dcimgui_impl_opengl3.h"

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment (lib, "shcore")
#pragma comment (lib, "dwmapi")
#pragma comment (lib, "opengl32")
#pragma comment (lib, "glu32")
#pragma comment (lib, "Comctl32")

typedef void(__cdecl *RenderFunction)(HWND hWnd);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC) (int interval);
PFNWGLSWAPINTERVALEXTPROC      wglSwapIntervalEXT;

static RenderFunction g_ClientRenderFunction;
static BOOL           g_ClientIsBorderless = FALSE;

// Data stored per platform window
typedef struct WGL_WindowData
{
    HDC   hDC;
    HGLRC hRC;
} WGL_WindowData;

typedef struct OGLViewportData
{
    HWND  Hwnd;
    HWND  HwndParent;
    BOOL  HwndOwned;
    DWORD DwStyle;
    DWORD DwExStyle;
} OGLViewportData;

// Data
static HGLRC            g_hRC;
static WGL_WindowData   g_MainWindow;
static HWND             g_MainHwnd;
static LPVOID           g_MainFiber;

static void (*g_Win32_Platform_DestroyWindow)(ImGuiViewport* viewport);
static void (*g_OpenGL_Renderer_RenderWindow)(ImGuiViewport* viewport, void* user_data);

static void Hook_RenderPlatformWindows(HWND priority_hwnd);

static void draw(HWND hWnd)
{
  static ImVec4 clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
  ImGuiIO* io = ImGui_GetIO();

  cImGui_ImplOpenGL3_NewFrame();
  cImGui_ImplWin32_NewFrame();
  ImGui_NewFrame();

  // Dockspace
  {
    ImGui_DockSpaceOverViewport();
  }

  // ImGui Demo
  {
    ImGui_ShowDemoWindow(NULL);
  }

  {
    if (ImGui_GetFrameCount() % 180)
    {
      TCHAR szTitle[256];
      _stprintf(szTitle, TEXT("Application average %.3f ms/frame (%.1f FPS)"), 1000.0f / io->Framerate, io->Framerate);
      SetWindowText(hWnd, szTitle);
    }
  }

  // Rendering
  ImGui_Render();
  RECT rc;
  GetClientRect(hWnd, &rc);
  POINT pt = { rc.left, rc.top };
  ClientToScreen(hWnd, &pt);
  WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
  //wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc);
  
  glViewport(pt.x, pt.y, pt.x + (rc.right - rc.left), pt.y + (rc.bottom - rc.top));
  //glViewport(0, 0, 256, 256);
  glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
  glClear(GL_COLOR_BUFFER_BIT);
  ImDrawData* dd = ImGui_GetDrawData();
  for (int i = 0; i < dd->Textures->Size; ++i)
    if (dd->Textures->Data[i] && (ImTextureStatus_WantCreate == dd->Textures->Data[i]->Status))
    {
      dd->Textures->Data[i]->TexID = ImTextureID_Invalid;
      dd->Textures->Data[i]->BackendUserData = 0;
    } 
  
  cImGui_ImplOpenGL3_RenderDrawData(dd);
  
  // Update and Render additional Platform Windows
  if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    HWND sync_resize_hwnd = GetOGLWindowSynchronousResizeHwnd();
    ImGui_UpdatePlatformWindows();
    if (sync_resize_hwnd && GetPropA(sync_resize_hwnd, OGLWINDOW_SECONDARY_VIEWPORT_PROP))
        Hook_RenderPlatformWindows(sync_resize_hwnd);
    else
        ImGui_RenderPlatformWindowsDefault();
    
    // Restore the OpenGL rendering context to the main window DC, since platform windows might have changed it.
    wglMakeCurrent(g_MainWindow.hDC, g_hRC);
  }

  SwapBuffers(g_MainWindow.hDC);
  // Present
}

static void __stdcall render(HWND hWnd)
{
    HDC hdc = BeginOGLWindowPaint(hWnd);

    if (g_ClientRenderFunction)
    {
      g_ClientRenderFunction(hWnd);
      //DwmFlush();
    }

    EndOGLWindowPaint(hdc);
}

static void InstallOGLViewportHooks(void);
static OGLViewportData* Hook_GetViewportData(ImGuiViewport* viewport);
static void Hook_UpdateViewportRectFromHwnd(ImGuiViewport* viewport, HWND hwnd, BOOL update_pos, BOOL update_size);
static LRESULT CALLBACK ImGuiSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static void Hook_UpdateViewportRectFromHwnd(ImGuiViewport* viewport, HWND hwnd, BOOL update_pos, BOOL update_size)
{
    if (!viewport || !hwnd)
        return;

    if (update_pos)
    {
        POINT pos = { 0, 0 };
        if (ClientToScreen(hwnd, &pos))
        {
            viewport->Pos.x = (float)pos.x;
            viewport->Pos.y = (float)pos.y;
        }
    }

    if (update_size)
    {
        RECT rect = { 0, 0, 0, 0 };
        if (GetClientRect(hwnd, &rect))
        {
            viewport->Size.x = (float)(rect.right - rect.left);
            viewport->Size.y = (float)(rect.bottom - rect.top);
        }
    }
}

static LRESULT CALLBACK ImGuiSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ImGuiContext* ctx = (ImGuiContext*)GetPropA(hWnd, "IMGUI_CONTEXT");
    if (ctx)
        ImGui_SetCurrentContext(ctx);

    ImGuiViewport* viewport = ImGui_FindViewportByPlatformHandle((void*)hWnd);
    if (viewport == ImGui_GetMainViewport() && uMsg == WM_CLOSE)
    {
        DestroyWindow(hWnd);
        return 0;
    }

    if (cImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
      return 1;

    if (viewport && viewport != ImGui_GetMainViewport())
    {
        switch (uMsg)
        {
        case WM_CLOSE:
            viewport->PlatformRequestClose = true;
            return 0;
        case WM_MOVE:
            Hook_UpdateViewportRectFromHwnd(viewport, hWnd, TRUE, FALSE);
            viewport->PlatformRequestMove = true;
            break;
        case WM_SIZE:
            Hook_UpdateViewportRectFromHwnd(viewport, hWnd, FALSE, TRUE);
            viewport->PlatformRequestResize = true;
            break;
        case WM_WINDOWPOSCHANGED:
        {
            const WINDOWPOS* window_pos = (const WINDOWPOS*)lParam;
            if (window_pos && 0 == (window_pos->flags & SWP_NOMOVE))
            {
                Hook_UpdateViewportRectFromHwnd(viewport, hWnd, TRUE, FALSE);
                viewport->PlatformRequestMove = true;
            }
            if (window_pos && 0 == (window_pos->flags & SWP_NOSIZE))
            {
                Hook_UpdateViewportRectFromHwnd(viewport, hWnd, FALSE, TRUE);
                viewport->PlatformRequestResize = true;
            }
            break;
        }
        case WM_MOUSEACTIVATE:
            if (viewport->Flags & ImGuiViewportFlags_NoFocusOnClick)
                return MA_NOACTIVATE;
            break;
        case WM_NCHITTEST:
            if (viewport->Flags & ImGuiViewportFlags_NoInputs)
                return HTTRANSPARENT;
            break;
        default:
            break;
        }
    }

    return DefOGLWindowProc(hWnd, uMsg, wParam, lParam);
}

int 
APIENTRY 
wWinMain(
    _In_ HINSTANCE     hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR        lpCmdLine,
    _In_ int           nCmdShow)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);
    UNREFERENCED_PARAMETER(lpCmdLine);

    cImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = cImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY));
    SetWindowCompositionAttribute = (PFN_SET_WINDOW_COMPOSITION_ATTRIBUTE)GetProcAddress(GetModuleHandle(TEXT("user32.dll")), "SetWindowCompositionAttribute");
    
    InitOGLControls();
    g_MainFiber = ConvertThreadToFiber(NULL);
    LPVOID hMsgFiber = CreateFiber(0, MessageFiberProc, g_MainFiber);

    HWND hwnd = OGLWindow_Create(TEXT("OGLWindow"), 0, 0, 
      //WS_CLIPCHILDREN|WS_CLIPSIBLINGS|
      WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, 
      CW_USEDEFAULT, CW_USEDEFAULT, 1080, 720, GetModuleHandle(NULL),
      g_MainFiber);
    g_MainHwnd = hwnd;

    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hwnd, 0);

    g_hRC = pwglSurf->pbrc;
    g_MainWindow.hDC = pwglSurf->pbdc;
    g_MainWindow.hRC = g_hRC;

    CIMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui_CreateContext(NULL);
    ImGuiIO* io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable Docking
    io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;     // Enable Multi-Viewport / Platform Windows
    io->ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
    io->ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;
    // Setup Dear ImGui style
    ImGuiStyle* style = ImGui_GetStyle();
    ImGui_StyleColorsDark(style);
    ImGuiStyle_ScaleAllSizes(style, main_scale);
    style->FontScaleDpi = main_scale;
    io->ConfigDpiScaleFonts = 1;          // [Experimental] Automatically overwrite style.FontScaleDpi in Begin() when Monitor DPI changes. This will scale fonts but _NOT_ scale sizes/padding for now.
    io->ConfigDpiScaleViewports = 1;      // [Experimental] Scale Dear ImGui and Platform Windows when Monitor DPI changes.
    
    // Setup Platform/Renderer backends
    cImGui_ImplWin32_InitForOpenGL((void*)hwnd);
    cImGui_ImplOpenGL3_Init();
    cImGui_ImplWin32_EnableDpiAwareness();

    if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        InstallOGLViewportHooks();
    int quit = 0;

    SubclassWindow(hwnd, ImGuiSubclassProc);

    g_ClientRenderFunction = draw;

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    D3DKMT_WAITFORVERTICALBLANKEVENT vbe;
    D3DKMTInitVerticalBlankEvent(g_MainWindow.hDC, &vbe);
    for(;;)
    {
        SwitchToFiber(hMsgFiber);


        InvalidateRect(hwnd, 0, 0);
        //RedrawWindow(hwnd, 0, 0, RDW_ERASE | RDW_UPDATENOW);
        //wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc);
        draw(hwnd);
        //glFinish();
        //wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc);
        //SwapBuffers(pwglSurf->pbdc);
        if (!IsOGLWindowInSynchronousResizeRender())
            D3DKMTWaitForVerticalBlankEvent(&vbe);

    }

    cImGui_ImplOpenGL3_Shutdown();
    cImGui_ImplWin32_Shutdown();
    ImGui_DestroyPlatformWindows();
    ImGui_DestroyContext(ctx);

    DestroyWindow(hwnd);

    return 0;
} // main

static HWND Hook_GetHwndFromViewport(ImGuiViewport* viewport)
{
    return viewport ? (HWND)viewport->PlatformHandle : NULL;
}

static OGLViewportData* Hook_GetViewportData(ImGuiViewport* viewport)
{
    OGLViewportData* data;

    if (!viewport || viewport == ImGui_GetMainViewport())
        return NULL;

    data = (OGLViewportData*)viewport->PlatformUserData;
    if (!data || data->Hwnd != (HWND)viewport->PlatformHandle)
        return NULL;

    return data;
}

static void Hook_GetWin32StyleFromViewportFlags(ImGuiViewportFlags flags, DWORD* out_style, DWORD* out_ex_style)
{
    *out_style = (flags & ImGuiViewportFlags_NoDecoration) ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    *out_ex_style = (flags & ImGuiViewportFlags_NoTaskBarIcon) ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW;
    if (flags & ImGuiViewportFlags_TopMost)
        *out_ex_style |= WS_EX_TOPMOST;

    *out_style |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
}

static void Hook_AdjustWindowRect(ImGuiViewport* viewport, RECT* rect, DWORD style, DWORD ex_style)
{
    UNREFERENCED_PARAMETER(viewport);
    AdjustWindowRectEx(rect, style, FALSE, ex_style);
}

static void Hook_UpdateStyleFromWindow(OGLViewportData* data)
{
    data->DwStyle = (DWORD)GetWindowLongPtr(data->Hwnd, GWL_STYLE);
    data->DwExStyle = (DWORD)GetWindowLongPtr(data->Hwnd, GWL_EXSTYLE);
}

static WGLSURFACE* Hook_GetViewportSurface(ImGuiViewport* viewport)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    HWND hwnd = data ? data->Hwnd : Hook_GetHwndFromViewport(viewport);
    return hwnd ? (WGLSURFACE*)GetWindowLongPtr(hwnd, 0) : NULL;
}

static void Hook_Platform_CreateWindow(ImGuiViewport* viewport)
{
    OGLViewportData* data = (OGLViewportData*)CIM_ALLOC(sizeof(OGLViewportData));
    if (!data)
        return;

    ZeroMemory(data, sizeof(*data));
    viewport->PlatformUserData = data;

    Hook_GetWin32StyleFromViewportFlags(viewport->Flags, &data->DwStyle, &data->DwExStyle);
    data->HwndParent = Hook_GetHwndFromViewport(viewport->ParentViewport);

    RECT rect = {
        (LONG)viewport->Pos.x,
        (LONG)viewport->Pos.y,
        (LONG)(viewport->Pos.x + viewport->Size.x),
        (LONG)(viewport->Pos.y + viewport->Size.y)
    };
    Hook_AdjustWindowRect(viewport, &rect, data->DwStyle, data->DwExStyle);

    data->Hwnd = CreateWindowEx(
        data->DwExStyle,
        WC_OGLWINDOW,
        TEXT("Untitled"),
        data->DwStyle,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        data->HwndParent,
        NULL,
        GetModuleHandle(NULL),
        g_MainFiber);

    if (!data->Hwnd)
    {
        viewport->PlatformUserData = NULL;
        CIM_FREE(data);
        return;
    }

    data->HwndOwned = TRUE;
    viewport->PlatformRequestResize = false;
    viewport->PlatformHandle = viewport->PlatformHandleRaw = data->Hwnd;

    SetPropA(data->Hwnd, "IMGUI_CONTEXT", ImGui_GetCurrentContext());
    SetPropA(data->Hwnd, OGLWINDOW_NO_QUIT_ON_DESTROY_PROP, (HANDLE)1);
    SetPropA(data->Hwnd, OGLWINDOW_SECONDARY_VIEWPORT_PROP, (HANDLE)1);
    SubclassWindow(data->Hwnd, ImGuiSubclassProc);
}

static void Hook_Platform_DestroyWindow(ImGuiViewport* viewport)
{
    if (viewport == ImGui_GetMainViewport())
    {
        if (g_Win32_Platform_DestroyWindow)
            g_Win32_Platform_DestroyWindow(viewport);
        else
            viewport->PlatformUserData = viewport->PlatformHandle = NULL;
        return;
    }

    OGLViewportData* data = Hook_GetViewportData(viewport);
    if (data)
    {
        if (GetCapture() == data->Hwnd)
        {
            ReleaseCapture();
            if (g_MainHwnd)
                SetCapture(g_MainHwnd);
        }

        if (data->Hwnd)
        {
            RemovePropA(data->Hwnd, "IMGUI_CONTEXT");
            RemovePropA(data->Hwnd, OGLWINDOW_SECONDARY_VIEWPORT_PROP);
            if (data->HwndOwned)
                DestroyWindow(data->Hwnd);
        }

        data->Hwnd = NULL;
        CIM_FREE(data);
    }

    viewport->PlatformUserData = NULL;
    viewport->PlatformHandle = NULL;
    viewport->PlatformHandleRaw = NULL;
}

static void Hook_Platform_ShowWindow(ImGuiViewport* viewport)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    BOOL avoid_bringing_parent_to_front;
    if (!data || !data->Hwnd)
        return;

    avoid_bringing_parent_to_front = data->HwndParent != NULL &&
        (viewport->Flags & (ImGuiViewportFlags_NoFocusOnAppearing | ImGuiViewportFlags_NoTaskBarIcon)) != 0;

    if (avoid_bringing_parent_to_front)
        SetWindowLongPtr(data->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)NULL);

    ShowWindow(data->Hwnd, (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing) ? SW_SHOWNA : SW_SHOW);

    if (avoid_bringing_parent_to_front)
        SetWindowLongPtr(data->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)data->HwndParent);
}

static void Hook_Platform_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    RECT rect;
    if (!data || !data->Hwnd)
        return;
    if (IsOGLWindowInModalSizeMove(data->Hwnd))
        return;

    rect.left = (LONG)pos.x;
    rect.top = (LONG)pos.y;
    rect.right = (LONG)pos.x;
    rect.bottom = (LONG)pos.y;
    if (viewport->Flags & ImGuiViewportFlags_OwnedByApp)
        Hook_UpdateStyleFromWindow(data);
    Hook_AdjustWindowRect(viewport, &rect, data->DwStyle, data->DwExStyle);
    SetWindowPos(data->Hwnd, NULL, rect.left, rect.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
}

static void Hook_Platform_GetWindowPos(ImGuiViewport* viewport, ImVec2* out_pos)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    HWND hwnd = data ? data->Hwnd : Hook_GetHwndFromViewport(viewport);
    POINT pos = { 0, 0 };

    out_pos->x = 0.0f;
    out_pos->y = 0.0f;
    if (hwnd && ClientToScreen(hwnd, &pos))
    {
        out_pos->x = (float)pos.x;
        out_pos->y = (float)pos.y;
    }
}

static void Hook_Platform_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    RECT rect;
    if (!data || !data->Hwnd)
        return;
    if (IsOGLWindowInModalSizeMove(data->Hwnd))
        return;

    rect.left = 0;
    rect.top = 0;
    rect.right = (LONG)size.x;
    rect.bottom = (LONG)size.y;
    if (viewport->Flags & ImGuiViewportFlags_OwnedByApp)
        Hook_UpdateStyleFromWindow(data);
    Hook_AdjustWindowRect(viewport, &rect, data->DwStyle, data->DwExStyle);
    SetWindowPos(data->Hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
}

static void Hook_Platform_GetWindowSize(ImGuiViewport* viewport, ImVec2* out_size)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    HWND hwnd = data ? data->Hwnd : Hook_GetHwndFromViewport(viewport);
    RECT rect = { 0, 0, 0, 0 };

    out_size->x = 0.0f;
    out_size->y = 0.0f;
    if (hwnd && GetClientRect(hwnd, &rect))
    {
        out_size->x = (float)(rect.right - rect.left);
        out_size->y = (float)(rect.bottom - rect.top);
    }
}

static void Hook_Platform_GetWindowFramebufferScale(ImGuiViewport* viewport, ImVec2* out_scale)
{
    UNREFERENCED_PARAMETER(viewport);
    out_scale->x = 1.0f;
    out_scale->y = 1.0f;
}

static void Hook_Platform_SetWindowFocus(ImGuiViewport* viewport)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    if (!data || !data->Hwnd)
        return;

    BringWindowToTop(data->Hwnd);
    SetForegroundWindow(data->Hwnd);
    SetFocus(data->Hwnd);
}

static bool Hook_Platform_GetWindowFocus(ImGuiViewport* viewport)
{
    HWND hwnd = Hook_GetHwndFromViewport(viewport);
    return hwnd && GetForegroundWindow() == hwnd;
}

static bool Hook_Platform_GetWindowMinimized(ImGuiViewport* viewport)
{
    HWND hwnd = Hook_GetHwndFromViewport(viewport);
    return hwnd && IsIconic(hwnd) != 0;
}

static void Hook_Platform_SetWindowTitle(ImGuiViewport* viewport, const char* title)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    int count;
    wchar_t stack_title[256];
    wchar_t* wide_title = stack_title;

    if (!data || !data->Hwnd)
        return;
    if (!title)
        title = "";

    count = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    if (count <= 0)
        return;

    if (count > ARRAYSIZE(stack_title))
    {
        wide_title = (wchar_t*)CIM_ALLOC((size_t)count * sizeof(wchar_t));
        if (!wide_title)
            return;
    }

    MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title, count);
    DefWindowProcW(data->Hwnd, WM_SETTEXT, 0, (LPARAM)wide_title);

    if (wide_title != stack_title)
        CIM_FREE(wide_title);
}

static void Hook_Platform_SetWindowAlpha(ImGuiViewport* viewport, float alpha)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    DWORD ex_style;
    if (!data || !data->Hwnd)
        return;

    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    ex_style = (DWORD)GetWindowLongPtr(data->Hwnd, GWL_EXSTYLE);
    if (alpha < 1.0f)
    {
        SetWindowLongPtr(data->Hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
        SetLayeredWindowAttributes(data->Hwnd, 0, (BYTE)(255.0f * alpha), LWA_ALPHA);
    }
    else
    {
        SetWindowLongPtr(data->Hwnd, GWL_EXSTYLE, ex_style & ~WS_EX_LAYERED);
    }
}

static void Hook_Platform_UpdateWindow(ImGuiViewport* viewport)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    DWORD next_style;
    DWORD next_ex_style;
    HWND next_parent;
    if (!data || !data->Hwnd)
        return;

    next_parent = Hook_GetHwndFromViewport(viewport->ParentViewport);
    if (next_parent != data->HwndParent)
    {
        if (IsOGLWindowInModalSizeMove(data->Hwnd))
            return;
        data->HwndParent = next_parent;
        SetWindowLongPtr(data->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)data->HwndParent);
    }

    Hook_GetWin32StyleFromViewportFlags(viewport->Flags, &next_style, &next_ex_style);
    if (data->DwStyle != next_style || data->DwExStyle != next_ex_style)
    {
        if (IsOGLWindowInModalSizeMove(data->Hwnd))
            return;
        BOOL top_most_changed = (data->DwExStyle & WS_EX_TOPMOST) != (next_ex_style & WS_EX_TOPMOST);
        HWND insert_after = top_most_changed ? ((viewport->Flags & ImGuiViewportFlags_TopMost) ? HWND_TOPMOST : HWND_NOTOPMOST) : NULL;
        UINT swp_flags = top_most_changed ? 0 : SWP_NOZORDER;
        RECT rect = {
            (LONG)viewport->Pos.x,
            (LONG)viewport->Pos.y,
            (LONG)(viewport->Pos.x + viewport->Size.x),
            (LONG)(viewport->Pos.y + viewport->Size.y)
        };

        data->DwStyle = next_style;
        data->DwExStyle = next_ex_style;
        SetWindowLongPtr(data->Hwnd, GWL_STYLE, data->DwStyle);
        SetWindowLongPtr(data->Hwnd, GWL_EXSTYLE, data->DwExStyle);
        Hook_AdjustWindowRect(viewport, &rect, data->DwStyle, data->DwExStyle);
        SetWindowPos(data->Hwnd, insert_after, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, swp_flags | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOREDRAW | SWP_NOCOPYBITS);
        ShowWindow(data->Hwnd, SW_SHOWNA);
        viewport->PlatformRequestMove = true;
        viewport->PlatformRequestResize = true;
    }
}

static void Hook_Platform_RenderWindow(ImGuiViewport* viewport, void* user_data)
{
    WGLSURFACE* surface;
    UNREFERENCED_PARAMETER(user_data);

    surface = Hook_GetViewportSurface(viewport);
    if (surface)
        wglMakeCurrent(surface->pbdc, surface->pbrc);
}

static void Hook_Platform_SwapBuffers(ImGuiViewport* viewport, void* user_data)
{
    OGLViewportData* data = Hook_GetViewportData(viewport);
    WGLSURFACE* surface;
    UNREFERENCED_PARAMETER(user_data);

    if (!data || !data->Hwnd)
        return;

    surface = Hook_GetViewportSurface(viewport);
    if (surface)
        wglMakeCurrent(surface->pbdc, surface->pbrc);
    PresentOGLWindow(data->Hwnd);
}

static void Hook_RenderOnePlatformWindow(ImGuiPlatformIO* platform_io, ImGuiViewport* viewport)
{
    if (!platform_io || !viewport)
        return;
    if (viewport->Flags & ImGuiViewportFlags_IsMinimized)
        return;

    if (platform_io->Platform_RenderWindow)
        platform_io->Platform_RenderWindow(viewport, NULL);
    if (platform_io->Renderer_RenderWindow)
        platform_io->Renderer_RenderWindow(viewport, NULL);
    if (platform_io->Platform_SwapBuffers)
        platform_io->Platform_SwapBuffers(viewport, NULL);
    if (platform_io->Renderer_SwapBuffers)
        platform_io->Renderer_SwapBuffers(viewport, NULL);
}

static void Hook_RenderPlatformWindows(HWND priority_hwnd)
{
    ImGuiPlatformIO* platform_io = ImGui_GetPlatformIO();
    ImGuiViewport* priority_viewport = NULL;
    int i;

    for (i = 1; i < platform_io->Viewports.Size; ++i)
    {
        ImGuiViewport* viewport = platform_io->Viewports.Data[i];
        if ((HWND)viewport->PlatformHandle == priority_hwnd)
        {
            priority_viewport = viewport;
            break;
        }
    }

    if (priority_viewport)
        Hook_RenderOnePlatformWindow(platform_io, priority_viewport);

    for (i = 1; i < platform_io->Viewports.Size; ++i)
    {
        ImGuiViewport* viewport = platform_io->Viewports.Data[i];
        if (viewport != priority_viewport)
            Hook_RenderOnePlatformWindow(platform_io, viewport);
    }
}

static float Hook_Platform_GetWindowDpiScale(ImGuiViewport* viewport)
{
    HWND hwnd = Hook_GetHwndFromViewport(viewport);
    return hwnd ? cImGui_ImplWin32_GetDpiScaleForHwnd((void*)hwnd) : 1.0f;
}

static void Hook_Renderer_CreateWindow(ImGuiViewport* viewport)
{
    WGLSURFACE* surface;
    assert(viewport->RendererUserData == NULL);

    surface = Hook_GetViewportSurface(viewport);
    if (!surface)
        return;

    if (g_hRC && surface->pbrc && surface->pbrc != g_hRC)
    {
        HDC previous_dc = wglGetCurrentDC();
        HGLRC previous_rc = wglGetCurrentContext();
        wglMakeCurrent(NULL, NULL);
        wglShareLists(g_hRC, surface->pbrc);
        if (previous_rc)
            wglMakeCurrent(previous_dc, previous_rc);
    }
    viewport->RendererUserData = surface;
}

static void Hook_Renderer_DestroyWindow(ImGuiViewport* viewport)
{
    viewport->RendererUserData = NULL;
}

static void Hook_Renderer_RenderWindow(ImGuiViewport* viewport, void* user_data)
{
    WGLSURFACE* surface = (WGLSURFACE*)viewport->RendererUserData;

    if (!surface)
        return;
    if (!wglMakeCurrent(surface->pbdc, surface->pbrc))
        return;

    if (g_OpenGL_Renderer_RenderWindow)
    {
        g_OpenGL_Renderer_RenderWindow(viewport, user_data);
        return;
    }

    if (!(viewport->Flags & ImGuiViewportFlags_NoRendererClear))
    {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    cImGui_ImplOpenGL3_RenderDrawData(viewport->DrawData);
}

static void InstallOGLViewportHooks(void)
{
    ImGuiPlatformIO* platform_io = ImGui_GetPlatformIO();

    g_Win32_Platform_DestroyWindow = platform_io->Platform_DestroyWindow;
    g_OpenGL_Renderer_RenderWindow = platform_io->Renderer_RenderWindow;

    platform_io->Platform_CreateWindow = Hook_Platform_CreateWindow;
    platform_io->Platform_DestroyWindow = Hook_Platform_DestroyWindow;
    platform_io->Platform_ShowWindow = Hook_Platform_ShowWindow;
    platform_io->Platform_SetWindowPos = Hook_Platform_SetWindowPos;
    ImGuiPlatformIO_SetPlatform_GetWindowPos(Hook_Platform_GetWindowPos);
    platform_io->Platform_SetWindowSize = Hook_Platform_SetWindowSize;
    ImGuiPlatformIO_SetPlatform_GetWindowSize(Hook_Platform_GetWindowSize);
    ImGuiPlatformIO_SetPlatform_GetWindowFramebufferScale(Hook_Platform_GetWindowFramebufferScale);
    platform_io->Platform_SetWindowFocus = Hook_Platform_SetWindowFocus;
    platform_io->Platform_GetWindowFocus = Hook_Platform_GetWindowFocus;
    platform_io->Platform_GetWindowMinimized = Hook_Platform_GetWindowMinimized;
    platform_io->Platform_SetWindowTitle = Hook_Platform_SetWindowTitle;
    platform_io->Platform_SetWindowAlpha = Hook_Platform_SetWindowAlpha;
    platform_io->Platform_UpdateWindow = Hook_Platform_UpdateWindow;
    platform_io->Platform_RenderWindow = Hook_Platform_RenderWindow;
    platform_io->Platform_SwapBuffers = Hook_Platform_SwapBuffers;
    platform_io->Platform_GetWindowDpiScale = Hook_Platform_GetWindowDpiScale;

    platform_io->Renderer_CreateWindow = Hook_Renderer_CreateWindow;
    platform_io->Renderer_DestroyWindow = Hook_Renderer_DestroyWindow;
    platform_io->Renderer_RenderWindow = Hook_Renderer_RenderWindow;
    platform_io->Renderer_SwapBuffers = NULL;
}
