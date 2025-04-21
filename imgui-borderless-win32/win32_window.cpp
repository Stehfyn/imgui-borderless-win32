#include "win32_window.h"

#include <windowsx.h>
#include <dwmapi.h>

extern "C" { 
  BOOL EndTask(
      HWND hWnd,
      BOOL fShutDown,
      BOOL fForce
      );
}

/****** Message crackers *****************************************************/

#define FORWARD_MSG(hwnd, uMsg, wParam, lParam, fn)    \
    default: return (fn)((hwnd), (uMsg), (wParam), (lParam))

static
VOID CFORCEINLINE CALLBACK
SyncFrameChange(
    HWND hWnd
    );

/****** Message Handlers *****************************************************/

static
BOOL CFORCEINLINE CALLBACK
OnNCCreate(
    HWND           hWnd,
    LPCREATESTRUCT lpCreateStruct
    );

static
BOOL CFORCEINLINE CALLBACK
OnNCActivate(
    HWND hWnd,
    BOOL fActive,
    HWND hwndActDeact,
    BOOL fMinimized
    );

static 
UINT CFORCEINLINE CALLBACK
OnNCCalcSize(
    HWND hWnd,
    BOOL fCalcValidRects,
    NCCALCSIZE_PARAMS* lpcsp
    );

static
UINT CFORCEINLINE CALLBACK
OnNCHittest(
    HWND hWnd,
    int  x,
    int  y
    );

static
VOID CFORCEINLINE CALLBACK
OnNCMouseMove(
    HWND hwnd,
    int x,
    int y,
    UINT codeHitTest
    );

static
VOID CFORCEINLINE CALLBACK
OnNCDestroy(
    HWND hWnd
    );

static
BOOL CFORCEINLINE CALLBACK
OnCreate(
    HWND hWnd,
    LPCREATESTRUCT lpCreateStruct
    );

static
VOID CFORCEINLINE CALLBACK
OnActivate(
    HWND hWnd,
    UINT state,
    HWND hwndActDeact,
    BOOL fMinimized
    );

static
VOID CFORCEINLINE CALLBACK
OnPaint(
    HWND hWnd
    );

static
UINT CFORCEINLINE CALLBACK
OnEraseBkgnd(
    HWND hWnd,
    HDC  hDC
    );

static
VOID CFORCEINLINE CALLBACK
OnKeyUp(
    HWND hWnd,
    UINT vk,
    BOOL fDown,
    int  cRepeat,
    UINT flags
    );

static
BOOL CFORCEINLINE CALLBACK
OnWindowPosChanging(
    HWND        hWnd,
    LPWINDOWPOS lpwpos
    );

static 
VOID CFORCEINLINE CALLBACK 
OnWindowPosChanged(
    HWND hWnd, 
    const LPWINDOWPOS lpwpos
    );

static 
VOID CFORCEINLINE CALLBACK
OnSysCommand(
    HWND hWnd,
    UINT uCmd,
    int x,
    int y
    );

static
VOID CFORCEINLINE CALLBACK
OnClose(
    HWND hWnd
    );

static
VOID CFORCEINLINE CALLBACK
OnDestroy(
    HWND hWnd
    );

/****** Private API Implementation *******************************************/

static
VOID CFORCEINLINE CALLBACK
SyncFrameChange(
    HWND hWnd)
{
    const DWORD dwFlags = SWP_NOSIZE | SWP_NOMOVE | SWP_FRAMECHANGED;
    SetWindowPos(hWnd, 0, 0, 0, 0, 0, dwFlags);
}

static
BOOL CFORCEINLINE CALLBACK
OnNCCreate(
    HWND           hWnd,
    LPCREATESTRUCT lpCreateStruct)
{
    UINT_PTR offset;

    SetLastError(NO_ERROR);
    offset = SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lpCreateStruct->lpCreateParams);

    if ((offset == 0) && (NO_ERROR != GetLastError()))
      return FALSE;

   return FORWARD_WM_NCCREATE(hWnd, lpCreateStruct, DefWindowProc);
}

static
BOOL CFORCEINLINE CALLBACK
OnNCActivate(
    HWND hWnd,
    BOOL fActive,
    HWND hwndActDeact,
    BOOL fMinimized)
{
    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(fActive);
    UNREFERENCED_PARAMETER(hwndActDeact);
    UNREFERENCED_PARAMETER(fMinimized);
    return TRUE;
}

