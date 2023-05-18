// Adapted from https://github.com/melak47/BorderlessWindow

#include "BorderlessWindow.hpp"

#include <stdexcept>
#include <system_error>

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <iostream>

namespace 
{
	enum class Style : DWORD 
	{
		windowed = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
		aero_borderless = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
		basic_borderless = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX
	};

	BOOL is_maximized(HWND hwnd)
	{
		WINDOWPLACEMENT placement{};
		if (!::GetWindowPlacement(hwnd, &placement)) return false;
		return placement.showCmd == SW_MAXIMIZE;
	}

	BOOL is_composition_enabled()
	{
		BOOL composition_enabled = FALSE;
		bool success = ::DwmIsCompositionEnabled(&composition_enabled) == S_OK;
		return composition_enabled && success;
	}

	Style select_borderless_style()
	{
		return is_composition_enabled() ? Style::aero_borderless : Style::basic_borderless;
	}

	VOID set_shadow(HWND handle, BOOL enabled)
	{
		if (is_composition_enabled()) 
		{
			static CONST MARGINS shadow_state[2]{ { 0,0,0,0 },{ 1,1,1,1 } };
			::DwmExtendFrameIntoClientArea(handle, &shadow_state[enabled]);
		}
	}

	VOID adjust_maximized_client_rect(HWND window, RECT& rect)
	{
		if (!is_maximized(window)) return;

		HMONITOR monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
		if (!monitor) return;

		MONITORINFO monitor_info{};
		monitor_info.cbSize = sizeof(monitor_info);
		if (!::GetMonitorInfoW(monitor, &monitor_info)) return;

		// when maximized, make the client area fill just the monitor (without task bar) rect,
		// not the whole window rect which extends beyond the monitor.
		rect = monitor_info.rcWork;
	}

	std::system_error last_error(const std::string& msg)
	{
		return std::system_error(std::error_code(::GetLastError(), std::system_category()), msg);
	}

	CONST WCHAR* window_class(WNDPROC wndproc)
	{
		static CONST WCHAR* window_class_name = [&]
		{
			WNDCLASSEXW wcx{};
			wcx.cbSize = sizeof(wcx);
			wcx.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
			wcx.hInstance = nullptr;
			wcx.lpfnWndProc = wndproc;
			wcx.lpszClassName = L"BorderlessWindowClass";
			wcx.hbrBackground = NULL;
			wcx.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);

			const ATOM result = ::RegisterClassExW(&wcx);
			if (!result) throw last_error("failed to register window class");

			return wcx.lpszClassName;
		}();

		return window_class_name;
	}

	unique_handle create_window(WNDPROC wndproc, void* userdata)
	{
		HWND handle = CreateWindowExW(0, window_class(wndproc), L"Borderless Window",
			static_cast<DWORD>(Style::aero_borderless), CW_USEDEFAULT, CW_USEDEFAULT,
			1280, 720, nullptr, nullptr, nullptr, userdata
		);

		if (!handle) throw last_error("failed to create window");

		return unique_handle{ handle };
	}
}

BorderlessWindow::BorderlessWindow()
	: m_hHWND{ create_window(&BorderlessWindow::WndProc, this) }
{
	GetClassNameW(m_hHWND.get(), m_wstrWC, 256);
	set_composition(true);
	set_borderless(true);
	set_borderless_shadow(true);
	::ShowWindow(m_hHWND.get(), SW_SHOW);

#ifdef BORDERLESS_DEBUG
	AllocConsole();

	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
#endif
}

BorderlessWindow::BorderlessWindow(std::function<void()> render) 
	: m_hHWND{ create_window(&BorderlessWindow::WndProc, this) }, m_fRender(render)
{
	GetClassNameW(m_hHWND.get(), m_wstrWC, 256);
	set_composition(true);
	set_borderless(true);
	set_borderless_shadow(true);
	::ShowWindow(m_hHWND.get(), SW_SHOW);
}

VOID BorderlessWindow::set_composition(BOOL enabled)
{
	DWM_BLURBEHIND bb = { 0 };
	HRGN hRgn = CreateRectRgn(0, 0, -1, -1);
	bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
	bb.hRgnBlur = hRgn;
	bb.fEnable = TRUE;
	DwmEnableBlurBehindWindow(m_hHWND.get(), &bb);
}

