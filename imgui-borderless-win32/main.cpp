#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _CRT_SECURE_NO_WARNINGS
#define _ALLOW_KEYWORD_MACROS
#define inline __inline

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_win32.h"

#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include <dwmapi.h>

#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "glu32.lib")
#pragma comment (lib, "dwmapi.lib")
#pragma comment( lib, "msimg32.lib" )

#include "swcadef.h"
static const auto SetWindowCompositionAttribute = 
reinterpret_cast<PFN_SET_WINDOW_COMPOSITION_ATTRIBUTE>(GetProcAddress(GetModuleHandle(L"user32.dll"), "SetWindowCompositionAttribute"));
#define STB_IMAGE_IMPLEMENTATION
#define GL_CLAMP_TO_EDGE 0x812F
#include "stb_image.h"

#include <atomic>
#include <iostream>
#include <utility>
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <cassert>
#include <stdexcept>
#include <assert.h>
#include <tchar.h>

#define USE_IMGUI
#include "BorderlessWindow.hpp"
#include "video_reader.hpp"
#define posix_memalign(p, a, s) (((*(p)) = _aligned_malloc((s), (a))), *(p) ?0 :errno)
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "runtimeobject.lib")
#pragma comment( lib, "msimg32.lib" )
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment (lib, "libavformat.a")
#pragma comment (lib, "libavcodec.a")
#pragma comment (lib, "libavutil.a")
#pragma comment( lib, "libswscale.a" )
#pragma comment( lib, "libswresample.a" )

// Data stored per platform window
struct WGL_WindowData { HDC hDC; };

// Data
static HGLRC            g_hRC;
static WGL_WindowData   g_MainWindow;
static int              g_Width;
static int              g_Height;
static bool             g_Focused;

// Forward declarations of helper functions
bool CreateDeviceWGL(HWND hWnd, WGL_WindowData* data);
void CleanupDeviceWGL(HWND hWnd, WGL_WindowData* data);
//void ResetDeviceWGL();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// SmartProperty notifies on value change
template <typename T>
struct SmartProperty
{
public:
    T m_Value; // The value to be changed/checked

    SmartProperty(T value)
        : m_Value(value),
        m_LastValue(value),
        m_Changed(FALSE)
    {

    }

    BOOL update()
    {
        if (!m_EpsilonEnabled)
        {
            if (m_Value == m_LastValue) m_Changed = FALSE;
            else m_Changed = TRUE;
            m_LastValue = m_Value;
            return m_Changed;
        }
        else
        {
            if ((m_Value <= m_LastValue + m_Epsilon) && (m_Value >= m_LastValue - m_Epsilon)) m_Changed = FALSE;
            else m_Changed = TRUE;
            m_LastValue = m_Value;
            return m_Changed;
        }
    }

    BOOL has_changed() const
    {
        return m_Changed;
    }

    VOID enable_epsilon(T epsilon)
    {
        m_EpsilonEnabled = TRUE;
        m_Epsilon = epsilon;
    }

private:
    T m_LastValue;
    T m_Epsilon;
    BOOL m_EpsilonEnabled{};
    BOOL m_Changed;

};

