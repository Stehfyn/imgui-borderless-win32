#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _CRT_SECURE_NO_WARNINGS

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
#include <atomic>
#include <vector>
#include <iostream>
#include <string>
#include <stdexcept>
#include <assert.h>
#include <tchar.h>

// Data stored per platform window
struct WGL_WindowData { HDC hDC; };

// Data
static HGLRC            g_hRC;
static WGL_WindowData   g_MainWindow;
static int              g_Width;
static int              g_Height;

struct ATOMIC_RECTVA
{
    unsigned int size = 0;
    RECT rects[100];
};

static std::atomic<ATOMIC_RECTVA> g_ImGuiWindows;
static std::atomic<ImVec2> g_Origin;

// Forward declarations of helper functions
bool CreateDeviceWGL(HWND hWnd, WGL_WindowData* data);
void CleanupDeviceWGL(HWND hWnd, WGL_WindowData* data);
//void ResetDeviceWGL();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    unsigned int returncode = 0;

    try
    {
        //For Debug
        AllocConsole();
        freopen("CONIN$", "r", stdin);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);

        //Create Window
        WNDCLASSEXW wcx = { sizeof(wcx), CS_OWNDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"ImGui Example", NULL };
        ::RegisterClassExW(&wcx);
        HWND hwnd = ::CreateWindowExW(0, wcx.lpszClassName, L"Dear ImGui Win32+OpenGL3 Example", WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, 100, 100, 1280, 800, NULL, NULL, wcx.hInstance, NULL);

        static const MARGINS shadow_state[2]{ { 0,0,0,0 },{ 1,1,1,1 } };
        ::DwmExtendFrameIntoClientArea(hwnd, &shadow_state[true]);

        static const auto SetWindowCompositionAttribute =
            reinterpret_cast<PFN_SET_WINDOW_COMPOSITION_ATTRIBUTE>(GetProcAddress(GetModuleHandle(L"user32.dll"), "SetWindowCompositionAttribute"));

        DWM_BLURBEHIND bb = { 0 };
        HRGN hRgn = CreateRectRgn(0, 0, -1, -1);
        bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        bb.hRgnBlur = hRgn;
        bb.fEnable = TRUE;
        DwmEnableBlurBehindWindow(hwnd, &bb);
        if (!CreateDeviceWGL(hwnd, &g_MainWindow))
        {
            CleanupDeviceWGL(hwnd, &g_MainWindow);
            ::DestroyWindow(hwnd);
            ::UnregisterClassW(wcx.lpszClassName, wcx.hInstance);
            return 1;
        }
        wglMakeCurrent(g_MainWindow.hDC, g_hRC);

        ::ShowWindow(hwnd, SW_SHOWDEFAULT);
        ::UpdateWindow(hwnd);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;   // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;    // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable Docking
        //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;     // Enable Multi-Viewport / Platform Windows

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsClassic();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // Setup Platform/Renderer backends
        ImGui_ImplWin32_InitForOpenGL(hwnd);
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

        ImGui::GetIO().ConfigViewportsNoDecoration = false;
        // Our state
        bool show_demo_window = true;
        bool show_another_window = false;
        ImVec4 clear_color = ImVec4(.0f, .0f, .0f, 0.f);

        // Main loop
        bool done = false;
        static bool gradient = TRUE;
        static int last_gradient_col = 0;
        static ImVec4 color = ImVec4(114.0f / 255.0f, 144.0f / 255.0f, 154.0f / 255.0f, 200.0f / 255.0f);

        MSG msg;
        while (!done)
        {
            std::vector<RECT> WindowRects;
            while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT)
                    done = true;
            }
            if (done)
                break;

            // Start the Dear ImGui frame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            //update window rects
            HWND vp = (HWND)ImGui::GetMainViewport()->PlatformHandle;
            RECT r;
            GetWindowRect(vp, &r);
            ImVec2 origin = { (float)r.left, (float)r.top };

            g_Origin.exchange(origin);
            {
                ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
            }
            if (ImGui::BeginMainMenuBar())
            {
                ImVec2 pos = ImGui::GetWindowPos();
                ImVec2 size = ImGui::GetWindowSize();
                RECT rect = { origin.x + pos.x, origin.y + pos.y, origin.x + (pos.x + size.x), origin.y + (pos.y + size.y) };
                WindowRects.push_back(rect);
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("New"))
                    {
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMainMenuBar();
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
            ImGui::SetNextWindowBgAlpha(254.0f);
            ImGuiWindowClass iwc;
            //iwc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoRendererClear;
            ImGui::SetNextWindowClass(&iwc);
            // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
            if (show_demo_window)
            { 
                ImGui::ShowDemoWindow(&show_demo_window);
            }
            ImGui::PopStyleColor();
            // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
            {
                static float f = 0.0f;
                static int counter = 0;

                ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.
                ImVec2 pos = ImGui::GetWindowPos();
                ImVec2 size = ImGui::GetWindowSize();
                RECT rect = { origin.x + pos.x, origin.y + pos.y, origin.x + (pos.x + size.x), origin.y + (pos.y + size.y) };
                WindowRects.push_back(rect);

                ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
                ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
                ImGui::Checkbox("Another Window", &show_another_window);

                ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
                ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

                if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
                    counter++;
                ImGui::SameLine();
                ImGui::Text("counter = %d", counter);

                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                ImGui::End();
            }

            // 3. Show another simple window.
            if (show_another_window)
            {
                ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
                ImGui::Text("Hello from another window!");
                if (ImGui::Button("Close Me"))
                    show_another_window = false;
                ImVec2 pos = ImGui::GetWindowPos();
                ImVec2 size = ImGui::GetWindowSize();
                RECT rect = { origin.x + pos.x, origin.y + pos.y, origin.x + (pos.x + size.x), origin.y + (pos.y + size.y) };
                WindowRects.push_back(rect);

                {


                    ImGui::Checkbox("Use Gradient", &gradient);
                    static bool aero = TRUE;
                    ImGui::Checkbox("Toggle Aero", &aero);


                    ImGui::ColorPicker4("##picker", (float*)&color, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
                    int gradient_col = (((int)(color.w * 255)) << 24) | (((int)(color.z * 255)) << 16) | (((int)(color.y * 255)) << 8) | ((int)(color.x * 255));
                    //::MessageBoxA(nullptr, std::string(std::to_string(gradient_col)).c_str(), "Unhandled Exception", MB_OK | MB_ICONERROR);
                    std::cout << color.x << " " << color.y << " " << color.z << " " << color.w << std::endl;
                    std::string(std::to_string(gradient_col));
                    if (gradient_col != last_gradient_col)
                    {

                        ACCENT_POLICY policy = {
                        (aero) ? ACCENT_ENABLE_BLURBEHIND : ACCENT_ENABLE_TRANSPARENTGRADIENT,
                        (gradient) ? 2 : 0,
                        gradient_col,
                        //0xFF000000,
                        0
                        };

                        const WINDOWCOMPOSITIONATTRIBDATA data = {
                            WCA_ACCENT_POLICY,
                            &policy,
                            sizeof(policy)
                        };

                        SetWindowCompositionAttribute(hwnd, &data);
                    }
                    last_gradient_col = gradient_col;


                }
                ImGui::End();
            }
            // update window rects
            ATOMIC_RECTVA updated;
            for (RECT rect : WindowRects)
            {
                updated.rects[updated.size] = rect;
                updated.size += 1;
            }
            g_ImGuiWindows.exchange(updated);
            

            
            // Rendering
            ImGui::Render();
            glViewport(0, 0, g_Width, g_Height);
            glEnable(GL_ALPHA_TEST);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_COLOR_MATERIAL);

            glEnable(GL_LIGHTING);
            glEnable(GL_LIGHT0);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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
    }

    catch (const std::exception& e)
    {
        ::MessageBoxA(nullptr, e.what(), "Unhandled Exception", MB_OK | MB_ICONERROR);
        std::cout << e.what() << std::endl;
        Sleep(10000);
        returncode = 1;
    }

    return returncode;
}
bool CreateDeviceWGL(HWND hWnd, WGL_WindowData* data)
{
    HDC hDc = ::GetDC(hWnd);
    //PIXELFORMATDESCRIPTOR pfd = { 0 };
    //pfd.nSize = sizeof(pfd);
    //pfd.nVersion = 1;
    //pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_SUPPORT_COMPOSITION;
    //pfd.iPixelType = PFD_TYPE_RGBA;
    //pfd.cColorBits = 32;
    ////===
    //pfd.cAlphaBits = 8;
    PIXELFORMATDESCRIPTOR pfd = {
      sizeof(PIXELFORMATDESCRIPTOR),
      1,                                // Version Number
      PFD_DRAW_TO_WINDOW |         // Format Must Support Window
      PFD_SUPPORT_OPENGL |         // Format Must Support OpenGL
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

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_CREATE:
    {
        RECT rcClient;
        GetWindowRect(hWnd, &rcClient);

        // Inform the application of the frame change.
        SetWindowPos(hWnd,
            NULL,
            rcClient.left, rcClient.top,
            rcClient.right - rcClient.left, rcClient.bottom - rcClient.top,
            SWP_FRAMECHANGED);
        return 0;
    }
    case WM_NCCALCSIZE: {
        if (wParam == TRUE && true) {
            auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            //adjust_maximized_client_rect(hwnd, params.rgrc[0]);
            return 0;
        }
        break;
    }
    case WM_NCHITTEST: {
        // When we have no border or title bar, we need to perform our
        // own hit testing to allow resizing and moving.
        if (true)
        {
            bool borderless_drag = true;
            bool borderless_resize = true;
            POINT cursor = POINT{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam) };

            const POINT border{
                ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER),
                ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER)
            };
            RECT window;
            if (!::GetWindowRect(hWnd, &window)) {
                return HTNOWHERE;
            }

            const auto drag = borderless_drag ? HTCAPTION : HTCLIENT;

            enum region_mask {
                client = 0b0000,
                left = 0b0001,
                right = 0b0010,
                top = 0b0100,
                bottom = 0b1000,
            };
            ImVec2 origin = g_Origin.load();
            std::cout << "ORIGIN: " << "( " << origin.x << ", " << origin.y << ") ";
            std::cout << "( " << cursor.x << ", " << cursor.y << ") ";
            std::cout << "RECTS: ";

            bool leave = false;
            ATOMIC_RECTVA atomic_rects = g_ImGuiWindows.load();
            for (unsigned int i = 0; i < atomic_rects.size; ++i)
            {
                RECT r = atomic_rects.rects[i];
                std::cout << "( " << r.left << ", " << r.top << ")" << " ( " << r.right << ", " << r.bottom << ") ";
                if (PtInRect(&r, cursor))
                {
                    leave = true;
                    break;
                }
            }
            
            std::cout << std::endl;
            if (leave) break;
            const auto result =
                left * (cursor.x < (window.left + border.x)) |
                right * (cursor.x >= (window.right - border.x)) |
                top * (cursor.y < (window.top + border.y)) |
                bottom * (cursor.y >= (window.bottom - border.y));

            switch (result) {
            case left: return borderless_resize ? HTLEFT : drag;
            case right: return borderless_resize ? HTRIGHT : drag;
            case top: return borderless_resize ? HTTOP : drag;
            case bottom: return borderless_resize ? HTBOTTOM : drag;
            case top | left: return borderless_resize ? HTTOPLEFT : drag;
            case top | right: return borderless_resize ? HTTOPRIGHT : drag;
            case bottom | left: return borderless_resize ? HTBOTTOMLEFT : drag;
            case bottom | right: return borderless_resize ? HTBOTTOMRIGHT : drag;
            case client: return drag;
            default: return HTNOWHERE;
            }
        }
        break;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            g_Width = LOWORD(lParam);
            g_Height = HIWORD(lParam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        if ((wParam & 0xF012))
            std::cout << "DRAG MOVE\n";
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
