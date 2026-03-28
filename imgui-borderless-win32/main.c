#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <tchar.h>
#include <GL/gl.h>
#include "oglwindow.h"
#include "swcadef.h"     

#include "dcimgui/dcimgui.h"
#include "dcimgui/dcimgui_internal.h"
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

static void ImGuiBorderlessWin32_ShowDemoWindow(HWND hWnd, ImVec4* clearColor);

// Data stored per platform window
typedef struct WGL_WindowData { HDC hDC; } WGL_WindowData;

// Data
static HGLRC            g_hRC;
static WGL_WindowData   g_MainWindow;
static int              g_Width;
static int              g_Height;

// Forward declarations of helper functions
int CreateDeviceWGL(HWND hWnd, WGL_WindowData* data);
void CleanupDeviceWGL(HWND hWnd, WGL_WindowData* data);
//void ResetDeviceWGL();

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

  // imgui-borderless-win32 Demo
  {
    ImGuiBorderlessWin32_ShowDemoWindow(hWnd, &clear_color);
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
    ImGui_UpdatePlatformWindows();
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

static void Hook_Renderer_CreateWindow(ImGuiViewport* viewport);
static void Hook_Renderer_DestroyWindow(ImGuiViewport* viewport);
static void Hook_Platform_RenderWindow(ImGuiViewport* viewport, void* user_data);
static void Hook_Renderer_SwapBuffers(ImGuiViewport* viewport, void* user_data);
SUBCLASSPROC ImGuiSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (cImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
      return 1;

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
    LPVOID hMainFiber = ConvertThreadToFiber(NULL);
    LPVOID hMsgFiber = CreateFiber(0, MessageFiberProc, hMainFiber);

    HWND hwnd = OGLWindow_Create(TEXT("OGLWindow"), 0, 0, WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 1080, 720, GetModuleHandle(NULL),
      hMainFiber);

    //if (!CreateDeviceWGL(hwnd, &g_MainWindow))
    //{
    //    CleanupDeviceWGL(hwnd, &g_MainWindow);
    //    DestroyWindow(hwnd);
    //    //UnregisterClass(TEXT("OGLWindow"), GetModuleHandle(NULL));
    //    return 1;
    //}
    //
    //wglMakeCurrent(g_MainWindow.hDC, g_hRC);
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hwnd, 0);

    g_hRC = pwglSurf->pbrc;
    g_MainWindow.hDC = pwglSurf->pbdc;

    CIMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui_CreateContext(NULL);
    ImGuiIO* io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable Docking
    //io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;     // Enable Multi-Viewport / Platform Windows
    io->ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
    io->ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;
    io->BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;
    io->BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
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
    {
        ImGuiPlatformIO* platform_io = ImGui_GetPlatformIO();
        IM_ASSERT(platform_io->Renderer_CreateWindow == NULL);
        IM_ASSERT(platform_io->Renderer_DestroyWindow == NULL);
        IM_ASSERT(platform_io->Renderer_SwapBuffers == NULL);
        IM_ASSERT(platform_io->Platform_RenderWindow == NULL);
        platform_io->Renderer_CreateWindow = Hook_Renderer_CreateWindow;
        platform_io->Renderer_DestroyWindow = Hook_Renderer_DestroyWindow;
        platform_io->Renderer_SwapBuffers = Hook_Renderer_SwapBuffers;
        platform_io->Platform_RenderWindow = Hook_Platform_RenderWindow;
    }
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

        D3DKMTWaitForVerticalBlankEvent(&vbe);

        InvalidateRect(hwnd, 0, 0);
        //RedrawWindow(hwnd, 0, 0, RDW_ERASE | RDW_UPDATENOW);
        //wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc);
        draw(hwnd);
        glFinish();
        //wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc);
        //SwapBuffers(pwglSurf->pbdc);

    }

    cImGui_ImplOpenGL3_Shutdown();
    cImGui_ImplWin32_Shutdown();
    ImGui_DestroyPlatformWindows();
    ImGui_DestroyContext(ctx);

    CleanupDeviceWGL(hwnd, &g_MainWindow);
    wglDeleteContext(g_hRC);
    DestroyWindow(hwnd);

    return 0;
} // main

static const ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoDecoration       |
                                                  ImGuiWindowFlags_NoDocking          |
                                                  ImGuiWindowFlags_AlwaysAutoResize   |
                                                  ImGuiWindowFlags_NoFocusOnAppearing |
                                                  ImGuiWindowFlags_NoNav;
