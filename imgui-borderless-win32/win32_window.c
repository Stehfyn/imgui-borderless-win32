#include "win32_window.h"

#include <windowsx.h>

LRESULT WINAPI
win32_wndproc(
    HWND   hWnd,
    UINT   msg,
    WPARAM wParam,
    LPARAM lParam);

VOID
win32_window_create(
    win32_window_t* w32Window,
    INT             width,
    INT             height,
    DWORD           styleEx,
    DWORD           style)
{
    WNDCLASSEX wcex = {0};
    {
        wcex.cbSize = sizeof(wcex);
        wcex.style = CS_VREDRAW | CS_HREDRAW | CS_OWNDC;
        wcex.lpfnWndProc = (WNDPROC)win32_wndproc;
        wcex.hInstance = NULL;
        wcex.lpszClassName = _T("win32_window");
        wcex.hbrBackground = (HBRUSH)(GetStockObject(BLACK_BRUSH));
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        CONST ATOM _ = RegisterClassEx(&wcex); (VOID)_;
        assert(_);
    }

    assert(w32Window);
    w32Window->pThis = w32Window;
    w32Window->hWnd = CreateWindowEx(
        styleEx,
        wcex.lpszClassName,
        _T("daedulus-demo"),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, NULL, w32Window->pThis
    );

    assert(w32Window->hWnd);
    assert(GetClassName(w32Window->hWnd, w32Window->tcClassName, 256));
}

LRESULT WINAPI
win32_wndproc(
    HWND   hWnd,
    UINT   msg,
    WPARAM wParam,
    LPARAM lParam)
{
    win32_window_t* w32Window = NULL;

    if (msg == WM_NCCREATE) {
        LPVOID userdata = (LPVOID)((LPCREATESTRUCT)lParam)->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)userdata);
    }

    w32Window = (win32_window_t*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    if (w32Window) {
        if (w32Window->msgHook) {
            return w32Window->msgHook(hWnd, msg, wParam, lParam);
        }
    }

    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default: break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

BOOL
win32_window_pump_message_loop(
    win32_window_t* w32Window,
    LPMSG           msg,
    BOOL            pumpHwnd)
{
    BOOL done = FALSE;
    while (PeekMessage(msg, (pumpHwnd) ? w32Window->hWnd : NULL, 0U, 0U, PM_REMOVE))
    {
        TranslateMessage(msg);
        DispatchMessage(msg);
        done |= (msg->message == WM_QUIT);
    }
    return !done;
}

VOID
win32_window_run_message_loop(
    win32_window_t* w32Window)
{
    for (;;) {
        MSG msg = { 0 };
        BOOL result = GetMessage(&msg, NULL, 0U, 0U);
        if (result > 0U) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                return;

            if (IsMaximized(w32Window->hWnd))
            {
                Sleep(100);
                continue;
            }
        }
        else break;
    }
}
