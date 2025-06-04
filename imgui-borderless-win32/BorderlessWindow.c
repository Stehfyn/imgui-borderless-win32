#include "BorderlessWindow.h"

#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "atls")

//extern "C" { 
  BOOL EndTask(
      HWND hWnd,
      BOOL fShutDown,
      BOOL fForce
      );
//}

/****** GDI Macro APIs *******************************************************/
#define ABS(x)         ((x < 0) ? -x : x)
#define RECTWIDTH(rc)  (ABS(rc.right - rc.left))
#define RECTHEIGHT(rc) (ABS(rc.bottom - rc.top))

/****** Message crackers *****************************************************/

#define FORWARD_MSG(hwnd, uMsg, wParam, lParam, fn)    \
    default: return (fn)((hwnd), (uMsg), (wParam), (lParam))

static
VOID CFORCEINLINE CALLBACK
SyncFrameChange(
    HWND hWnd
    );

static
BOOL FORCEINLINE APIPRIVATE
MonitorWorkRectFromWindow(
    HWND   hwnd,
    LPRECT rcWork
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
VOID CFORCEINLINE CALLBACK
OnNCPaint(
    HWND hwnd,
    HRGN hrgn
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
    const DWORD dwFlags = SWP_SHOWWINDOW | SWP_NOMOVE | SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER;

    static const MARGINS margins[2] = { {0,0,0,0}, {1,1,1,1} };
    DwmExtendFrameIntoClientArea(hWnd, &margins[TRUE]);
    RECT rcWindow;
    GetWindowRect(hWnd, &rcWindow);
    SetWindowPos(hWnd, 0, 0, 0, RECTWIDTH(rcWindow), RECTHEIGHT(rcWindow), dwFlags);
}

static
BOOL FORCEINLINE APIPRIVATE
MonitorWorkRectFromWindow(
    HWND   hwnd,
    LPRECT lprcWork)
{
    MONITORINFO mi;
    SecureZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    
    return GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), (LPMONITORINFO)&mi) && 
           CopyRect(lprcWork, &mi.rcWork);
}

static
BOOL CFORCEINLINE CALLBACK
OnNCCreate(
    HWND           hWnd,
    LPCREATESTRUCT lpCreateStruct)
{
    UINT_PTR offset;

    AtlThunkData_t* timerproc;
    timerproc = AtlThunk_AllocateData();
    AtlThunk_InitData(timerproc, (LPVOID)lpCreateStruct->lpCreateParams, (size_t)(uintptr_t)hWnd);

    SetLastError(NO_ERROR);
    offset = SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)timerproc);

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
    UNREFERENCED_PARAMETER(fActive);
    UNREFERENCED_PARAMETER(hwndActDeact);
    UNREFERENCED_PARAMETER(fMinimized);

    BOOL fAllowNcPaint = FALSE;
    BOOL fDisableTransitions = FALSE;
    enum DWMNCRENDERINGPOLICY eNcRenderingPolicy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(hWnd, DWMWA_ALLOW_NCPAINT, &fAllowNcPaint, sizeof(fAllowNcPaint));
    DwmSetWindowAttribute(hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &fDisableTransitions, sizeof(fDisableTransitions));
    DwmSetWindowAttribute(hWnd, DWMWA_NCRENDERING_POLICY, &eNcRenderingPolicy, sizeof(eNcRenderingPolicy));
    FORWARD_WM_NCACTIVATE(hWnd, fActive, hwndActDeact, fMinimized, DefWindowProc);
    return TRUE;
}