static 
UINT CFORCEINLINE CALLBACK
OnNCCalcSize(
    HWND hWnd,
    BOOL fCalcValidRects,
    NCCALCSIZE_PARAMS* lpcsp)
{
    if (fCalcValidRects) 
    {
      UINT dpi    = GetDpiForWindow(hWnd);
      int frame_x = GetSystemMetricsForDpi(SM_CXFRAME, dpi);
      int frame_y = GetSystemMetricsForDpi(SM_CYFRAME, dpi);
      int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
      RECT* requested_client_rect = lpcsp->rgrc;

      requested_client_rect->right  -= frame_x + padding;
      requested_client_rect->left   += frame_x + padding;
      requested_client_rect->bottom -= frame_y + padding;

      if (IsMaximized(hWnd)) {
        requested_client_rect->top += padding;
      }

      lpcsp->rgrc[1] = lpcsp->rgrc[2];

      //DwmFlush();
      
      return 0;
    }

    return 0;
}

static
UINT CFORCEINLINE CALLBACK
OnNCHittest(
    HWND hWnd,
    int  x,
    int  y)
{
    typedef enum tagNCHITMASK {
      NCHIT_CLIENT = 0b0000,
      NCHIT_LEFT   = 0b0001,
      NCHIT_RIGHT  = 0b0010,
      NCHIT_TOP    = 0b0100,
      NCHIT_BOTTOM = 0b1000,

    } NCHITMASK;

    int         result;
    RECT        rcWindow;
    UINT dpi = GetDpiForWindow(hWnd);

    CONST POINT cursor = {(LONG) x, (LONG) y};
    CONST SIZE  border =
    {
      (LONG)(GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)),
      (LONG)(GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi))  // Padded border is symmetric for both x, y
    };
    RtlSecureZeroMemory(&rcWindow, sizeof(rcWindow));
    GetWindowRect(hWnd, &rcWindow);

    result = NCHIT_LEFT   * (cursor.x <  (rcWindow.left   + border.cx)) |
             NCHIT_RIGHT  * (cursor.x >= (rcWindow.right  - border.cx)) |
             NCHIT_TOP    * (cursor.y <  (rcWindow.top    + border.cy)) |
             NCHIT_BOTTOM * (cursor.y >= (rcWindow.bottom - border.cy));

    switch (result) {
    case NCHIT_LEFT:                 return HTLEFT;
    case NCHIT_RIGHT:                return HTRIGHT;
    case NCHIT_TOP:                  return HTTOP;
    case NCHIT_BOTTOM:               return HTBOTTOM;
    case NCHIT_TOP    | NCHIT_LEFT:  return HTTOPLEFT;
    case NCHIT_TOP    | NCHIT_RIGHT: return HTTOPRIGHT;
    case NCHIT_BOTTOM | NCHIT_LEFT:  return HTBOTTOMLEFT;
    case NCHIT_BOTTOM | NCHIT_RIGHT: return HTBOTTOMRIGHT;
    case NCHIT_CLIENT: {
      return HTCLIENT;
    }   
    default: return FORWARD_WM_NCHITTEST(hWnd, x, y, DefWindowProc);
    }
}

static
VOID CFORCEINLINE CALLBACK
OnNCMouseMove(
    HWND hwnd,
    int x,
    int y,
    UINT codeHitTest)
{
    FORWARD_WM_NCMOUSEMOVE(hwnd, x, y, codeHitTest, DefWindowProc);
}

static
VOID CFORCEINLINE CALLBACK
OnNCDestroy(
    HWND hWnd)
{
    UNREFERENCED_PARAMETER(hWnd);
}

static
BOOL CFORCEINLINE CALLBACK
OnCreate(
    HWND hWnd,
    LPCREATESTRUCT lpCreateStruct)
{
    SyncFrameChange(hWnd);
    FORWARD_WM_CREATE(hWnd, lpCreateStruct, DefWindowProc);
    return TRUE;
}

static
VOID CFORCEINLINE CALLBACK
OnActivate(
    HWND hWnd,
    UINT state,
    HWND hwndActDeact,
    BOOL fMinimized)
{
    if (!fMinimized)
    {
      HRGN hRgn;
      DWM_BLURBEHIND bb;
      const MARGINS margins = { 1,1,1,1 };
      DwmExtendFrameIntoClientArea(hWnd, &margins);
      bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
      bb.fEnable = TRUE;
      bb.hRgnBlur = hRgn = CreateRectRgn(0, 0, -1, -1);
      DwmEnableBlurBehindWindow(hWnd, &bb);
      DeleteRgn(hRgn);
      SyncFrameChange(hWnd);
    }

    FORWARD_WM_ACTIVATE(hWnd, state, hwndActDeact, fMinimized, DefWindowProc);
}

static
VOID CFORCEINLINE CALLBACK
OnPaint(
    HWND hWnd)
{
    PAINTSTRUCT ps;
    BeginPaint(hWnd, &ps);
    EndPaint(hWnd, &ps);
}

static
UINT CFORCEINLINE CALLBACK
OnEraseBkgnd(
    HWND hWnd,
    HDC  hDC)
{
    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(hDC);
    return TRUE;
}

