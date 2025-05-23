#define WIN32_LEAN_AND_MEAN
#define IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DISABLE_DEFAULT_ALLOCATORS
#include <windows.h>
#include <windowsx.h>
#include <winerror.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include "BorderlessWindow.h"
#include "swcadef.h"      // Courtesy of https://gist.github.com/sylveon/9c199bb6684fe7dffcba1e3d383fb609
#include <GL/gl.h>
#include <GL/wglext.h>
#include "wglex.h"
//#include <dcomp.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"
#pragma comment (lib, "shcore")
#pragma comment (lib, "dwmapi")
#pragma comment (lib, "opengl32")
#pragma comment (lib, "glu32")
#pragma comment (lib, "Comctl32")
//#pragma comment (lib, "dcomp")
namespace ImGuiBorderlessWin32 {
static constexpr DWORD windowed   = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
static constexpr DWORD borderless = WS_POPUPWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_VISIBLE;
static void ShowDemoWindow(HWND hWnd, ImVec4& clearColor);
}
// Data stored per platform window
struct WGL_WindowData { HDC hDC; };
#include <wingdi.h>
#include <format>
// Data
static HGLRC            g_hRC;
static WGL_WindowData   g_MainWindow;
static int              g_Width = 0;
static int              g_Height = 0;
// Forward declarations of helper functions
bool CreateDeviceWGL(HWND hWnd, WGL_WindowData* data);
void CleanupDeviceWGL(HWND hWnd, WGL_WindowData* data);
//void ResetDeviceWGL();
extern "C" {
  //__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}
static void Hook_Renderer_CreateWindow(ImGuiViewport* viewport);
static void Hook_Renderer_DestroyWindow(ImGuiViewport* viewport);
static void Hook_Platform_RenderWindow(ImGuiViewport* viewport, void*);
static void Hook_Renderer_SwapBuffers(ImGuiViewport* viewport, void*);

static void Draw(HWND hWnd);
static void Hack(HWND hWnd);

static
LRESULT CALLBACK
ImGuiSubclassproc(
    HWND      hWnd,
    UINT      uMsg,
    WPARAM    wParam,
    LPARAM    lParam,
    UINT_PTR  uIdSubclass,
    DWORD_PTR dwRefData);

static
LRESULT CALLBACK
ImGuiMultiviewportSubclassproc(
    HWND      hWnd,
    UINT      uMsg,
    WPARAM    wParam,
    LPARAM    lParam,
    UINT_PTR  uIdSubclass,
    DWORD_PTR dwRefData);
static void Test();
static void Demo(void*)
{

    HWND hWnd = CreateBorderlessWindow(0, ImGuiBorderlessWin32::borderless, 1080, 720, Draw);

    if (!hWnd)
      ExitProcess(EXIT_FAILURE);

    if (!CreateDeviceWGL(hWnd, &g_MainWindow))
    {
        CleanupDeviceWGL(hWnd, &g_MainWindow);
        ::DestroyWindow(hWnd);
        //::UnregisterClass(win32_window.tcClassName, GetModuleHandle(NULL));
    }

    wglMakeCurrent(g_MainWindow.hDC, g_hRC);

    ImGui_ImplWin32_EnableAlphaCompositing(hWnd);

    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO&    io    = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;     // Enable Multi-Viewport / Platform Windows
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;
    io.ConfigInputTrickleEventQueue = false;
    
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Circumvent CRT Heap Mismatch -- currently leaks handles when a non-primary viewport is merged
    ImGui::SetAllocatorFunctions(
        [](size_t sz, void*) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sz); },
        [](void* ptr, void*) { (void)ptr; /*HeapFree(GetProcessHeap(), 0, ptr);*/ }, // leak
        nullptr);

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_InitForOpenGL(hWnd);
    ImGui_ImplOpenGL3_Init(nullptr);
    ImGui_ImplWin32_EnableDpiAwareness();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        IM_ASSERT(platform_io.Renderer_CreateWindow == NULL);
        IM_ASSERT(platform_io.Renderer_DestroyWindow == NULL);
        IM_ASSERT(platform_io.Renderer_SwapBuffers == NULL);
        IM_ASSERT(platform_io.Platform_RenderWindow == NULL);
        platform_io.Renderer_CreateWindow  = Hook_Renderer_CreateWindow;
        platform_io.Renderer_DestroyWindow = Hook_Renderer_DestroyWindow;
        platform_io.Renderer_SwapBuffers   = Hook_Renderer_SwapBuffers;
        platform_io.Platform_RenderWindow  = Hook_Platform_RenderWindow;
    }

    if (!SetWindowSubclass(hWnd, ImGuiSubclassproc, 0, 0))
      ExitProcess(EXIT_FAILURE);

    wglSwapIntervalEXT(0);

    while(TRUE)
    {
        MSG msg;

        if (!PumpMessageQueue(&msg))
          break;

        if (IsIconic(hWnd))
        {
          WaitMessage();
        }
        else
        {
          wglWaitForVerticalBlank(hWnd);

          Draw(hWnd);

          if (!GetInputState())
          {
            MsgWaitForMultipleObjects(0, 0, 0, USER_TIMER_MINIMUM, QS_ALLEVENTS);
          }
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceWGL(hWnd, &g_MainWindow);
    wglDeleteContext(g_hRC);
    ::DestroyWindow(hWnd);
    //::UnregisterClass(win32_window.tcClassName, GetModuleHandle(NULL));
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
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    HRESULT hr = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE); // This can be set by a program's manifest or its corresponding registry settings
    if (E_INVALIDARG == hr)
    {
        return 1;
    }

    if (HANDLE hThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Demo, 0, 0, 0))
    {
      WaitForSingleObject(hThread, INFINITE);
    }

    ExitProcess(EXIT_SUCCESS);

} // main

