extern "C"{
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <vsstyle.h>
#include <vssym32.h>
#include <Uxtheme.h>
#include <GL/gl.h>
#include <versionhelpers.h>
#include "win32_window.h"
#include "win32_helpers.h"
#include "win32_wgl.h"
#include "stephs_types.h"
#include "swcadef.h"      // Courtesy of https://gist.github.com/sylveon/9c199bb6684fe7dffcba1e3d383fb609
#include <string.h>
#include <GL\glext.h>
//#include <GL\wglext.h>
}
#pragma comment (lib, "opengl32")
#pragma comment (lib, "glu32")
#pragma comment (lib, "dwmapi")
#pragma comment (lib, "uxtheme")

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"
#include "ImLogCounter.hpp"

#include <functional>
#include <vector>
#include <string>

enum class WindowMode {
    Windowed = 0,
    Borderless = 1,
    CustomCaption = 2
};

enum class TitleBarButton {
    None = 0,
    Close = 1,
    Maximize = 2,
    Minimize = 3
};
LRESULT WINAPI WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ImGuiBorderlessWin32 {
static constexpr DWORD windowed   = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
static constexpr DWORD borderless = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_VISIBLE;
static constexpr DWORD custom     = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
static void PushDemoStyle(void);
static void ShowDemoWindow(HWND hWnd, ImVec4& clearColor);
} // ImGuiBorderlessWin32
INT compute_standard_caption_height_for_window(HWND window_handle);

static std::function<void(HWND hWnd)> g_ClientRenderFunction;
static std::vector<RECT>              g_ClientCustomClientArea;
static WindowMode                     g_ClientWindowMode      = WindowMode::Windowed;
static TitleBarButton                 g_TitlebarHoveredButton = TitleBarButton::None;
static ImLogCounter                   g_WindowMessageLogCounter{};
static BOOL                           g_TitlebarButtonHovered = FALSE;
static BOOL                           g_ClientBackdropEnabled = FALSE;
static HANDLE                         g_IRTimer = INVALID_HANDLE_VALUE;
static bool SetVSync(bool enabled);

// Data stored per platform window
struct WGL_WindowData { HDC hDC; };

// Data
static HGLRC            g_hRC;
static WGL_WindowData   g_MainWindow;
static int              g_Width;
static int              g_Height;
static INT              g_pfMSAA;
// Forward declarations of helper functions
static bool CreateDeviceWGL(HWND hWnd, WGL_WindowData* data);
static void CleanupDeviceWGL(HWND hWnd, WGL_WindowData* data);
//void ResetDeviceWGL();

static void Hook_Renderer_CreateWindow(ImGuiViewport* viewport);
static void Hook_Renderer_DestroyWindow(ImGuiViewport* viewport);
static void Hook_Platform_RenderWindow(ImGuiViewport* viewport, void*);
static void Hook_Renderer_SwapBuffers(ImGuiViewport* viewport, void*);