static void ImGuiBorderlessWin32_ShowDemoWindow(HWND hWnd, ImVec4* clearColor)
{
    ImGuiStyle* style = ImGui_GetStyle();
    
    float height = (2.0f * style->WindowPadding.y) + ImGui_GetFrameHeight();

    if (ImGui_BeginViewportSideBar("Demo Sidebar", ImGui_GetMainViewport(), ImGuiDir_Down, height, 0))
    {
        ImVec2 avail;
        ImVec2 label_size;
        const char* slider_label;
        int changes = 0;
        int window_mode = (int)g_ClientIsBorderless;
        changes |= ImGui_RadioButtonIntPtr("Windowed", &window_mode, 0);
        ImGui_SameLine();
        changes |= ImGui_RadioButtonIntPtr("Borderless", &window_mode, 1);
        ImGui_SameLine();
        avail = ImGui_GetContentRegionAvail();
        slider_label = "BgAlpha";
        label_size = ImGui_CalcTextSize(slider_label);
        ImGui_SetNextItemWidth(avail.x - label_size.x - style->ItemSpacing.x);
        ImGui_SliderFloat("BgAlpha", &(style->Colors[ImGuiCol_WindowBg].w), 1, 0);
    }
    ImGui_End();

    if (ImGui_Begin("imgui-borderless-win32", 0, 0))
    {
#if 0
        if (ImGui_Begin("Borderless Settings", 0, overlay_flags))
        {
            ImGuiIO*    io    = ImGui_GetIO();
            ImGuiStyle* style = ImGui_GetStyle();
            static const DWORD windowed = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
            static const DWORD borderless = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_VISIBLE;

            ImGui_BeginGroup();
            int        changes     = 0;
            static int window_mode = 0;
            window_mode = (int)g_ClientIsBorderless;
            changes |= ImGui_RadioButtonIntPtr("Windowed",   &window_mode, 0);
            changes |= ImGui_RadioButtonIntPtr("Borderless", &window_mode, 1);
            if (changes && (window_mode != g_ClientIsBorderless))
            {
                RECT r;
                GetWindowRect(hWnd, &r);
                INT caption          = GetSystemMetrics(SM_CYCAPTION);
                DWORD next_style     = (BOOL)window_mode ? borderless : windowed;
                g_ClientIsBorderless = (BOOL)window_mode;
                caption = (g_ClientIsBorderless) ? -caption : caption;
                (void)SetWindowLongPtr(hWnd, GWL_STYLE, (LONG)next_style);
                (void)SetWindowPos(hWnd, 0, 0, 0, (r.right - r.left), (r.bottom - r.top) + caption, SWP_FRAMECHANGED | SWP_NOMOVE |SWP_SHOWWINDOW);
            }
            ImGui_EndGroup();
            
            ImGui_SameLine();

            ImGui_BeginGroup();
            ImGui_Text(" %.3f ms/frame (%.1f FPS)", 1000.0f / io->Framerate, io->Framerate);
            if (ImGui_IsMousePosValid(&io->MousePos)) ImGui_Text("      MousePos: (%.1f,%.1f)", io->MousePos.x, io->MousePos.y);
            else                                      ImGui_Text("      MousePos: <invalid>");
            ImGui_EndGroup();

            ImGui_SeparatorText("ImGuiCol_WindowBgAlpha");
            ImGui_SliderFloat("BgAlpha", &(style->Colors[ImGuiCol_WindowBg].w), 1, 0);
        }
        ImGui_End();
#endif

        if (ImGui_CollapsingHeader("Win32", 0))
        {
            static int enable_alpha_compositing        = 0;
            static int enable_border_shadow            = 0;

            if (ImGui_Checkbox("Enable Alpha Compositing", &enable_alpha_compositing))
            {
                if (enable_alpha_compositing) cImGui_ImplWin32_EnableAlphaCompositing((void*)hWnd);
                else 
                {
                    DWM_BLURBEHIND bb = { 0 };
                    bb.dwFlags = DWM_BB_ENABLE;
                    bb.fEnable = FALSE;
                    DwmEnableBlurBehindWindow(hWnd, &bb);
                }
            }
            if (ImGui_Checkbox("Enable Border Shadow", &enable_border_shadow))
            {
                static const MARGINS margins[2] = { {0,0,0,0}, {1,1,1,1} };
                DwmExtendFrameIntoClientArea(hWnd, &margins[enable_border_shadow]);
            }
        }

        if (ImGui_CollapsingHeader("OpenGL", 0)) 
        {
            ImGui_SeparatorText("glClearColor");
            (void)ImGui_ColorPicker4("###glClearColorPicker", (float*)clearColor, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview, NULL);
        }
        if (ImGui_CollapsingHeader("DwmSetWindowAttribute", 0)) 
        {
            int changes = 0;

            ImGui_SeparatorText("DWM Gradient");
            static ImVec4 gradient_color = {114.0f / 255.0f, 144.0f / 255.0f, 154.0f / 255.0f, 100.0f / 255.0f};
            changes |= ImGui_ColorPicker4("###DwmGradientColorPicker", (float*)&gradient_color, ImGuiColorEditFlags_NoSidePreview |
                                                                                                 ImGuiColorEditFlags_NoSmallPreview, NULL);
            ImGui_SeparatorText("DWM Accent State");
            static INT accent_policy = ACCENT_ENABLE_BLURBEHIND;
            changes |= ImGui_RadioButtonIntPtr("DISABLED",             &accent_policy, ACCENT_DISABLED);
            changes |= ImGui_RadioButtonIntPtr("GRADIENT",             &accent_policy, ACCENT_ENABLE_GRADIENT);
            changes |= ImGui_RadioButtonIntPtr("TRANSPARENT GRADIENT", &accent_policy, ACCENT_ENABLE_TRANSPARENTGRADIENT);
            changes |= ImGui_RadioButtonIntPtr("BLUR BEHIND",          &accent_policy, ACCENT_ENABLE_BLURBEHIND);
            changes |= ImGui_RadioButtonIntPtr("ACRYLIC BLUR BEHIND",  &accent_policy, ACCENT_ENABLE_ACRYLICBLURBEHIND);
            changes |= ImGui_RadioButtonIntPtr("HOST BACKDROP",        &accent_policy, ACCENT_ENABLE_HOSTBACKDROP);
            changes |= ImGui_RadioButtonIntPtr("INVALID STATE",        &accent_policy, ACCENT_INVALID_STATE);

            ImGui_SeparatorText("DWM Accent Flags");
            static UINT accent_flags = 0;
            ImGui_SeparatorText("DWM Animation id");
            static LONG animation_id = 0;

            if (changes)
            {
                COLORREF accent_color = (((int)(gradient_color.w * 255)) << 24) |
                                        (((int)(gradient_color.z * 255)) << 16) |
                                        (((int)(gradient_color.y * 255)) << 8)  |
                                         ((int)(gradient_color.x * 255));

                ACCENT_POLICY policy = {
                    accent_policy,
                    accent_flags,
                    accent_color,
                    animation_id
                };

                const WINDOWCOMPOSITIONATTRIBDATA data = {
                    WCA_ACCENT_POLICY,
                    &policy,
                    sizeof(policy)
                };

                SetWindowCompositionAttribute(hWnd, &data);
            }
        }
    }
    ImGui_End();
}