VOID BorderlessWindow::set_borderless(BOOL enabled)
{
	Style new_style = (enabled) ? select_borderless_style() : Style::windowed;
	Style old_style = static_cast<Style>(::GetWindowLongPtrW(m_hHWND.get(), GWL_STYLE));

	if (new_style != old_style) 
	{
		m_bBorderless = enabled;

		::SetWindowLongPtrW(m_hHWND.get(), GWL_STYLE, static_cast<LONG>(new_style));

		// when switching between borderless and windowed, restore appropriate shadow state
		set_shadow(m_hHWND.get(), m_bBorderless_shadow && (new_style != Style::windowed));

		// redraw frame
		::SetWindowPos(m_hHWND.get(), nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
		::ShowWindow(m_hHWND.get(), SW_SHOW);
	}
}

VOID BorderlessWindow::set_borderless_shadow(BOOL enabled)
{
	if (m_bBorderless)
	{
		m_bBorderless_shadow = enabled;
		set_shadow(m_hHWND.get(), enabled);
	}
}

VOID BorderlessWindow::set_client_area(std::vector<RECT>& client_rects)
{
	m_vClientRects = client_rects;
}

VOID BorderlessWindow::set_client_area(std::vector<RECT>&& client_rects)
{
	m_vClientRects = std::move(client_rects);
}

UINT BorderlessWindow::get_width() const
{
	return 0;
}

UINT BorderlessWindow::get_height() const
{
	return 0;
}

#ifdef BORDERLESS_USE_IMGUI
	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)> BorderlessWindow::m_ImGui_ImplWin32_WndProcHandler = ImGui_ImplWin32_WndProcHandler;
#else
	std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)> BorderlessWindow::m_ImGui_ImplWin32_WndProcHandler;
#endif

LRESULT BorderlessWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
#ifdef BORDERLESS_USE_IMGUI
	if (m_ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
		return true;
#endif

	if (msg == WM_NCCREATE) {
		auto userdata = reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams;
		// store window instance pointer in window user data
		::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(userdata));
	}

	if (auto window_ptr = reinterpret_cast<BorderlessWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
		auto& window = *window_ptr;
		
		switch (msg) {
		case WM_NCCALCSIZE: {
			if (wparam == TRUE && window.m_bBorderless) {
				auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
				adjust_maximized_client_rect(hwnd, params.rgrc[0]);
				return 0;
			}
			break;
		}

		case WM_NCHITTEST: {
			// When we have no border or title bar, we need to perform our
			// own hit testing to allow resizing and moving.
			POINT cursor = POINT{
					GET_X_LPARAM(lparam),
					GET_Y_LPARAM(lparam)
			};
			//::ScreenToClient(hwnd, &cursor);
			if (window.m_bBorderless) {
				return window.hit_test(cursor);
			}
			break;
		}
		case WM_NCACTIVATE: {
			if (!is_composition_enabled()) {
				// Prevents window frame reappearing on window activation
				// in "basic" theme, where no aero shadow is present.
				return 1;
			}
			break;
		}
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN: {
			switch (wparam) {
				case VK_F8: { window.m_bBorderless_drag = !window.m_bBorderless_drag;        return 0; }
				case VK_F9: { window.m_bBorderless_resize = !window.m_bBorderless_resize;    return 0; }
				case VK_F10: { window.set_borderless(!window.m_bBorderless);                 return 0; }
				case VK_F11: { window.set_borderless_shadow(!window.m_bBorderless_shadow);   return 0; }
			}
			break;
		}
		case WM_SETFOCUS:
			break;

		case WM_KILLFOCUS:
			break;

		case WM_SIZE:
			if (wparam != SIZE_MINIMIZED)
			{
				window.m_uWidth = LOWORD(lparam);
				window.m_uHeight = HIWORD(lparam);
			}
			return 0;
		case WM_SYSCOMMAND:
			if ((wparam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
				return 0;
#ifdef BORDERLESS_DEBUG
			if ((wparam & 0xF012))
				std::cout << "DRAG MOVE\n";
#endif
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		}
	}

	return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT BorderlessWindow::hit_test(POINT cursor)
{
	// identify borders and corners to allow resizing the window.
	// Note: On Windows 10, windows behave differently and
	// allow resizing outside the visible window frame.
	// This implementation does not replicate that behavior.

	const POINT border{
		::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER),
		::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER)
	};

	RECT window{};
	if (!::GetWindowRect(m_hHWND.get(), &window)) return HTNOWHERE;

	CONST UINT drag = m_bBorderless_drag ? HTCAPTION : HTCLIENT;

	enum region_mask {
		client = 0b0000,
		left = 0b0001,
		right = 0b0010,
		top = 0b0100,
		bottom = 0b1000,
	};

	const auto result =
		left * (cursor.x < (window.left + border.x)) |
		right * (cursor.x >= (window.right - border.x)) |
		top * (cursor.y < (window.top + border.y)) |
		bottom * (cursor.y >= (window.bottom - border.y));

	switch (result) {
	case left: return m_bBorderless_resize ? HTLEFT : drag;
	case right: return m_bBorderless_resize ? HTRIGHT : drag;
	case top: return m_bBorderless_resize ? HTTOP : drag;
	case bottom: return m_bBorderless_resize ? HTBOTTOM : drag;
	case top | left: return m_bBorderless_resize ? HTTOPLEFT : drag;
	case top | right: return m_bBorderless_resize ? HTTOPRIGHT : drag;
	case bottom | left: return m_bBorderless_resize ? HTBOTTOMLEFT : drag;
	case bottom | right: return m_bBorderless_resize ? HTBOTTOMRIGHT : drag;
	case client:
	{
		for (RECT rect : m_vClientRects)
		{
			if (PtInRect(&rect, cursor) && (drag == HTCAPTION))
			{
				return HTCLIENT;
			}
		}
		return drag;
	}
	default: return HTNOWHERE;
	}
}
