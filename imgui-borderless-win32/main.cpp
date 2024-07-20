#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_win32.h"

#include <windows.h>
#include <dwmapi.h>
#include <GL/gl.h>

// Courtesy of https://gist.github.com/sylveon/9c199bb6684fe7dffcba1e3d383fb609
#include "swcadef.h"
static const auto SetWindowCompositionAttribute = 
reinterpret_cast<PFN_SET_WINDOW_COMPOSITION_ATTRIBUTE>(GetProcAddress(GetModuleHandle(L"user32.dll"), "SetWindowCompositionAttribute"));

#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "glu32.lib")
#pragma comment (lib, "dwmapi.lib")

#include <string>
#include <vector>
#include <iostream>
#include "BorderlessWindow.hpp"

// Data stored per platform window
struct WGL_WindowData { HDC hDC; };

// Data
static HGLRC            g_hRC;
static WGL_WindowData   g_MainWindow;
static int              g_Width;  // Unused
static int              g_Height; // Unused

// Forward declarations of helper functions
bool CreateDeviceWGL(HWND hWnd, WGL_WindowData* data);
void CleanupDeviceWGL(HWND hWnd, WGL_WindowData* data);
//void ResetDeviceWGL();

// Borderless Window implements WndProc
// LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// SmartProperty notifies on value change
template <typename T>
struct SmartProperty
{
public:
    T m_Value; // The value to be changed/checked

    SmartProperty(T value)
        : m_Value(value),
        m_LastValue(value),
        m_Changed(FALSE) { }

    BOOL update()
    {
        if (m_Value == m_LastValue) m_Changed = FALSE;
        else m_Changed = TRUE;
        m_LastValue = m_Value;
        return m_Changed;
    }