int CreateDeviceWGL(HWND hWnd, WGL_WindowData* data)
{
    HDC hDc = GetDC(hWnd);
    PIXELFORMATDESCRIPTOR pfd = {
      sizeof(PIXELFORMATDESCRIPTOR),
      1,                                // Version Number
      PFD_DRAW_TO_WINDOW |              // Format Must Support Window
      PFD_SUPPORT_OPENGL |              // Format Must Support OpenGL
      PFD_SUPPORT_COMPOSITION |         // Format Must Support Composition
      PFD_DOUBLEBUFFER,                 // Must Support Double Buffering
      PFD_TYPE_RGBA,                    // Request An RGBA Format
      32,                               // Select Our Color Depth
      0, 0, 0, 0, 0, 0,                 // Color Bits Ignored
      8,                                // An Alpha Buffer
      0,                                // Shift Bit Ignored
      0,                                // No Accumulation Buffer
      0, 0, 0, 0,                       // Accumulation Bits Ignored
      24,                               // 16Bit Z-Buffer (Depth Buffer)
      8,                                // Some Stencil Buffer
      0,                                // No Auxiliary Buffer
      PFD_MAIN_PLANE,                   // Main Drawing Layer
      0,                                // Reserved
      0, 0, 0                           // Layer Masks Ignored
    };

    const int pf = ChoosePixelFormat(hDc, &pfd);
    if (pf == 0)
        return 0;
    if (SetPixelFormat(hDc, pf, &pfd) == FALSE)
        return 0;
    ReleaseDC(hWnd, hDc);

    data->hDC = GetDC(hWnd);
    if (!g_hRC)
        g_hRC = wglCreateContext(data->hDC);

    return 1;
}

void CleanupDeviceWGL(HWND hWnd, WGL_WindowData* data)
{
    wglMakeCurrent(NULL, NULL);
    ReleaseDC(hWnd, data->hDC);
}

static void Hook_Renderer_CreateWindow(ImGuiViewport* viewport)
{
    assert(viewport->RendererUserData == NULL);

    WGL_WindowData* data = (WGL_WindowData*)CIM_ALLOC(sizeof(WGL_WindowData));
    CreateDeviceWGL((HWND)viewport->PlatformHandle, data);
    viewport->RendererUserData = data;
}

static void Hook_Renderer_DestroyWindow(ImGuiViewport* viewport)
{
    if (viewport->RendererUserData != NULL)
    {
        WGL_WindowData* data = (WGL_WindowData*)viewport->RendererUserData;
        CleanupDeviceWGL((HWND)viewport->PlatformHandle, data);
        CIM_FREE(data);
        viewport->RendererUserData = NULL;
    }
}

static void Hook_Platform_RenderWindow(ImGuiViewport* viewport, void* user_data)
{
    WGL_WindowData* data;
    // Activate the platform window DC in the OpenGL rendering context
    if (data = (WGL_WindowData*)viewport->RendererUserData)
        wglMakeCurrent(data->hDC, g_hRC);
}

static void Hook_Renderer_SwapBuffers(ImGuiViewport* viewport, void* user_data)
{
    WGL_WindowData* data;
    
    if (data = (WGL_WindowData*)viewport->RendererUserData)
        SwapBuffers(data->hDC);
}