static
LRESULT CALLBACK
ImGuiSubclassproc(
    HWND      hWnd,
    UINT      uMsg,
    WPARAM    wParam,
    LPARAM    lParam,
    UINT_PTR  uIdSubclass,
    DWORD_PTR dwRefData)
{
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);

    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
      return TRUE;

    switch (uMsg) {
    case WM_CREATE:
      SetTimer(hWnd, 67, 2 * USER_TIMER_MINIMUM, (TIMERPROC)Hack);
      break;
    case WM_NCCALCSIZE: {
      if (wParam)
      {
    case WM_WINDOWPOSCHANGED:
        wglWaitForVerticalBlank(hWnd);
        Draw(hWnd);
      }
      break;
    }
    case WM_NCHITTEST: {
      if (HIWORD(GetKeyState(VK_LCONTROL)))
      {
        return HTCAPTION;
      }
      break;
    }
    case WM_ENTERMENULOOP:
    case WM_ENTERSIZEMOVE: {
      SetTimer(hWnd, 1, USER_TIMER_MINIMUM, (TIMERPROC)Draw);
      break;
      //return 0;
    }
    //case WM_SIZING: {
    //  LPRECT lprcDrag = (LPRECT)lParam;
    //  g_Width = labs(lprcDrag->right - lprcDrag->left);
    //  g_Height = labs(lprcDrag->bottom - lprcDrag->top);
    //  break;
    //}
    case WM_SIZE: {
      if (SIZE_MINIMIZED != wParam) 
      {
        g_Width  = GET_X_LPARAM(lParam);
        g_Height = GET_Y_LPARAM(lParam);
        //glViewport(0, 0, g_Width, g_Height);
      }
      break;
    }
    case WM_TIMER: {
      wglWaitForVerticalBlank(hWnd);
      Draw(hWnd);
      DwmFlush();
      return 0;
    }
    case WM_EXITMENULOOP:
    case WM_EXITSIZEMOVE: {
      KillTimer(hWnd, 1);
      Draw(hWnd);
      return 0;
    }
    default:
      break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static
LRESULT CALLBACK
ImGuiMultiviewportSubclassproc(
    HWND      hWnd,
    UINT      uMsg,
    WPARAM    wParam,
    LPARAM    lParam,
    UINT_PTR  uIdSubclass,
    DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg) {
    case WM_ENTERMENULOOP:
    case WM_ENTERSIZEMOVE: {
      SetTimer(hWnd, 2, USER_TIMER_MINIMUM, (TIMERPROC)Draw);
      return 0;
    }
    case WM_TIMER: {
      wglWaitForVerticalBlank(GetParent(hWnd));
      Draw(hWnd);
      //DwmFlush();
      return 0;
    }
    case WM_EXITMENULOOP:
    case WM_EXITSIZEMOVE: {
      KillTimer(hWnd, 2);
      //Draw(hWnd);
      return 0;
    }
    case WM_NCDESTROY:
      RemoveWindowSubclass(hWnd, ImGuiMultiviewportSubclassproc, uIdSubclass);
      break;
    default:
      break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);

}