static 
VOID CFORCEINLINE CALLBACK
OnNCPaint(
    HWND hwnd,
    HRGN hrgn)
{
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hrgn);

    ValidateRgn(hwnd, hrgn);
    FORWARD_WM_NCPAINT(hwnd, hrgn, DefWindowProc);
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
      if (IsMaximized(hWnd))
      {
        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONULL);
        if (!hMonitor)
        {
          return 0;
        }
        MONITORINFO mi = { 0 };
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfo(hMonitor, &mi))
        {
          return 0;
        }
        lpcsp->rgrc[0] = mi.rcWork;
        return 0;
      }
      else
      {
        lpcsp->rgrc[1] = lpcsp->rgrc[2];

        lpcsp->rgrc[0].top += 6;
        lpcsp->rgrc[0].bottom -= 6;
        lpcsp->rgrc[0].left += 6;
        lpcsp->rgrc[0].right -= 6;
        
        return WVR_VALIDRECTS;
      }
    }
    return 0;
    //return (UINT)FORWARD_WM_NCCALCSIZE(hWnd, fCalcValidRects, lpcsp, DefWindowProc);
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
      ((LONG)(GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi))),
      ((LONG)(GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)))  // Padded border is symmetric for both x, y
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
    UNREFERENCED_PARAMETER(lpCreateStruct);

    HRGN region = CreateRectRgn(0, 0, -1, -1);
    DWM_BLURBEHIND bb = {};
    bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    bb.hRgnBlur = region;
    bb.fEnable = TRUE;
    DwmEnableBlurBehindWindow((HWND)hWnd, &bb);
    DeleteObject(region);
    SyncFrameChange(hWnd);
    //static const MARGINS margins = {-1};
    //DwmExtendFrameIntoClientArea(hWnd, &margins);
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
      //HRGN hRgn;
      //DWM_BLURBEHIND bb;
      //const MARGINS margins = { 1,1,1,1 };
      //DwmExtendFrameIntoClientArea(hWnd, &margins);
      //bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
      //bb.fEnable = TRUE;
      //bb.hRgnBlur = hRgn = CreateRectRgn(0, 0, -1, -1);
      //DwmEnableBlurBehindWindow(hWnd, &bb);
      //DeleteRgn(hRgn);
      const BOOL fEnabled = FALSE;
      DwmSetWindowAttribute(hWnd, DWMWA_NCRENDERING_POLICY, &fEnabled, sizeof(fEnabled));
      SyncFrameChange(hWnd);
    }
    //SyncFrameChange(hWnd);

    FORWARD_WM_ACTIVATE(hWnd, state, hwndActDeact, fMinimized, DefWindowProc);
}

static
VOID CFORCEINLINE CALLBACK
OnPaint(
    HWND hWnd)
{
    ValidateRect(hWnd, 0);
    FORWARD_WM_PAINT(hWnd, DefWindowProc);
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
    lpwpos->flags |= SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_DEFERERASE | SWP_NOREPOSITION;
    return 0;
    //return FORWARD_WM_WINDOWPOSCHANGING(hWnd, lpwpos, DefWindowProc);
}

static 
VOID CFORCEINLINE CALLBACK 
OnWindowPosChanged(
    HWND hWnd, 
    const LPWINDOWPOS lpwpos)
{
    //FORWARD_WM_WINDOWPOSCHANGED(hWnd, lpwpos, DefWindowProc);
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
      PostMessage(hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(0,0));
      return;
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
    HANDLE_MSG(hWnd,  WM_NCPAINT,           OnNCPaint);
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
    HANDLE_MSG(hWnd,  WM_CLOSE,             OnClose);
    HANDLE_MSG(hWnd,  WM_DESTROY,           OnDestroy);
    HANDLE_MSG(hWnd,  WM_SYSCOMMAND,        OnSysCommand);
    FORWARD_MSG(hWnd, uMsg, wParam, lParam, DefWindowProc);
    }
}

HWND CALLBACK
CreateBorderlessWindow(
    DWORD dwExStyle,
    DWORD dwStyle,
    int   nWidth,
    int   nHeight,
    DRAWPROC lpfnDrawProc)
{
    HWND       hWnd;
    WNDCLASSEX wcex = { 0 };

    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_OWNDC;
    wcex.lpfnWndProc = (WNDPROC)WndProc;
    wcex.lpszClassName = _T("win32_window");
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
        NULL, NULL, NULL, lpfnDrawProc
    );

    return hWnd;
}

BOOL
PumpMessageQueue(
    LPMSG msg)
{
    BOOL done = FALSE;
    PeekMessage(msg, 0, WM_TIMER, WM_TIMER, PM_NOREMOVE);
    while (PeekMessage(msg, 0, 0, 0, PM_REMOVE | PM_NOYIELD))
    {
        TranslateMessage(msg);
        DispatchMessage(msg);
        done |= (msg->message == WM_QUIT);
    }
    return !done;
}