int SliderIntPow2(const char* label, int* v, int v_min, int v_max)
{
    int pow2min = static_cast<int>(std::log2(v_min));
    int pow2max = static_cast<int>(std::log2(v_max));

    int pow2v = static_cast<int>(std::log2(*v));
    if (pow2v < pow2min) pow2v = pow2min;
    if (pow2v > pow2max) pow2v = pow2max;

    if (ImGui::SliderInt(label, &pow2v, pow2min, pow2max)) {
        *v = static_cast<int>(std::pow(2, pow2v));
        return true;
    }
    return false;
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

bool LoadTextureFromFile(const char* filename, GLuint* out_texture, int* out_width, int* out_height)
{
    // Load from file
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

    // Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture = image_texture;
    *out_width = image_width;
    *out_height = image_height;

    return true;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{

#ifdef USE_IMGUI
    //For Debug
    AllocConsole();

    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    BorderlessWindow window;
    
    if (!CreateDeviceWGL(window.m_hHWND.get(), &g_MainWindow))
    {
        CleanupDeviceWGL(window.m_hHWND.get(), &g_MainWindow);
        ::DestroyWindow(window.m_hHWND.get());
        ::UnregisterClassW((LPCWSTR)window.m_wstrWC, GetModuleHandle(NULL));
        return 1;
    }

    wglMakeCurrent(g_MainWindow.hDC, g_hRC);

    ::ShowWindow(window.m_hHWND.get(), SW_SHOWDEFAULT);
    ::UpdateWindow(window.m_hHWND.get());

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
    ImGui_ImplWin32_InitForOpenGL(window.m_hHWND.get());
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

    VideoReaderState vr_state;
    vr_state.sws_scaler_ctx = NULL;
    if (!video_reader_open(&vr_state, "..\\res\\WarthogVignette.mp4")) {
        printf("Couldn't open video file (make sure you set a video file that exists)\n");
        return 1;
    }
    
    GLuint tex_handle;
    glGenTextures(1, &tex_handle);
    glBindTexture(GL_TEXTURE_2D, tex_handle);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    constexpr int ALIGNMENT = 128;
    const int frame_width = vr_state.width;
    const int frame_height = vr_state.height;
    uint8_t* frame_data;

    if (posix_memalign((void**)&frame_data, ALIGNMENT, frame_width * frame_height * 4) != 0) {
        printf("Couldn't allocate frame buffer\n");
        return 1;
    }

    // Main loop
    bool done = false;
    MSG msg;
    ImVec4 clear_color = ImVec4(.0f, .0f, .0f, 0.f);
    while (!done)
    {

        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        GLuint image_texture;
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
                static bool show = true;
                if (ImGui::Begin("Borderless Settings", 0, window_flags))
                {
                    static SmartProperty<INT> window_mode{ 1 };
                    ImGui::RadioButton("Windowed", &window_mode.m_Value, 0);
                    ImGui::RadioButton("Borderless", &window_mode.m_Value, 1);
                    if (window_mode.update()) window.set_borderless(window_mode.m_Value);
                }
                ImGui::End();
            }

            // Borderless Demo
            {
                ImGui::Begin("DWM Accent State", 0, window_flags);
                static SmartProperty<INT> accent_policy { ACCENT_DISABLED };
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
                //ImGui::SliderInt("Accent Flags", &accent_flags.m_Value, 0 , 32);
                SliderIntPow2("Accent Flags", &accent_flags.m_Value, 1, 256);
                ImGui::End();

                ImGui::Begin("DWM Gradient", 0, window_flags);
                ImGui::SeparatorText("DWM Gradient");
                static ImVec4 color = ImVec4(114.0f / 255.0f, 144.0f / 255.0f, 154.0f / 255.0f, 200.0f / 255.0f);
                static SmartProperty<INT> gradient_col = { (((int)(color.w * 255)) << 24) | (((int)(color.z * 255)) << 16) | (((int)(color.y * 255)) << 8) | ((int)(color.x * 255)) };
                ImGui::ColorPicker4("##picker", (float*)&color, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
                gradient_col.m_Value = (((int)(color.w * 255)) << 24) | (((int)(color.z * 255)) << 16) | (((int)(color.y * 255)) << 8) | ((int)(color.x * 255));
                ImGui::End();

                ImGui::Begin("DWM Animation id", 0, window_flags);
                ImGui::SeparatorText("DWM Animation id");
                static SmartProperty<INT> animation_id{ 0 };
                ImGui::SliderInt("Accent Flags", &animation_id.m_Value, 0 , 32);
                ImGui::End();

                accent_policy.update();
                accent_flags.update();
                gradient_col.update();
                animation_id.update();

                if (accent_policy.has_changed() || accent_flags.has_changed() 
                    || gradient_col.has_changed() || animation_id.has_changed())
                {
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

                    SetWindowCompositionAttribute(window.m_hHWND.get(), &data);
                }
            }

            // Demo Overlay
            {
                static float f = 0.0f;
                static int counter = 0;
                ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
                static bool show = true;
                if (ImGui::Begin("Example: Simple overlay", &show, window_flags))
                {
                    if (ImGui::IsMousePosValid())
                        ImGui::Text("Mouse Position: (%.1f,%.1f)", io.MousePos.x, io.MousePos.y);
                    else
                        ImGui::Text("Mouse Position: <invalid>");
                    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                }
                ImGui::End();
            }

            static float last_frame_time = 0.0f;
            last_frame_time += ImGui::GetIO().DeltaTime;
            window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            ImGui::Begin("Enable .bk2", 0, window_flags);
            static bool show = true;
            ImGui::Checkbox("Enable .bk2", &show);
            ImGui::End();

            static float time = 0.0f;
            static SmartProperty<double> endFlag {-100};
            endFlag.enable_epsilon(0.0001);
            if(show)
            {
                RECT wRect{};
                GetWindowRect(window.m_hHWND.get(), &wRect);
                ImVec2 start{ (float)wRect.left, (float)wRect.top };
                ImVec2 end{ (float)wRect.right, (float)wRect.bottom };
                if (time > 0.0333f)
                {
                    int64_t pts;
                    if (!video_reader_read_frame(&vr_state, frame_data, &pts)) {
                        printf("Couldn't load video frame\n");
                        return 1;
                    }
                    printf("%lf\n", pts * (double)vr_state.time_base.num / (double)vr_state.time_base.den);
                    endFlag.m_Value = pts * (double)vr_state.time_base.num / (double)vr_state.time_base.den;
                    if (!endFlag.update()) video_reader_seek_frame(&vr_state, 0.00f);

                    glBindTexture(GL_TEXTURE_2D, tex_handle);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame_data);
                    time = 0.00f;
                }
                ImGui::GetBackgroundDrawList()->AddImage((void*)(intptr_t)tex_handle, start, end);
            }
            time += ImGui::GetIO().DeltaTime;
        }

        // Rendering
        ImGui::Render();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glDeleteTextures(1, &image_texture);
        // Update imgui window rects for hit testing
        {
            // Get ScreenPos offset
            ImGuiViewport* vp = ImGui::GetMainViewport();
            HWND handle = (HWND)vp->PlatformHandle;
            RECT r;
            GetWindowRect(handle, &r);

            // Only apply offset if Multi-viewports are enabled
            ImVec2 origin = { (float)r.left, (float)r.top };
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                origin = { 0, 0 };
            }

            // Add imgui windows that aren't default rects/dockspaces/etc to client area whitelist
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
    }

    return 0;

#else
    try {
        BorderlessWindow window;
        std::vector<RECT> rects = { {0, 0, 100, 100}, {1820, 0, 1920, 100} };
        //window.set_client_area(rects);

        MSG msg;
        while (::GetMessageW(&msg, nullptr, 0, 0) == TRUE) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    catch (const std::exception& e) {
        ::MessageBoxA(nullptr, e.what(), "Unhandled Exception", MB_OK | MB_ICONERROR);
}
#endif
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