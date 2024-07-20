#pragma once
// Adapted from https://github.com/melak47/BorderlessWindow

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Windows.h>
#include "imgui.h"

#define BORDERLESS_USE_IMGUI // Tell BorderlessWindow to call ImGui_ImplWin32_WndProcHandler in WndProc

class BorderlessWindow
{
public:
    BorderlessWindow();
    BorderlessWindow(std::function<void()> render);

    VOID render_callback() { if (m_fRender) m_fRender(); };
    VOID set_render_callback(std::function<void()> render) { m_fRender = render; };
    VOID set_composition(BOOL enabled);
    VOID set_borderless(BOOL enabled);
    VOID set_borderless_shadow(BOOL enabled);
    VOID set_client_area(std::vector<RECT>& client_rects);
    VOID set_client_area(std::vector<RECT>&& client_rects);

    UINT get_width() CONST;
    UINT get_height() CONST;

    HWND m_hWND;
    LPWSTR m_wstrWC;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT hit_test(POINT cursor);

private:
    std::vector<RECT> m_vClientRects;
    std::function<void()> m_fRender; // currently unused, but will be necessary for proper redraw on resize
    static std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)> m_ImGui_ImplWin32_WndProcHandler;

    UINT m_uWidth;
    UINT m_uHeight;

    BOOL m_bCompositionEnabled = TRUE;
    BOOL m_bBorderless         = TRUE; // is the window currently borderless
    BOOL m_bBorderless_resize  = TRUE; // should the window allow resizing by dragging the borders while borderless
    BOOL m_bBorderless_drag    = TRUE; // should the window allow moving my dragging the client area
    BOOL m_bBorderless_shadow  = TRUE; // should the window display a native aero shadow while borderless
};

