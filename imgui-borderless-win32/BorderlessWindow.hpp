#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Windows.h>
#include "imgui.h"
#define USE_IMGUI

struct hwnd_deleter {
    using pointer = HWND;
    auto operator()(HWND handle) const -> void {
        ::DestroyWindow(handle);
    }
};

using unique_handle = std::unique_ptr<HWND, hwnd_deleter>;

class BorderlessWindow
{
public:
    BorderlessWindow();
    BorderlessWindow(std::function<void()> render);

    VOID set_composition(BOOL enabled);
    VOID set_borderless(BOOL enabled);
    VOID set_borderless_shadow(BOOL enabled);
    VOID set_client_area(std::vector<RECT>& client_rects);
    VOID set_client_area(std::vector<RECT>&& client_rects);
    VOID set_imgui_wndprochandler(std::function<void()>&);

    UINT get_width() CONST;
    UINT get_height() CONST;

    unique_handle m_hHWND;
    LPWSTR m_wstrWC;
private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT hit_test(POINT cursor);

private:
    std::vector<RECT> m_vClientRects;
    std::function<void()> m_fRender;
    static std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)> m_ImGui_ImplWin32_WndProcHandler;

    UINT m_uWidth;
    UINT m_uHeight;

    BOOL m_bCompositionEnabled = TRUE;
    BOOL m_bBorderless         = TRUE; // is the window currently borderless
    BOOL m_bBorderless_resize  = TRUE; // should the window allow resizing by dragging the borders while borderless
    BOOL m_bBorderless_drag    = TRUE; // should the window allow moving my dragging the client area
    BOOL m_bBorderless_shadow  = TRUE; // should the window display a native aero shadow while borderless
};

