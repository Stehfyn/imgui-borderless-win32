extern "C"{
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <GL/gl.h>
#include "win32_window.h"
#include "swcadef.h"      // Courtesy of https://gist.github.com/sylveon/9c199bb6684fe7dffcba1e3d383fb609
}

#pragma comment (lib, "shcore")
#pragma comment (lib, "dwmapi")
#pragma comment (lib, "opengl32")
#pragma comment (lib, "glu32")

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"

#include <functional>
#include <vector>
#include <string>

LRESULT WINAPI WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static std::function<void(HWND hWnd)> g_ClientRenderFunction;
static std::vector<RECT>              g_ClientCustomClientArea;
static BOOL                           g_ClientIsBorderless      = FALSE;

namespace ImGuiBorderlessWin32 {
static constexpr DWORD windowed   = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
static constexpr DWORD borderless = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_VISIBLE;
static void ShowDemoWindow(HWND hWnd, ImVec4& clearColor);
}

// Data stored per platform window
struct WGL_WindowData { HDC hDC; };

// Data
static HGLRC            g_hRC;
static WGL_WindowData   g_MainWindow;
static int              g_Width;
static int              g_Height;

// Forward declarations of helper functions
bool CreateDeviceWGL(HWND hWnd, WGL_WindowData* data);
void CleanupDeviceWGL(HWND hWnd, WGL_WindowData* data);
//void ResetDeviceWGL();

