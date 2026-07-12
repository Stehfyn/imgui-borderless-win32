#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <tchar.h>
#include <assert.h>
#include <GL/gl.h>
#include "wglwindow.h"
#include "dwmframe.h"
#include "dxgipresent.h"
#include "imgui_impl_wglwindow.h"
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

// Data stored per platform window
typedef struct WGL_WindowData
{
    HDC   hDC;
    HGLRC hRC;
} WGL_WindowData;

// Data
static HGLRC            g_hRC;
static WGL_WindowData   g_MainWindow;
static HWND             g_MainHwnd;
static LPVOID           g_MainFiber;

static void ThemeAnimTick(HWND hWnd);

static void draw(HWND hWnd)
{
  static ImVec4 clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
  ImGuiIO* io = ImGui_GetIO();
  WGLSURFACE* pwglSurfFrame = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

  /* InFrame latch (reference): viewport create/destroy inside NewFrame /
   * UpdatePlatformWindows sends WM_WINDOWPOSCHANGED / WM_ACTIVATE to this
   * window synchronously mid-frame; a modal repaint from there would
   * re-enter the frame. */
  if (pwglSurfFrame)
    pwglSurfFrame->in_frame = TRUE;

  ThemeAnimTick(hWnd);
  cImGui_ImplOpenGL3_NewFrame();
  cImGui_ImplWin32_NewFrame();

  /* Driven size (ImmersiveWindow pending-rect present): during the
   * WM_NCCALCSIZE pre-geometry repaint the frame must lay out at the size
   * the window is ABOUT to have — GetClientRect (what the win32 backend
   * just used for DisplaySize) still reports the old size there. */
  {
    SIZE szDriven;
    if (GetWGLWindowDrivenClientSize(hWnd, &szDriven))
    {
      ImGuiViewport* mv = ImGui_GetMainViewport();
      io->DisplaySize.x = (float)szDriven.cx;
      io->DisplaySize.y = (float)szDriven.cy;
      mv->Size = io->DisplaySize;
    }
  }
  ImGui_NewFrame();

  /* Client == window (dwmframe): the caption band paints over the top capH
   * pixels; inset the viewport work area so the dockspace and any windows
   * lay out below the band.  Applied per frame (locks in next NewFrame). */
  {
    ImGuiViewportP* vp = (ImGuiViewportP*)ImGui_GetMainViewport();
    vp->BuildWorkInsetMin.y += (float)DwmFrameCaptionHeight(hWnd);
  }

  // Dockspace
  {
    ImGui_DockSpaceOverViewport();
  }

  // ImGui Demo
  {
    ImGui_ShowDemoWindow(NULL);
  }

  {
    if (0 == (ImGui_GetFrameCount() % 180))
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
    HWND sync_resize_hwnd = GetWGLWindowSynchronousResizeHwnd(hWnd);
    ImGui_UpdatePlatformWindows();
    if (sync_resize_hwnd && !GetPropA(sync_resize_hwnd, WGLWINDOW_SECONDARY_VIEWPORT_PROP))
        sync_resize_hwnd = NULL;
    ImGui_ImplWGLWindow_RenderPlatformWindows(sync_resize_hwnd);

    // Restore the OpenGL rendering context to the main window DC, since platform windows might have changed it.
    wglMakeCurrent(g_MainWindow.hDC, g_hRC);
  }

  /* No SwapBuffers: the pbuffer is never displayed by GL, and swapping it
   * would rotate the frame just rendered out of the read buffer — the
   * present would then deliver the PREVIOUS frame, pairing every resize
   * tick's new geometry with old-size content.
   * The frame ends here (clear the latch before the present, reference
   * PresentFrame shape), then present on this stack with the flavor the
   * window control pinned for the calling context. */
  if (pwglSurfFrame)
    pwglSurfFrame->in_frame = FALSE;
  PresentWGLWindow(hWnd);
}

/* Light/dark theme (dwmframe seam): the imgui style colors are SLAVED to
 * the chrome's own 160ms crossfade — dwmframe owns the timeline, this just
 * reads (from, to, t) each frame and blends the two palettes — so the
 * content always matches the caption mid-transition, with no animation
 * state of its own.  Colors only — sizes/scales stay.  Skipped while the
 * blend inputs are unchanged, so runtime style-color edits survive outside
 * transitions. */