    BOOL has_changed() const
    {
        return m_Changed;
    }

private:
    T m_LastValue;
    BOOL m_Changed;
};

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

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    BorderlessWindow window; // Instantiate our borderless window

    if (!CreateDeviceWGL(window.m_hWND, &g_MainWindow))
    {
        CleanupDeviceWGL(window.m_hWND, &g_MainWindow);
        ::DestroyWindow(window.m_hWND);
        ::UnregisterClassW((LPCWSTR)window.m_wstrWC, GetModuleHandle(NULL));
        return 1;
    }

    wglMakeCurrent(g_MainWindow.hDC, g_hRC);

    ::ShowWindow(window.m_hWND, SW_SHOWDEFAULT);
    ::UpdateWindow(window.m_hWND);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;   // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;    // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;     // Enable Multi-Viewport / Platform Windows

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_InitForOpenGL(window.m_hWND);
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

    //ImGui::GetIO().ConfigViewportsNoDecoration = false;

    // Main loop
    bool done = false;
    MSG msg;
    ImVec4 clear_color = ImVec4(.0f, .0f, .0f, .0f);

    while (!done)
    {
        static bool swap_styles = false; // Make sure we swap styles outside the WndProc
        static bool borderless  = true;  // Borderless Window's default constructs to borderless

        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        // If we want to swap styles
        if (swap_styles)
        {
            borderless = !borderless;
            window.set_borderless(borderless);
            swap_styles = false;
        }

        static auto render = [&]() {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            {
                // Dockspace
                {
                    ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
                }

                // ImGui Demo
                {
                    ImGui::ShowDemoWindow();
                }

                ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
                {
                    if (ImGui::Begin("Borderless Settings", 0, window_flags))
                    {
                        static SmartProperty<INT> window_mode{ static_cast<int>(borderless) };
                        ImGui::RadioButton("Windowed", &window_mode.m_Value, 0);
                        ImGui::RadioButton("Borderless", &window_mode.m_Value, 1);
                        if (window_mode.update()) swap_styles = true;
                    }
                    ImGui::End();
                }
                
                // Borderless Demo
                {
                    // DWM api is undocumented, and has varying behavior between Windows Releases
                    // Therefore Accent Flags and Animation Flags are essentially trial and error :)
                    // On my system, Accent Flags set to 1 allows for the desired effects in regards
                    // to Accent State.
                    // 
                    // These are just default values that achieve the desired effect on my system
                    // Windows 10 Build 19044

                    ImGui::Begin("DWM Accent State", 0, window_flags);
                    static SmartProperty<INT> accent_policy { ACCENT_ENABLE_BLURBEHIND };
                    ImGui::SeparatorText("DWM Accent State");
                    ImGui::RadioButton("DISABLED", &accent_policy.m_Value, ACCENT_DISABLED);
                    ImGui::RadioButton("GRADIENT", &accent_policy.m_Value, ACCENT_ENABLE_GRADIENT);
                    ImGui::RadioButton("TRANSPARENT GRADIENT", &accent_policy.m_Value, ACCENT_ENABLE_TRANSPARENTGRADIENT);
                    ImGui::RadioButton("BLUR BEHIND", &accent_policy.m_Value, ACCENT_ENABLE_BLURBEHIND);
                    ImGui::RadioButton("ACRYLIC BLUR BEHIND", &accent_policy.m_Value, ACCENT_ENABLE_ACRYLICBLURBEHIND);
                    ImGui::RadioButton("HOST BACKDROP", &accent_policy.m_Value, ACCENT_ENABLE_HOSTBACKDROP);
                    ImGui::RadioButton("INVALID STATE", &accent_policy.m_Value, ACCENT_INVALID_STATE);
                    ImGui::End();

                    ImGui::Begin("DWM Accent Flags", 0, window_flags);
                    ImGui::SeparatorText("DWM Accent Flags");
                    static SmartProperty<INT> accent_flags{ 1 };
                    ImGui::InputInt("Accent Flags", &accent_flags.m_Value, 0, 255);
                    ImGui::End();

                    ImGui::Begin("DWM Animation id", 0, window_flags);
                    ImGui::SeparatorText("DWM Animation id");
                    static SmartProperty<INT> animation_id{ 0 };
                    ImGui::SliderInt("Accent Flags", &animation_id.m_Value, 0, 32);
                    ImGui::End();

                    ImGui::Begin("DWM Gradient", 0, window_flags);
                    ImGui::SeparatorText("DWM Gradient");
                    static ImVec4 color = ImVec4(114.0f / 255.0f, 144.0f / 255.0f, 154.0f / 255.0f, 200.0f / 255.0f);
                    static SmartProperty<INT> gradient_col = { (((int)(color.w * 255)) << 24) | (((int)(color.z * 255)) << 16) | (((int)(color.y * 255)) << 8) | ((int)(color.x * 255)) };
                    ImGui::ColorPicker4("##picker", (float*)&color, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
                    gradient_col.m_Value = (((int)(color.w * 255)) << 24) | (((int)(color.z * 255)) << 16) | (((int)(color.y * 255)) << 8) | ((int)(color.x * 255));
                    ImGui::End();

                    ImGui::Begin("glClearColor", 0, window_flags);
                    ImGui::SeparatorText("glClearColor");
                    ImGui::ColorPicker4("##picker", (float*)&clear_color, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
                    ImGui::End();

                    ImGui::Begin("ImGui Window Bg", 0, window_flags);
                    ImGui::SeparatorText("ImGuiCol_WindowBgAlpha");
                    static SmartProperty<float> bg_alpha{ 1 };
                    ImGui::SliderFloat("BgAlpha", &bg_alpha.m_Value, 1, 0);
                    if (bg_alpha.update()) {
                        ImVec4 window_bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
                        window_bg.w = bg_alpha.m_Value;
                        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = window_bg;
                    }
                    ImGui::End();


                    accent_policy.update();
                    accent_flags.update();
                    gradient_col.update();
                    animation_id.update();


                    static bool init_accents = false; //to apply default initialization
                    if (accent_policy.has_changed() || accent_flags.has_changed()
                        || gradient_col.has_changed() || animation_id.has_changed()
                        || init_accents)
                    {
                        if (init_accents) init_accents = false;

                        ACCENT_POLICY policy = {
                        ACCENT_STATE(accent_policy.m_Value),
                        accent_flags.m_Value,
                        gradient_col.m_Value,
                        animation_id.m_Value
                        };

                        const WINDOWCOMPOSITIONATTRIBDATA data = {
                            WCA_ACCENT_POLICY,
                            &policy,
                            sizeof(policy)
                        };

                        SetWindowCompositionAttribute(window.m_hWND, &data);
                    }
                }

                // Demo Overlay
                {
                    static float f = 0.0f;
                    static int counter = 0;
                    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

                    if (ImGui::Begin("Example: Simple overlay", 0, window_flags))
                    {
                        if (ImGui::IsMousePosValid())
                            ImGui::Text("Mouse Position: (%.1f,%.1f)", io.MousePos.x, io.MousePos.y);
                        else
                            ImGui::Text("Mouse Position: <invalid>");
                        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                    }
                    ImGui::End();
                }
            }

            // Rendering
            ImGui::Render();
            //glClearColor(0, 0, 0, 0);
            glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            // Update imgui window rects for hit testing
            {
                // Get ScreenPos offset
                ImGuiViewport* vp = ImGui::GetMainViewport();
                HWND handle = (HWND)vp->PlatformHandle;
                RECT r;
                GetWindowRect(handle, &r);

                // Only apply offset if Multi-viewports are not enabled
                ImVec2 origin = { (float)r.left, (float)r.top };
                if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
                {
                    origin = { 0, 0 };
                }

                // Add imgui windows that aren't default rects/dockspaces/etc to client area whitelist, but explicitly include imgui demo
                std::vector<RECT> WindowRects;
                for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
                {
                    if ((!(std::string(window->Name).find("Default") != std::string::npos) &&
                        (!(std::string(window->Name).find("Dock") != std::string::npos)) &&
                        (!(std::string(window->Name).find("Menu") != std::string::npos))) ||
                        (std::string(window->Name).find("Dear ImGui Demo") != std::string::npos))
                    {
                        ImVec2 pos = window->Pos;
                        ImVec2 size = window->Size;
                        RECT rect = { origin.x + pos.x, origin.y + pos.y, origin.x + (pos.x + size.x), origin.y + (pos.y + size.y) };
                        WindowRects.push_back(rect);
                    }
                }
                window.set_client_area(WindowRects);
            }

            // Update and Render additional Platform Windows
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();

                // Restore the OpenGL rendering context to the main window DC, since platform windows might have changed it.
                wglMakeCurrent(g_MainWindow.hDC, g_hRC);
            }

            // Present
            ::SwapBuffers(g_MainWindow.hDC);
        };

        static bool set_render = false;
        if (!set_render) {
            window.set_render_callback(render); //render when we are stuck in the message loop doing wm_sizing and wm_move
            set_render = true;
        }
        
        render(); // and render when we leave message loop
    }

    return 0;

} // main

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