static void Hook_Renderer_CreateWindow(ImGuiViewport* viewport);
static void Hook_Renderer_DestroyWindow(ImGuiViewport* viewport);
static void Hook_Platform_RenderWindow(ImGuiViewport* viewport, void*);
static void Hook_Renderer_SwapBuffers(ImGuiViewport* viewport, void*);

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
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    win32_window_t win32_window = { 0 };

    HRESULT hr = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE); // This can be set by a program's manifest or its corresponding registry settings
    if (E_INVALIDARG == hr)
    {
        return 1;
    }

    win32_window_create(&win32_window, 1080, 720, 0, ImGuiBorderlessWin32::windowed);
    win32_window.msgHook = (win32_wndproc_hook_t)WndProcHook;

    if (!CreateDeviceWGL(win32_window.hWnd, &g_MainWindow))
    {
        CleanupDeviceWGL(win32_window.hWnd, &g_MainWindow);
        ::DestroyWindow(win32_window.hWnd);
        ::UnregisterClass(win32_window.tcClassName, GetModuleHandle(NULL));
        return 1;
    }

    wglMakeCurrent(g_MainWindow.hDC, g_hRC);

    ::ShowWindow(win32_window.hWnd, SW_SHOWDEFAULT);
    ::UpdateWindow(win32_window.hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;     // Enable Multi-Viewport / Platform Windows
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_InitForOpenGL((void*)win32_window.hWnd);
    ImGui_ImplOpenGL3_Init();
    ImGui_ImplWin32_EnableDpiAwareness();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        IM_ASSERT(platform_io.Renderer_CreateWindow == NULL);
        IM_ASSERT(platform_io.Renderer_DestroyWindow == NULL);
        IM_ASSERT(platform_io.Renderer_SwapBuffers == NULL);
        IM_ASSERT(platform_io.Platform_RenderWindow == NULL);
        platform_io.Renderer_CreateWindow = Hook_Renderer_CreateWindow;
        platform_io.Renderer_DestroyWindow = Hook_Renderer_DestroyWindow;
        platform_io.Renderer_SwapBuffers = Hook_Renderer_SwapBuffers;
        platform_io.Platform_RenderWindow = Hook_Platform_RenderWindow;
    }

    for(;;)
    {
        MSG msg; //- Pump message loop; break on WM_QUIT
        if (!win32_window_pump_message_loop(&win32_window, &msg, FALSE)) break;

        //- Set the client render function callback if not done so already (So we can also render in sizemoves)
        if (!g_ClientRenderFunction)
        {
            g_ClientRenderFunction = [](HWND hWnd) {
                static ImVec4 clear_color(.0f, .0f, .0f, .0f);
                ImGuiIO& io = ImGui::GetIO();

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                // Dockspace
                {
                    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
                }

                // ImGui Demo
                {
                    ImGui::ShowDemoWindow();
                }

                // imgui-borderless-win32 Demo
                {
                    ImGuiBorderlessWin32::ShowDemoWindow(hWnd, clear_color);
                }

                // Rendering
                ImGui::Render();
                glViewport(0, 0, g_Width, g_Height);
                glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
                glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

                // Update and Render additional Platform Windows
                if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
                {
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();

                    // Restore the OpenGL rendering context to the main window DC, since platform windows might have changed it.
                    wglMakeCurrent(g_MainWindow.hDC, g_hRC);
                }

                // Update imgui window rects for hit testing
                {
                    ImVec2 origin = { 0, 0 };
                    if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) // Only apply offset if Multi-viewports are not enabled
                    {
                        RECT r;
                        GetWindowRect(hWnd, &r); // Get ScreenPos offset
                        origin = { (float)r.left, (float)r.top };
                    }

                    // Add imgui windows that aren't default rects/dockspaces/windows over viewports/etc to client area whitelist,
                    // but explicitly include imgui demo
                    std::vector<RECT> WindowRects;
                    for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
                    {
                        if(window->Active)
                        { 
                            if ((!(std::string(window->Name).find("Default") != std::string::npos)  &&
                                (!(std::string(window->Name).find("Dock")    != std::string::npos)) &&
                                (!(std::string(window->Name).find("Menu")    != std::string::npos)) &&
                                (!(std::string(window->Name).find("WindowOverViewport") != std::string::npos))) ||
                                (std::string(window->Name).find("Dear ImGui Demo") != std::string::npos))
                            {
                                ImVec2 pos  = window->Pos;
                                ImVec2 size = window->Size;
                                RECT   rect = { (LONG)(origin.x + pos.x),
                                                (LONG)(origin.y + pos.y),
                                                (LONG)(origin.x + (pos.x + size.x)),
                                                (LONG)(origin.y + (pos.y + size.y)) };

                                WindowRects.push_back(rect);
                            }
                        }
                    }
                    g_ClientCustomClientArea = std::move(WindowRects);
                }

                // Present
                ::SwapBuffers(g_MainWindow.hDC);
            };
        }
        
        //- Then just make that client render call like usual
        g_ClientRenderFunction(win32_window.hWnd);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceWGL(win32_window.hWnd, &g_MainWindow);
    wglDeleteContext(g_hRC);
    ::DestroyWindow(win32_window.hWnd);
    ::UnregisterClass(win32_window.tcClassName, GetModuleHandle(NULL));

    return 0;
} // main

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static constexpr UINT timer_id = 0;

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;

    switch (msg) {
    case WM_NCCALCSIZE: {
        if (wParam && g_ClientIsBorderless) {
            NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lParam;
            if(IsMaximized(hWnd)) {

                HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONULL);
                if (!monitor) return 0;

                MONITORINFO monitor_info = {0};
                monitor_info.cbSize = sizeof(monitor_info);
                if (!GetMonitorInfo(monitor, &monitor_info)) return 0;

                // when maximized, make the client area fill just the monitor (without task bar) rect,
                // not the whole window rect which extends beyond the monitor.
                params->rgrc[0] = monitor_info.rcWork;
                return 0;
            }
            else {
                params->rgrc[0].bottom += 1;
                return WVR_VALIDRECTS;
            }
        }
        break;
    }
    case WM_NCHITTEST: {
        // When we have no border or title bar, we need to perform our
        // own hit testing to allow resizing and moving.
        const POINT cursor = POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const POINT border{
            ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER), 
            ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER) // Padded border is symmetric for both x, y
        };
        RECT window; 
        if (!::GetWindowRect(hWnd, &window)) return HTNOWHERE;

        enum region_mask {
            client = 0b0000,
            left   = 0b0001,
            right  = 0b0010,
            top    = 0b0100,
            bottom = 0b1000,
        };

        const int result =
            left   * (cursor.x <  (window.left   + border.x)) |
            right  * (cursor.x >= (window.right  - border.x)) |
            top    * (cursor.y <  (window.top    + border.y)) |
            bottom * (cursor.y >= (window.bottom - border.y));

        switch (result) {
        case left:           return HTLEFT;
        case right:          return HTRIGHT;
        case top:            return HTTOP;
        case bottom:         return HTBOTTOM;
        case top | left:     return HTTOPLEFT;
        case top | right:    return HTTOPRIGHT;
        case bottom | left:  return HTBOTTOMLEFT;
        case bottom | right: return HTBOTTOMRIGHT;
        case client:{
            for (RECT rect : g_ClientCustomClientArea)
                if (PtInRect(&rect, cursor)) return HTCLIENT;
            return HTCAPTION;
        }
        default: return HTNOWHERE;
        }
        break;
    }
    case WM_SIZE: {
        if (wParam != SIZE_MINIMIZED)
        {
            g_Width  = LOWORD(lParam);
            g_Height = HIWORD(lParam);
        }
        return 0;
    }
    case WM_ENTERSIZEMOVE: {
        SetTimer(hWnd, (UINT_PTR)&timer_id, USER_TIMER_MINIMUM, NULL); // Start the render timer wince we'll be stuck modally in the message loop
        break;
    }
    case WM_EXITSIZEMOVE: {
        KillTimer(hWnd, (UINT_PTR)&timer_id); // Kill the timer since we're no longer stuck
        break;
    }
    case WM_TIMER: {
        if (g_ClientRenderFunction) {
            g_ClientRenderFunction(hWnd);
            DwmFlush();
        }
        return 1;
    }
    case WM_ERASEBKGND: // Prevent flicker when we're rendering during resize
        return 1;

    case WM_SYSCOMMAND: {
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default: break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}