static void ThemeAnimTick(HWND hWnd)
{
    static ImGuiStyle dark;    /* palettes, built once (large: off-stack) */
    static ImGuiStyle light;
    static BOOL  fBuilt;
    static BOOL  fToLast   = -1;
    static BOOL  fFromLast = -1;
    static float tLast     = -1.0f;

    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    ImGuiStyle* style;
    BOOL  fTo;
    BOOL  fFrom;
    float t;
    int   i;

    if (!pwglSurf || !pwglSurf->frame)
      return;

    if (!fBuilt)
    {
      ImGui_StyleColorsDark(&dark);
      ImGui_StyleColorsLight(&light);
      fBuilt = TRUE;
    }

    DwmFrameGetThemeAnim(pwglSurf->frame, &fTo, &fFrom, &t);
    if (fTo == fToLast && fFrom == fFromLast && t == tLast)
      return;
    fToLast   = fTo;
    fFromLast = fFrom;
    tLast     = t;

    style = ImGui_GetStyle();
    for (i = 0; i < ImGuiCol_COUNT; ++i)
    {
      const ImVec4* a = fFrom ? &dark.Colors[i] : &light.Colors[i];
      const ImVec4* b = fTo   ? &dark.Colors[i] : &light.Colors[i];
      style->Colors[i].x = a->x + (b->x - a->x) * t;
      style->Colors[i].y = a->y + (b->y - a->y) * t;
      style->Colors[i].z = a->z + (b->z - a->z) * t;
      style->Colors[i].w = a->w + (b->w - a->w) * t;
    }
}

static void __stdcall render(HWND hWnd)
{
    HDC hdc = BeginWGLWindowPaint(hWnd);

    if (g_ClientRenderFunction)
    {
      g_ClientRenderFunction(hWnd);
      //DwmFlush();
    }

    EndWGLWindowPaint(hdc);
}

static LRESULT CALLBACK ImGuiSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK ImGuiSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ImGuiContext* ctx = (ImGuiContext*)GetPropA(hWnd, "IMGUI_CONTEXT");
    if (ctx)
        ImGui_SetCurrentContext(ctx);

    /* Teardown tail: the final DestroyWindow(main) runs AFTER
     * ImGui_DestroyContext — its WM_DESTROY/WM_NCDESTROY must not touch
     * imgui (0xC000041D in this proc otherwise). */
    if (!ImGui_GetCurrentContext())
        return DefWGLWindowProc(hWnd, uMsg, wParam, lParam);

    ImGuiViewport* viewport = ImGui_FindViewportByPlatformHandle((void*)hWnd);
    if (viewport == ImGui_GetMainViewport() && uMsg == WM_CLOSE)
    {
        /* Quit through the render loop's teardown: the secondary platform
         * windows must be destroyed BEFORE this window — every viewport
         * renders and presents through THIS window's GL context (g_hRC).
         * DestroyWindow here deleted that shared context under the live
         * secondary presenters (CoreMessaging fail-fast). */
        PostQuitMessage(0);
        return 0;
    }

    /* A capture-tracked caption-button press owns the mouse stream BEFORE
     * imgui sees it: imgui never saw the NC button-down, so its WM_LBUTTONUP
     * path would ReleaseCapture and the synchronous WM_CAPTURECHANGED would
     * cancel the press before the commit ran. */
    if (IsWGLWindowCaptionPressActive(hWnd) &&
        (uMsg == WM_MOUSEMOVE || uMsg == WM_LBUTTONUP || uMsg == WM_CAPTURECHANGED))
        return DefWGLWindowProc(hWnd, uMsg, wParam, lParam);

    if (cImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
      return 1;

    return DefWGLWindowProc(hWnd, uMsg, wParam, lParam);
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
    
    InitWGLControls();
    g_MainFiber = ConvertThreadToFiber(NULL);
    LPVOID hMsgFiber = CreateFiber(0, MessageFiberProc, g_MainFiber);

    /* Canonical immersive-window creation (reference verbatim):
     * WS_EX_NOREDIRECTIONBITMAP + BCS_WINDOW.  NO WS_CAPTION — with it, DWM
     * draws the standard frame geometry into the nonclient border strips
     * (visible borders around the client) and refights the composed chrome
     * on every resize tick.  GL renders into the pbuffer; each present
     * delivers client + caption chrome through the composition swapchain's
     * present ladder as one atomic present. */
    HWND hwnd = WGLWindow_CreateEx(WS_EX_NOREDIRECTIONBITMAP, TEXT("WGLWindow"), 0, 0,
      BCS_WINDOW,
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
        ImGui_ImplWGLWindow_Init(hwnd, g_MainFiber);
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
        if (WGLWindowQuitPosted())
            break;

        /* draw() presents directly; no InvalidateRect — WM_PAINT is for
         * genuine exposure only. */
        draw(hwnd);
        if (!IsWGLWindowInSynchronousResizeRender(hwnd))
            D3DKMTWaitForVerticalBlankEvent(&vbe);

    }

    /* Shutdown order (contract): the GL renderer's Shutdown destroys all
     * platform windows FIRST (through this module's handlers, renderer and
     * shared GL context still alive), then this module, then the input
     * backend, then the context. */
    wglMakeCurrent(g_MainWindow.hDC, g_hRC);
    cImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWGLWindow_Shutdown();
    cImGui_ImplWin32_Shutdown();
    ImGui_DestroyPlatformWindows();
    ImGui_DestroyContext(ctx);

    DestroyWindow(hwnd);

    return 0;
} // main