static
VOID CFORCEINLINE CALLBACK
OnKeyUp(
    HWND hWnd,
    UINT vk,
    BOOL fDown,
    int  cRepeat,
    UINT flags)
{
    UNREFERENCED_PARAMETER(fDown);

    switch (vk) {
    case VK_ESCAPE:
      if(GetForegroundWindow() == hWnd)
        EndTask(hWnd, FALSE, TRUE);
      break;
    default: FORWARD_WM_KEYUP(hWnd, vk, cRepeat, flags, DefWindowProc);
    }
}

static
BOOL CFORCEINLINE CALLBACK
OnWindowPosChanging(
    HWND        hWnd,
    LPWINDOWPOS lpwpos)
{
    lpwpos->flags |= SWP_NOCOPYBITS;
    return FORWARD_WM_WINDOWPOSCHANGING(hWnd, lpwpos, DefWindowProc);
}

static 
VOID CFORCEINLINE CALLBACK 
OnWindowPosChanged(
    HWND hWnd, 
    const LPWINDOWPOS lpwpos)
{
    DwmFlush();
    FORWARD_WM_WINDOWPOSCHANGED(hWnd, lpwpos, DefWindowProc);
}

static 
VOID CFORCEINLINE CALLBACK
OnSysCommand(
    HWND hWnd,
    UINT uCmd,
    int x,
    int y)
{
    switch (uCmd) {
    case SC_MOVE: {
      PostMessage(hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(-(MAXINT16 >> 1), -(MAXINT16 >> 1)));
    }
    }
    
    FORWARD_WM_SYSCOMMAND(hWnd, uCmd, x, y, DefWindowProc);
}

static
VOID CFORCEINLINE CALLBACK
OnClose(
    HWND hWnd)
{
    DestroyWindow(hWnd);
}

static
VOID CFORCEINLINE CALLBACK
OnDestroy(
    HWND hWnd)
{
    UNREFERENCED_PARAMETER(hWnd);
    PostQuitMessage(0);
}

LRESULT CALLBACK
WndProc(
    HWND   hWnd,
    UINT   uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch(uMsg) {
    HANDLE_MSG(hWnd,  WM_NCCREATE,          OnNCCreate);
    HANDLE_MSG(hWnd,  WM_NCACTIVATE,        OnNCActivate);
    HANDLE_MSG(hWnd,  WM_NCCALCSIZE,        OnNCCalcSize);
    HANDLE_MSG(hWnd,  WM_NCHITTEST,         OnNCHittest);
    HANDLE_MSG(hWnd,  WM_NCMOUSEMOVE,       OnNCMouseMove);
    HANDLE_MSG(hWnd,  WM_NCDESTROY,         OnNCDestroy);
    HANDLE_MSG(hWnd,  WM_CREATE,            OnCreate);
    HANDLE_MSG(hWnd,  WM_ACTIVATE,          OnActivate);
    HANDLE_MSG(hWnd,  WM_PAINT,             OnPaint);
    HANDLE_MSG(hWnd,  WM_ERASEBKGND,        OnEraseBkgnd);
    HANDLE_MSG(hWnd,  WM_KEYUP,             OnKeyUp);
    HANDLE_MSG(hWnd,  WM_WINDOWPOSCHANGING, OnWindowPosChanging);
    HANDLE_MSG(hWnd,  WM_WINDOWPOSCHANGED,  OnWindowPosChanged);
    HANDLE_MSG(hWnd,  WM_CLOSE,             OnClose);
    HANDLE_MSG(hWnd,  WM_DESTROY,           OnDestroy);
    FORWARD_MSG(hWnd, uMsg, wParam, lParam, DefWindowProc);
    }
}

HWND CALLBACK
CreateBorderlessWindow(
    DWORD dwExStyle,
    DWORD dwStyle,
    int   nWidth,
    int   nHeight)
{
    HWND       hWnd;
    WNDCLASSEX wcex = { 0 };

    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_OWNDC | CS_DROPSHADOW; // CS_VREDRAW | CS_HREDRAW | 
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = NULL;
    wcex.lpszClassName = _T("win32_window");
    wcex.hbrBackground = (HBRUSH)(GetStockObject(BLACK_BRUSH));
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    CONST ATOM _ = RegisterClassEx(&wcex); (VOID)_;
    assert(_);

    //SecureZeroMemory(&hWnd, sizeof(hWnd));
    hWnd = CreateWindowEx(
        dwExStyle,
        wcex.lpszClassName,
        _T("daedulus-demo"),
        dwStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, nWidth, nHeight,
        NULL, NULL, NULL, NULL
    );

    return hWnd;
}

BOOL
PumpMessageQueue(
    LPMSG msg)
{
    BOOL done = FALSE;
    while (PeekMessage(msg, 0, 0, 0, PM_REMOVE))
    {
        TranslateMessage(msg);
        DispatchMessage(msg);
        done |= (msg->message == WM_QUIT);
    }
    return !done;
}