namespace ImGuiBorderlessWin32 {
static constexpr ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoDecoration       |
                                                  ImGuiWindowFlags_NoDocking          |
                                                  ImGuiWindowFlags_AlwaysAutoResize   |
                                                  ImGuiWindowFlags_NoFocusOnAppearing |
                                                  ImGuiWindowFlags_NoNav;
void ShowDemoWindow(HWND hWnd, ImVec4& clearColor)
{
    static bool swap_styles = false;

    if (ImGui::Begin("imgui-borderless-win32"))
    {
        if (ImGui::Begin("Borderless Settings", 0, overlay_flags))
        {
            ImGuiIO&    io    = ImGui::GetIO();
            ImGuiStyle& style = ImGui::GetStyle();

            ImGui::BeginGroup();
            bool       changes     = false;
            static int window_mode = static_cast<int>(g_ClientIsBorderless);
            changes |= ImGui::RadioButton("Windowed",   &window_mode, 0);
            changes |= ImGui::RadioButton("Borderless", &window_mode, 1);
            if (changes && (static_cast<BOOL>(window_mode) != g_ClientIsBorderless)) {
                RECT r;
                GetWindowRect(hWnd, &r);
                INT caption          = GetSystemMetrics(SM_CYCAPTION);
                DWORD next_style     = (static_cast<BOOL>(window_mode)) ? borderless : windowed;
                g_ClientIsBorderless = static_cast<BOOL>(window_mode);
                caption = (g_ClientIsBorderless) ? -caption : caption;
                (void)SetWindowLongPtr(hWnd, GWL_STYLE, static_cast<LONG>(next_style));
                (void)SetWindowPos(hWnd, nullptr, 0, 0, (r.right - r.left), (r.bottom - r.top) + caption, SWP_FRAMECHANGED |
                                                                                                          SWP_NOMOVE       |
                                                                                                          SWP_SHOWWINDOW);
            }
            ImGui::EndGroup();
            
            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::Text(" %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            if (ImGui::IsMousePosValid()) ImGui::Text("      MousePos: (%.1f,%.1f)", io.MousePos.x, io.MousePos.y);
            else                          ImGui::Text("      MousePos: <invalid>");
            ImGui::EndGroup();

            ImGui::SeparatorText("ImGuiCol_WindowBgAlpha");
            ImGui::SliderFloat("BgAlpha", &(style.Colors[ImGuiCol_WindowBg].w), 1, 0);
        }
        ImGui::End();

        if (ImGui::CollapsingHeader("Win32"))
        {
            static bool enable_alpha_compositing        = false;
            static bool enable_border_shadow            = false;

            if (ImGui::Checkbox("Enable Alpha Compositing", &enable_alpha_compositing))
            {
                if (enable_alpha_compositing) ImGui_ImplWin32_EnableAlphaCompositing((void*)hWnd);
                else 
                {
                    DWM_BLURBEHIND bb = {};
                    bb.dwFlags = DWM_BB_ENABLE;
                    bb.fEnable = FALSE;
                    ::DwmEnableBlurBehindWindow(hWnd, &bb);
                }
            }
            if (ImGui::Checkbox("Enable Border Shadow", &enable_border_shadow))
            {
                static const MARGINS margins[2] = { {0,0,0,0}, {1,1,1,1} };
                ::DwmExtendFrameIntoClientArea(hWnd, &margins[enable_border_shadow]);
            }
        }

        if (ImGui::CollapsingHeader("OpenGL")) 
        {
            ImGui::SeparatorText("glClearColor");
            (void)ImGui::ColorPicker4("###glClearColorPicker", (float*)&clearColor, ImGuiColorEditFlags_NoSidePreview |
                                                                                    ImGuiColorEditFlags_NoSmallPreview);
        }
        if (ImGui::CollapsingHeader("DwmSetWindowAttribute")) 
        {
            bool changes = false;

            ImGui::SeparatorText("DWM Gradient");
            static ImVec4 gradient_color = ImVec4(114.0f / 255.0f, 144.0f / 255.0f, 154.0f / 255.0f, 100.0f / 255.0f);
            changes |= ImGui::ColorPicker4("###DwmGradientColorPicker", (float*)&gradient_color, ImGuiColorEditFlags_NoSidePreview |
                                                                                                 ImGuiColorEditFlags_NoSmallPreview);
            ImGui::SeparatorText("DWM Accent State");
            static INT accent_policy = ACCENT_ENABLE_BLURBEHIND;
            changes |= ImGui::RadioButton("DISABLED",             &accent_policy, ACCENT_DISABLED);
            changes |= ImGui::RadioButton("GRADIENT",             &accent_policy, ACCENT_ENABLE_GRADIENT);
            changes |= ImGui::RadioButton("TRANSPARENT GRADIENT", &accent_policy, ACCENT_ENABLE_TRANSPARENTGRADIENT);
            changes |= ImGui::RadioButton("BLUR BEHIND",          &accent_policy, ACCENT_ENABLE_BLURBEHIND);
            changes |= ImGui::RadioButton("ACRYLIC BLUR BEHIND",  &accent_policy, ACCENT_ENABLE_ACRYLICBLURBEHIND);
            changes |= ImGui::RadioButton("HOST BACKDROP",        &accent_policy, ACCENT_ENABLE_HOSTBACKDROP);
            changes |= ImGui::RadioButton("INVALID STATE",        &accent_policy, ACCENT_INVALID_STATE);

            ImGui::SeparatorText("DWM Accent Flags");
            static UINT accent_flags = 0;
            ImGui::SeparatorText("DWM Animation id");
            static LONG animation_id = 0;

            if (changes)
            {
                COLORREF accent_color = (((int)(gradient_color.w * 255)) << 24) |
                                        (((int)(gradient_color.z * 255)) << 16) |
                                        (((int)(gradient_color.y * 255)) << 8)  |
                                         ((int)(gradient_color.x * 255));

                ACCENT_POLICY policy = {
                    ACCENT_STATE(accent_policy),
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
    ImGui::End();
}
}

bool CreateDeviceWGL(HWND hWnd, WGL_WindowData* data)
{
    HDC hDc = ::GetDC(hWnd);
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

    const int pf = ::ChoosePixelFormat(hDc, &pfd);
    if (pf == 0)
        return false;
    if (::SetPixelFormat(hDc, pf, &pfd) == FALSE)
        return false;
    ::ReleaseDC(hWnd, hDc);

    data->hDC = ::GetDC(hWnd);
    if (!g_hRC)
        g_hRC = wglCreateContext(data->hDC);
    return true;
}

void CleanupDeviceWGL(HWND hWnd, WGL_WindowData* data)
{
    wglMakeCurrent(NULL, NULL);
    ::ReleaseDC(hWnd, data->hDC);
}

static void Hook_Renderer_CreateWindow(ImGuiViewport* viewport)
{
    assert(viewport->RendererUserData == NULL);

    WGL_WindowData* data = IM_NEW(WGL_WindowData);
    CreateDeviceWGL((HWND)viewport->PlatformHandle, data);
    viewport->RendererUserData = data;
}

static void Hook_Renderer_DestroyWindow(ImGuiViewport* viewport)
{
    if (viewport->RendererUserData != NULL)
    {
        WGL_WindowData* data = (WGL_WindowData*)viewport->RendererUserData;
        CleanupDeviceWGL((HWND)viewport->PlatformHandle, data);
        IM_DELETE(data);
        viewport->RendererUserData = NULL;
    }
}

static void Hook_Platform_RenderWindow(ImGuiViewport* viewport, void*)
{
    // Activate the platform window DC in the OpenGL rendering context
    if (WGL_WindowData* data = (WGL_WindowData*)viewport->RendererUserData)
        wglMakeCurrent(data->hDC, g_hRC);
}

static void Hook_Renderer_SwapBuffers(ImGuiViewport* viewport, void*)
{
    if (WGL_WindowData* data = (WGL_WindowData*)viewport->RendererUserData)
        ::SwapBuffers(data->hDC);
}