static void Draw(HWND hWnd)
{
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

    {
      Test();
    }

    if (wglCheckOcclusion(hWnd))
    {
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

      SwapBuffers(g_MainWindow.hDC);
    }
}

static void Hack(HWND hWnd)
{
  if (!IsIconic(hWnd))
  {
    POINT pt;
    if (GetCursorPos(&pt))
    {
      SendMessage(hWnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
    }
  }
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
            static bool enable_alpha_compositing = false;
            static bool enable_border_shadow     = false;

            if (ImGui::Checkbox("Enable Alpha Compositing", &enable_alpha_compositing))
            {
                if (enable_alpha_compositing) ImGui_ImplWin32_EnableAlphaCompositing(hWnd);
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
                //static const MARGINS margins[2] = { {-1,-1,-1,-1}, {1,1,1,1} };
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
    HDC hDc = GetDC(hWnd);
    PIXELFORMATDESCRIPTOR pfd = {
      sizeof(PIXELFORMATDESCRIPTOR),
      1,                                // Version Number 
      PFD_DRAW_TO_WINDOW |              // Format Must Support Window
      PFD_SUPPORT_OPENGL |              // Format Must Support OpenGL
      PFD_SUPPORT_COMPOSITION | PFD_GENERIC_ACCELERATED | PFD_SWAP_COPY | // Format Must Support Composition
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

    int pfAttribs[] = {
      WGL_DRAW_TO_WINDOW_EXT, GL_TRUE,
      WGL_SUPPORT_OPENGL_EXT, GL_TRUE,
      WGL_DOUBLE_BUFFER_EXT, GL_TRUE,
      WGL_PIXEL_TYPE_EXT, WGL_TYPE_RGBA_EXT,
      WGL_TRANSPARENT_EXT, GL_TRUE,
      WGL_COLOR_BITS_EXT, 32,
      WGL_DEPTH_BITS_EXT, 24,
      WGL_ALPHA_BITS_EXT, 8,
      WGL_SWAP_METHOD_EXT, WGL_SWAP_EXCHANGE_EXT,
      WGL_ACCELERATION_EXT, WGL_FULL_ACCELERATION_EXT,
      //WGL_SAMPLE_BUFFERS_EXT, 4, 
      GL_NONE
    };

    int pfEx = wglGetPixelFormat(hDc, pfAttribs);

    if (!pfEx)
      return false;

    int pf = ::ChoosePixelFormat(hDc, &pfd);
    if (pf == 0)
        return false;

    if (::SetPixelFormat(hDc, pfEx, &pfd) == FALSE)
        return false;
    ::ReleaseDC(hWnd, hDc);
    data->hDC = GetDC(hWnd);
    if (!g_hRC)
      //g_hRC = wglCreateContextAttribsARB(data->hDC, nullptr, nullptr);
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
    SetWindowSubclass((HWND)viewport->PlatformHandle, ImGuiMultiviewportSubclassproc, 0, 0);
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
    {
        wglMakeCurrent(data->hDC, g_hRC);
        wglSwapIntervalEXT(0);
    }
}

static void Hook_Renderer_SwapBuffers(ImGuiViewport* viewport, void*)
{
  if (WGL_WindowData* data = (WGL_WindowData*)viewport->RendererUserData)
  {
    wglSwapIntervalEXT(0);
    ::SwapBuffers(data->hDC);
  }    
        
}

static void Test()
{
  static float fov = 58.f;
  static float hdg = 180.0f;
  static const ImU32 c_green = ImGui::GetColorU32(ImVec4(0.0f, 1.0f, 0.0f, 1.0f));


  if (ImGui::Begin("bartest"))
  {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float c_scale = 0.75f;
    ImRect r = ImRect{ w->Pos, w->Pos + w->Size };

    ImRect rs = ImRect{ r.GetCenter() - (r.GetSize() * 0.5f * c_scale), r.GetCenter() + (r.GetSize() * 0.5f * c_scale) };
    ImGui::GetWindowDrawList()->AddRect(rs.Min, rs.Max, c_green);
    ImVec2 s = w->Size;

    static const auto c_centered = [](const char* cstr) -> ImVec2
      { return ImVec2(-0.5f, -0.5f) * ImGui::CalcTextSize(cstr); };

    static const auto c_radians = [](const float degrees) -> float
      { return (degrees * 3.1459267f) / 180.0f; };

    // Centered
    {
      auto str = std::format("{:2}", hdg);
      ImVec2 start = w->Pos + ImVec2((0.5f * s.x), 40.0f + (0.5f * s.y));
      ImGui::GetWindowDrawList()->AddRectFilled(start, start + ImVec2(5.0f, 30.0f), c_green);
      ImGui::GetWindowDrawList()->AddText(start + ImVec2(2.5f, 2.5f * ImGui::GetFontSize()) + c_centered(str.c_str()), c_green, str.c_str());
    }

    ImGui::GetWindowDrawList()->PushClipRect(rs.Min, rs.Max);
    for (int i = 0; i < 72; ++i)
    {
      float tick_angle = i * 5.0f;
      float relative_angle = hdg - tick_angle;
      float angle_delta = fabsf(relative_angle);

      if (angle_delta < (0.5f * fov))
      {

        float x_delta;

        {
          float relative_angle_radians = c_radians(relative_angle);
          float half_fov_radians = c_radians(0.5f * fov);

          //x_delta = ((0.5f * rs.GetWidth()) * tanf((relative_angle * 3.14159267f) / 180.0f)) / tanf((((0.5f * fov) * 3.14159267f) / 180.0f));
          x_delta = ((0.5f * rs.GetWidth()) * tanf(relative_angle_radians)) / tanf(half_fov_radians);
        }

        {
          auto str = std::format("{:2}", tick_angle);
          ImVec2 start = rs.Min + ImVec2((0.5f * rs.GetWidth()) + x_delta, 0.5f * rs.GetHeight());
          ImGui::GetWindowDrawList()->AddRectFilled(start, start + ImVec2(5.0f, 10.0f), c_green);
          ImGui::GetWindowDrawList()->AddText(start + ImVec2(2.5f, 1.5f * ImGui::GetFontSize()) + c_centered(str.c_str()), c_green, str.c_str());
        }

        //{
        //  ImVec2 start = w->Pos + ImVec2((0.5f * s.x) - x_delta, 0.5f * s.y);
        //  ImGui::GetWindowDrawList()->AddRectFilled(start, start + ImVec2(5.0f, 30.0f), c_green);
        //}
      }

    }
    ImGui::GetWindowDrawList()->PopClipRect();
  }
  ImGui::End();

  if (ImGui::Begin("bartestmods"))
  {
    ImGuiWindow* w = ImGui::GetCurrentWindow();

    ImVec2 s = w->Size;

    //ImGui::SliderFloat("fov", &fov, 40.0f, 180.0f);
    ImGui::SliderFloat("fov", &fov, 40.0f, 180.0f);
    ImGui::SliderFloat("hdg", &hdg, 0.0f, 360.0f);


  }
  ImGui::End();
}