int APIENTRY wWinMain(
    _In_ HINSTANCE     hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR        lpCmdLine,
    _In_ int           nCmdShow)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);
    (VOID)win32_set_timer_resolution(5000, TRUE, NULL);
    g_IRTimer = win32_create_high_resolution_timer(NULL, NULL, TIMER_ALL_ACCESS);
    ImGui_ImplWin32_EnableDpiAwareness();
    DwmEnableMMCSS(TRUE);
    g_pfMSAA = win32_wgl_get_pixel_format(4);
    g_WindowMessageLogCounter.SetMsgMap(WMMsgMap);
    EnableTheming(TRUE);
    win32_window_t w32Window = {0};
    win32_window_create(&w32Window, 1080, 720, CS_VREDRAW | CS_HREDRAW | CS_OWNDC, 0, ImGuiBorderlessWin32::windowed);
    w32Window.msgHook = (win32_wndproc_hook_t)WndProcHook;

    if (!CreateDeviceWGL(w32Window.hWnd, &g_MainWindow))
    {
        CleanupDeviceWGL(w32Window.hWnd, &g_MainWindow);
        ::DestroyWindow(w32Window.hWnd);
        ::UnregisterClass(w32Window.tcClassName, GetModuleHandle(NULL));
        return 1;
    }

    wglMakeCurrent(g_MainWindow.hDC, g_hRC);

    ::ShowWindow(w32Window.hWnd, SW_SHOWDEFAULT);
    ::UpdateWindow(w32Window.hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;     // Enable Multi-Viewport / Platform Windows

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_InitForOpenGL((void*)w32Window.hWnd);
    ImGui_ImplOpenGL3_Init();

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
    io.Fonts->AddFontFromFileTTF("MyriadPro-Light.ttf", 16.0f);
    for(;;)
    {
        MSG msg; //- Pump message loop; break on WM_QUIT
        if (!win32_window_pump_message_loop(&w32Window, &msg, FALSE)) break;

        //- Set the client render function callback if not done so already (So we can also render in sizemoves)
        if (!g_ClientRenderFunction)
        {
            g_ClientRenderFunction = [io](HWND hWnd) {
                static ImVec4 clear_color(.0f, .0f, .0f, .0f);

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
                glEnable(GL_MULTISAMPLE);
                glViewport(0, 0, g_Width, g_Height);
                glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
                glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

                // Update and Render additional Platform Windows
                if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
                {
                    ImGuiContext& g = *GImGui;
                    for (int i = 1; i < g.Viewports.Size; i++)
                    {
                        ImGuiViewportP* viewport = g.Viewports[i];
                        ImGui_ImplWin32_EnableAlphaCompositing((HWND)viewport->PlatformHandleRaw);
                    }
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
                // Hack to reset DWM animation timer
                if (g_ClientBackdropEnabled) ImGui_ImplWin32_EnableAlphaCompositing(hWnd);

                int64_t COEFF{ -100'0 };
                int64_t TIMEOUT{ 12'0 };
                LARGE_INTEGER dueTime;
                //desired_fr = 60;
                if (GetActiveWindow() == NULL) {
                    //desired_fr = 10;
                    TIMEOUT = 97'0;
                }
                dueTime.QuadPart = TIMEOUT * COEFF;
                //win32_hectonano_sleep(16000);
                //win32_yield_on_high_resolution_timer(g_IRTimer, &dueTime);
            };
        }
        
        //- Then just make that client render call like usual
        g_ClientRenderFunction(w32Window.hWnd);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceWGL(w32Window.hWnd, &g_MainWindow);
    wglDeleteContext(g_hRC);
    ::DestroyWindow(w32Window.hWnd);
    ::UnregisterClass(w32Window.tcClassName, GetModuleHandle(NULL));

    return 0;
} // main

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static constexpr UINT timer_id = 0;

    //g_WindowMessageLogCounter.AddMsg(msg);

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;

    switch (msg) {
    case WM_CREATE:
    {
        
        break;
    }
    case WM_NCCALCSIZE: {
        if (wParam) {
            if (g_ClientWindowMode == WindowMode::Borderless)
            {
                WINDOWPLACEMENT placement = {0};
                if (::GetWindowPlacement(hWnd, &placement)) 
                {
                    if (placement.showCmd == SW_MAXIMIZE)
                    {
                        NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lParam;

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
                }
                return 0;
            }
            else if (g_ClientWindowMode == WindowMode::CustomCaption)
            {
                auto parameters = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);

                auto& requested_client_area = parameters->rgrc[0];
                requested_client_area.right  -= GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                requested_client_area.left   += GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                requested_client_area.bottom -= GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                return 0;
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

        if (g_ClientWindowMode == WindowMode::CustomCaption)
        {
            UINT dpi = GetDpiForWindow(hWnd);
            const LONG b_width  = (LONG)::GetSystemMetricsForDpi(SM_CXSIZE, dpi);
            const LONG b_height = (LONG)compute_standard_caption_height_for_window(hWnd);
            
            const RECT close_{
                window.right - b_width,
                window.top,
                window.right,
                window.top + b_height
            };

            const RECT max_{
                window.right - (b_width * 2),
                window.top,
                window.right,
                window.top + b_height
            };

            const RECT min_{
                window.right - (b_width * 3),
                window.top,
                window.right,
                window.top + b_height
            };

            if (PtInRect(&close_, cursor))
            {
                g_TitlebarHoveredButton = TitleBarButton::Close;
                g_TitlebarButtonHovered = TRUE;
                return HTCLOSE;
            }

            else if (PtInRect(&max_, cursor)) {
                g_TitlebarHoveredButton = TitleBarButton::Maximize;
                g_TitlebarButtonHovered = TRUE;
                return HTMAXBUTTON;
            }

            else if (PtInRect(&min_, cursor)) {
                g_TitlebarHoveredButton = TitleBarButton::Minimize;
                g_TitlebarButtonHovered = TRUE;
                return HTMINBUTTON;
            }
        }
        g_TitlebarHoveredButton = TitleBarButton::None;
        g_TitlebarButtonHovered = FALSE;

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
            window.top += (LONG)(30 * 1.5);
            if (PtInRect(&window, cursor))
            {
                return HTCLIENT;
            }
            return HTCAPTION;
            //return DefWindowProc(hWnd, msg, wParam, lParam);
            //for (RECT rect : g_ClientCustomClientArea)
            //    if (PtInRect(&rect, cursor)) return HTCLIENT;
            //return HTCAPTION;
        }
        default: return HTNOWHERE;
        }
        break;
    }
    case WM_ENTERSIZEMOVE: {
        SetTimer(hWnd, (UINT_PTR)&timer_id, USER_TIMER_MINIMUM, NULL); // Start the render timer wince we'll be stuck modally in the message loop
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
    case WM_SIZING:
    {
        if (g_ClientRenderFunction)
            g_ClientRenderFunction(hWnd);
        break;
    }
    case WM_EXITSIZEMOVE: {
        KillTimer(hWnd, (UINT_PTR)&timer_id); // Kill the timer since we're no longer stuck
        break;
    }
    case WM_TIMER: {
        if (g_ClientRenderFunction) {

            g_ClientRenderFunction(hWnd);
            g_ClientRenderFunction(hWnd);
        }
        return 0;
    }
    case WM_NCLBUTTONDOWN:
        if(g_TitlebarButtonHovered && (g_ClientWindowMode == WindowMode::CustomCaption))
            return 0;
        break;
        // Map button clicks to the right messages for the window
    case WM_NCLBUTTONUP: {
        if (g_TitlebarHoveredButton == TitleBarButton::Close) {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        else if (g_TitlebarHoveredButton == TitleBarButton::Minimize) {
            ShowWindow(hWnd, SW_MINIMIZE);
            return 0;
        }
        else if (g_TitlebarHoveredButton == TitleBarButton::Maximize) {
            int mode = IsMaximized(hWnd) ? SW_NORMAL : SW_MAXIMIZE;
            ShowWindow(hWnd, mode);
            return 0;
        }
        break;
    }
    case WM_NCACTIVATE: {
        if (g_ClientWindowMode == WindowMode::CustomCaption) {
            return 1;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        (void)BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        //DwmFlush();
        return 0;
    }
    case WM_ERASEBKGND: // Prevent flicker when we're rendering during resize
        return 1;
    case WM_SYSCOMMAND: {
        //return 0;
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
namespace ImGui {
static bool CheckboxFlagsTransform(const char* label, int* flags, int bitflag, std::function<void(int*)> transform)
{
    bool   toggled = ImGui::CheckboxFlags(label, flags, bitflag);
    if    (toggled) transform(flags);
    return toggled;
}
template<class...Args>
static void WText(const wchar_t* fmt, Args&&... args)
{
    std::u8string buf(wcsnlen_s(fmt, std::numeric_limits<i16>::max() + 1));
    (void)WideCharToMultiByte(CP_UTF8, 0, static_cast<LPCWCH>(fmt), buf.size(),static_cast<LPSTR>(buf.data()), buf.size(), 0, NULL);
    ImGui::Text(buf.c_str(), std::forward<Args>(args)...);
}
}
namespace ImGuiBorderlessWin32 {
static constexpr ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoDecoration       |
                                                  ImGuiWindowFlags_NoDocking          |
                                                  ImGuiWindowFlags_AlwaysAutoResize   |
                                                  ImGuiWindowFlags_NoFocusOnAppearing |
                                                  ImGuiWindowFlags_NoNav;
void PushDemoStyle(void)
{
    float a = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).w;
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.118f, 0.118f, 0.118f, a);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.443f, 0.376f, 0.910f, 1.000f));


}
static void PopDemoStyle(void)
{
    ImGui::PopStyleColor(1);
}
void ShowDemoWindow(HWND hWnd, ImVec4& clearColor)
{
    static bool swap_styles = false;
    PushDemoStyle();
    if (ImGui::Begin("imgui-borderless-win32"))
    {
        if (ImGui::Begin("Borderless Settings", 0, overlay_flags))
        {
            ImGuiIO&    io    = ImGui::GetIO();
            ImGuiStyle& style = ImGui::GetStyle();

            ImGui::BeginGroup();
            int last_mode = static_cast<int>(g_ClientWindowMode);
            bool       changes     = false;
            static int window_mode = static_cast<int>(g_ClientWindowMode);
            changes |= ImGui::RadioButton("Windowed",   &window_mode, static_cast<int>(WindowMode::Windowed));
            changes |= ImGui::RadioButton("Borderless", &window_mode, static_cast<int>(WindowMode::Borderless));
            changes |= ImGui::RadioButton("Custom Caption", &window_mode, static_cast<int>(WindowMode::CustomCaption));
            if (changes && (last_mode != window_mode)) {
                g_ClientWindowMode   = static_cast<WindowMode>(window_mode);
                RECT r;
                GetWindowRect(hWnd, &r);
                INT caption          = GetSystemMetrics(SM_CYCAPTION);
                DWORD next_style     = (g_ClientWindowMode == WindowMode::Borderless) ? borderless : (g_ClientWindowMode == WindowMode::Windowed) ? windowed : custom;
                caption = (g_ClientWindowMode == WindowMode::Borderless) ? -caption : caption;
                //if (g_ClientWindowMode == WindowMode::CustomCaption) caption += GetSystemMetrics(SM_CXPADDEDBORDER);
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
            ImGui::BeginGroup();
            if (ImGui::Checkbox("Enable Alpha Compositing", &enable_alpha_compositing))
            {
                if (enable_alpha_compositing) ImGui_ImplWin32_EnableAlphaCompositing((void*)hWnd);
                else 
                {
                    DWM_BLURBEHIND bb = { 0 };
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
            ImGui::EndGroup();
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::SeparatorText("Win11");
            //ImGui::BeginDisabled(!IsWindowsVersionOrGreater(10, 0, 22000));
            static int corner_preference = 0;
            bool changes = false;
            changes |= ImGui::RadioButton("Default", &corner_preference, DWMWCP_DEFAULT);
            changes |= ImGui::RadioButton("Do Not Round", &corner_preference, DWMWCP_DONOTROUND);
            changes |= ImGui::RadioButton("Round", &corner_preference, DWMWCP_ROUND);
            changes |= ImGui::RadioButton("Round Small", &corner_preference, DWMWCP_ROUNDSMALL);
            if (changes) {
                (void)DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_preference, sizeof(corner_preference));
            }
            //ImGui::EndDisabled();
            ImGui::EndGroup();
            static int wtnca = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU;
            ImGui::SeparatorText("Window Theme Nonclient Area###WTNCA");
            ImGui::CheckboxFlagsTransform("WTNCA_NODRAWCAPTION", &wtnca, (int)WTNCA_NODRAWCAPTION,
                [hWnd](int* wtnca) { SetWindowThemeNonClientAttributes(hWnd, WTNCA_NODRAWCAPTION, (*wtnca & WTNCA_NODRAWCAPTION)); SendMessage(hWnd, WM_THEMECHANGED, 0, 0); });
            ImGui::CheckboxFlagsTransform("WTNCA_NODRAWICON", &wtnca, (int)WTNCA_NODRAWICON,
                [hWnd](int* wtnca) { SetWindowThemeNonClientAttributes(hWnd, WTNCA_NODRAWICON, (*wtnca & WTNCA_NODRAWICON));
            SendMessage(hWnd, WM_THEMECHANGED, 0, 0); });
            ImGui::CheckboxFlagsTransform("WTNCA_NOSYSMENU", &wtnca, (int)WTNCA_NOSYSMENU,
                [hWnd](int* wtnca) { SetWindowThemeNonClientAttributes(hWnd, WTNCA_NOSYSMENU, (*wtnca & WTNCA_NOSYSMENU)); SendMessage(hWnd, WM_THEMECHANGED, 0, 0); });
        }

        if (ImGui::CollapsingHeader("OpenGL")) 
        {
            static bool enable_vsync = false;
            ImGui::SeparatorText("glClearColor");
            (void)ImGui::ColorPicker4("###glClearColorPicker", (float*)&clearColor, ImGuiColorEditFlags_NoSidePreview |
                                                                                    ImGuiColorEditFlags_NoSmallPreview);
            ImGui::SameLine();
            if (ImGui::Checkbox("Enable VSync", &enable_vsync)) (void)SetVSync(enable_vsync);
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
                g_ClientBackdropEnabled = (accent_policy != ACCENT_DISABLED) && 
                                          (accent_policy != ACCENT_ENABLE_HOSTBACKDROP) &&
                                          (accent_policy != ACCENT_INVALID_STATE);
            }
        }
    }
    //g_WindowMessageLogCounter.Draw("yuh");
    ImGui::End();

    ImGui::Begin("Displays");
    {
        UINT i;
        static win32_display_info_t display_info;
        (void)memset(&display_info,0,sizeof(win32_display_info_t));
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImDrawList*  dl     = window->DrawList;
        ImRect       wr     = window->Rect();
        ImVec2       ws     = window->Rect().GetSize();
        ImRect bb(wr.Min + (ws * 0.05f), wr.Max - (ws * 0.05f));
        dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
        
        //if (!display_info.numDisplays)
        (void)win32_get_display_info(&display_info);

        RECT br = display_info.boundingRect;
        ImVec2 scale_factor = ImVec2(bb.GetWidth() / win32_rect_get_width(&br), bb.GetHeight() / win32_rect_get_height(&br));
        float bbw = win32_rect_get_width(&br) * ImMin(scale_factor.x, scale_factor.y);
        float bbh = win32_rect_get_height(&br) * ImMin(scale_factor.x, scale_factor.y);
        bb = ImRect(bb.GetCenter() - (ImVec2(bbw, bbh) * 0.5f), bb.GetCenter() + (ImVec2(bbw, bbh) * 0.5f));
        for (i = 0; i < display_info.numDisplays; ++i)
        {
            char info_buffer[256] = { 0 };
            const win32_monitor_info_t* const mi = &display_info.displays[i];
            const char* const display_name       = mi->deviceName;
            RECT dr         = mi->monitorInfoEx.rcMonitor;
            float br_width  = (float)(br.right  - br.left);
            float br_height = (float)(br.bottom - br.top);
            float tx_min    = (dr.left   - br.left) / br_width;
            float tx_max    = (dr.right  - br.left) / br_width;
            float ty_min    = (dr.top    - br.top)  / br_height;
            float ty_max    = (dr.bottom - br.top)  / br_height;
            ImRect display_bb(bb.Min + (ImVec2(tx_min, ty_min) * bb.GetSize()), bb.Min + (ImVec2(tx_max, ty_max) * bb.GetSize()));
            (void)sprintf_s(info_buffer, 256U, "(%d x %d) @ %dhz", mi->deviceMode.dmPelsWidth, mi->deviceMode.dmPelsHeight, mi->deviceMode.dmDisplayFrequency);
            ImVec2 name_start = display_bb.GetCenter() - (ImGui::CalcTextSize(display_name) * 0.5f);
            ImVec2 info_start = display_bb.GetCenter() - (ImGui::CalcTextSize(info_buffer) * 0.5f);
            dl->AddRect(display_bb.Min, display_bb.Max, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            dl->PushClipRect(display_bb.Min, display_bb.Max, true);
            dl->AddText(name_start, ImGui::GetColorU32(ImGuiCol_Text), display_name);
            dl->AddText(info_start + ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing()), ImGui::GetColorU32(ImGuiCol_Text), info_buffer);
            dl->PopClipRect();
            if (ImGui::IsMouseHoveringRect(display_bb.Min, display_bb.Max))
            {
                ImGui::BeginTooltip();
                ImGui::Text("Friendly Monitor Device Name: %s", mi->monitorFriendlyDeviceName);
                ImGui::Text("Monitor Device Path:          %s", mi->monitorDevicePath);
                ImGui::Text("Adapter Device Path:          %s", mi->adapterDevicePath);
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::End();

    if (g_ClientWindowMode == WindowMode::CustomCaption)
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        const float b_height = (float)compute_standard_caption_height_for_window(hWnd);
        ImRect bb(vp->Pos, vp->Pos + ImVec2(vp->Size.x, b_height));
        UINT dpi = GetDpiForWindow(hWnd);
        const LONG b_width = (LONG)::GetSystemMetricsForDpi(SM_CXSIZE, dpi);
        ImRect close   (ImVec2(bb.Max.x - b_width, vp->Pos.y), ImVec2(bb.Max.x, vp->Pos.y + b_height));
        ImRect maximize(ImVec2(close.Min.x - b_width, vp->Pos.y), ImVec2(close.Min.x, close.Max.y));
        ImRect minimize(ImVec2(maximize.Min.x - b_width, vp->Pos.y), ImVec2(maximize.Min.x, maximize.Max.y));
        ImGui::GetStyle().WindowBorderSize = 0;
        ImGuiWindowFlags flags =  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
        ImGui::BeginViewportSideBar("###titlebar", NULL, ImGuiDir_Up, b_height, flags);
        ImDrawList* root = ImGui::GetForegroundDrawList();
        ImGui::Text("imgui-borderless-win32");

        //root->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.118f, 0.118f, 0.118f, 1.000f)));

        if (close.Contains(ImGui::GetMousePos()))
        {
            ImVec4 col = ImVec4(0.75f, 0.0f, 0.0f, 0.75f);
            root->AddRectFilled(close.Min, close.Max, ImGui::GetColorU32(col));
        }
        if (maximize.Contains(ImGui::GetMousePos()))
        {
            ImVec4 col = ImVec4(0.75f, 0.0f, 0.0f, 0.75f);
            root->AddRectFilled(maximize.Min, maximize.Max, ImGui::GetColorU32(col));
        }
        if (minimize.Contains(ImGui::GetMousePos()))
        {
            ImVec4 col = ImVec4(0.75f, 0.0f, 0.0f, 0.75f);
            root->AddRectFilled(minimize.Min, minimize.Max, ImGui::GetColorU32(col));
        }
        ImRect min_bounds = ImRect(minimize.Min + ImVec2(minimize.GetWidth() * .3f, minimize.GetHeight() * .49f), minimize.Max - (ImVec2(minimize.GetWidth() * .3f, minimize.GetHeight() * .49f)));
        root->AddRectFilled(min_bounds.Min, min_bounds.Max, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));

        ImRect max_bounds = ImRect(maximize.Min + (maximize.GetSize() * 0.33f), maximize.Max - (maximize.GetSize() * 0.33f));
        root->AddRect(max_bounds.Min, max_bounds.Max, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));

        ImRect x_bounds = ImRect(close.Min + (close.GetSize() * 0.33f), close.Max - (close.GetSize() * 0.33f));
        root->AddLine(x_bounds.Min, x_bounds.Max, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), 1.0f);
        root->AddLine(ImVec2(x_bounds.Min.x, x_bounds.Max.y), ImVec2(x_bounds.Max.x, x_bounds.Min.y), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), 1.0f);


        //root->AddRectFilled(maximize.Min, maximize.Max, ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_Button]));
        //root->AddRectFilled(minimize.Min, minimize.Max, ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]));
        ImGui::End();
        ImGui::GetStyle().WindowBorderSize = 1;
    }
    PopDemoStyle();
}
}

static bool SetVSync(bool sync)
{
    if (wglSwapIntervalEXT)
        wglSwapIntervalEXT(sync);
    return true;
}

static bool CreateDeviceWGL(HWND hWnd, WGL_WindowData* data)
{
    HDC hDc = ::GetDC(hWnd);
    PIXELFORMATDESCRIPTOR pfd = {
      sizeof(PIXELFORMATDESCRIPTOR),
      1,                                // Version Number
      PFD_DRAW_TO_WINDOW |              // Format Must Support Window
      PFD_SUPPORT_OPENGL |              // Format Must Support OpenGL
      PFD_SUPPORT_COMPOSITION |         // Format Must Support Composition
      PFD_GENERIC_ACCELERATED |
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
    if (::SetPixelFormat(hDc, g_pfMSAA, &pfd) == FALSE)
        return false;
    ::ReleaseDC(hWnd, hDc);

    data->hDC = ::GetDC(hWnd);
    if (!g_hRC)
        g_hRC = wglCreateContext(data->hDC);
        //g_hRC = wglCreateContextAttribsARB(data->hDC, 0, attribs);
    return true;
}

static void CleanupDeviceWGL(HWND hWnd, WGL_WindowData* data)
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

INT compute_standard_caption_height_for_window(HWND window_handle) {

    SIZE caption_size{};
    auto const accounting_for_borders = 2;
    auto theme = OpenThemeData(window_handle, L"WINDOW");
    auto dpi = GetDpiForWindow(window_handle);
    GetThemePartSize(theme, nullptr, WP_CAPTION, CS_ACTIVE, nullptr, TS_TRUE, &caption_size);
    CloseThemeData(theme);

    auto height = static_cast<float>(caption_size.cy * dpi) / 96.0f;
    return static_cast<int>(height) + accounting_for_borders;
}