#include "oglwindow.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <GL/gl.h>
#include <gl/wglext.h>

#pragma comment (lib, "dwmapi")
#pragma comment(lib, "ntdll")

#define EXIT_SUCCESS 0L
#define EXIT_FAILURE 1L
#define ABS(x)         ((x < 0) ? -x : x)
#define RECTWIDTH(rc)  (ABS(rc.right - rc.left))
#define RECTHEIGHT(rc) (ABS(rc.bottom - rc.top))
#define CLAMP(x, lo, hi)    (max(min(x, hi), lo))
/****** Message crackers *****************************************************/

#define FORWARD_MSG(hwnd, uMsg, wParam, lParam, fn)    \
    default: return (fn)((hwnd), (uMsg), (wParam), (lParam))

/* void Cls_OnEnterSizeMove(HWND hwnd) */
#define HANDLE_WM_ENTERSIZEMOVE(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd)), 0L)

/* void Cls_OnExitSizeMove(HWND hwnd */
#define HANDLE_WM_EXITSIZEMOVE(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd)), 0L)

/* void Cls_OnExitSizeMove(HWND hwnd */
#define HANDLE_WM_ENTERMENULOOP(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd), (BOOL)(wParam)), 0L)

/* void Cls_OnEnterSizeMove(HWND hwnd) */
#define HANDLE_WM_EXITMENULOOP(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd), (BOOL)(wParam)), 0L)

typedef struct _FIBERDATA
{
  LPVOID                           lpFiber;
  D3DKMT_WAITFORVERTICALBLANKEVENT vbe;

} FIBERDATA;

typedef const char* (WINAPI* PFNWGLGETEXTENSIONSSTRINGARBPROC)(HDC);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC) (int interval);
typedef BOOL(WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
typedef HPBUFFERARB(WINAPI* PFNWGLCREATEPBUFFERARBPROC)(HDC, int, int, int, const int*);
typedef HDC(WINAPI* PFNWGLGETPBUFFERDCARBPROC)(HPBUFFERARB);
typedef BOOL(WINAPI* PFNWGLRELEASEPBUFFERDCARBPROC)(HPBUFFERARB, HDC);
typedef BOOL(WINAPI* PFNWGLDESTROYPBUFFERARBPROC)(HPBUFFERARB);
typedef BOOL(WINAPI* PFNWGLQUERYPBUFFERARBPROC)(HPBUFFERARB, int, int*);
typedef BOOL(WINAPI* PFNWGLBINDTEXIMAGEARBPROC)(HPBUFFERARB, int);
typedef BOOL(WINAPI* PFNWGLRELEASETEXIMAGEARBPROC)(HPBUFFERARB, int);
PFNWGLSWAPINTERVALEXTPROC      wglSwapIntervalEXT;
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;
PFNWGLCREATEPBUFFERARBPROC     wglCreatePbufferARB;
PFNWGLGETPBUFFERDCARBPROC      wglGetPbufferDCARB;
PFNWGLDESTROYPBUFFERARBPROC    wglDestroyPbufferARB;
PFNWGLBINDTEXIMAGEARBPROC      wglBindTexImageARB;
PFNWGLRELEASETEXIMAGEARBPROC   wglReleaseTexImageARB;
#define GL_BGR                 (0x80E0)
#define GL_BGRA                (0x80E1)
/****** Private API **********************************************************/

static 
ATOM PFORCEINLINE APIPRIVATE
RegisterOGLWindowClass(
    VOID
    );

static
BOOL PFORCEINLINE APIPRIVATE
InitWGL(
    VOID
    );

static
BOOL PFORCEINLINE APIPRIVATE
CreateSurface(
    HDC dc,
    WGLSURFACE* pwglSurf
    );

static 
VOID PFORCEINLINE APIPRIVATE
SendPaint(
    LPVOID lpArgToCompletionRoutine,
    DWORD dwTimerLowValue,
    DWORD dwTimerHighValue
    );

static 
VOID PFORCEINLINE APIPRIVATE
PumpInputMessages(
    LPVOID lpArgToCompletionRoutine,
    DWORD dwTimerLowValue,
    DWORD dwTimerHighValue
    );

static
HDC PFORCEINLINE APIPRIVATE
CreateMemoryDevice(
    DWORD dmDisplayFrequency
    );

/****** Message Handlers *****************************************************/

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnDestroy(
    HWND hWnd
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnPaint(
    HWND hWnd
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnWindowPosChanged(
    HWND              hWnd,
    const LPWINDOWPOS lpwpos
    );

static
BOOL PFORCEINLINE CALLBACK
OGLWindow_OnNCCreate(
    HWND            hWnd,
    LPCREATESTRUCT  lpCreateStruct
    );

static
UINT PFORCEINLINE CALLBACK
OGLWindow_OnNCCalcSize(
    HWND               hWnd,
    BOOL               fCalcValidRects,
    NCCALCSIZE_PARAMS* lpcsp
    );

static
UINT PFORCEINLINE CALLBACK
OGLWindow_OnNCHitTest(
    HWND hWnd,
    int  x,
    int  y
    );

static
BOOL PFORCEINLINE CALLBACK
OGLWindow_OnEraseBkgnd(
    HWND hWnd,
    HDC  hDC
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnEnterMenuLoop(
    HWND hWnd,
    BOOL fIsTrackPopupMenu
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnExitMenuLoop(
    HWND hWnd,
    BOOL fIsShortcutMenu
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnEnterSizeMove(
    HWND hWnd
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnExitSizeMove(
    HWND hWnd
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnTimer(
    HWND hWnd,
    UINT uId
    );

/****** Private API Implementation *******************************************/

static 
ATOM PFORCEINLINE APIPRIVATE
RegisterOGLWindowClass(
    VOID)
{
    WNDCLASSEX wcx = { sizeof(wcx) };

    wcx.style         = CS_OWNDC | CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW;
    wcx.lpfnWndProc   = DefOGLWindowProc;
    wcx.hInstance     = GetModuleHandle(NULL);
    wcx.cbWndExtra    = sizeof(void*);
    wcx.hCursor       = LoadCursor(0, IDC_ARROW);
    wcx.hbrBackground = GetStockBrush(BLACK_BRUSH);
    wcx.lpszMenuName  = NULL;
    wcx.lpszClassName = OGLWINDOW_CLASS;

    return RegisterClassEx(&wcx);
}

static
BOOL PFORCEINLINE APIPRIVATE
InitWGL(
    VOID)
{
    WNDCLASSEX wcx = { sizeof(wcx) };
    wcx.lpfnWndProc = DefWindowProc;
    wcx.hInstance = GetModuleHandle(NULL);
    wcx.lpszClassName = TEXT("pbuff");
    LPTSTR szClassAtom = MAKEINTATOM(RegisterClassEx(&wcx));
    HWND hwnd = CreateWindow(szClassAtom, TEXT("pb"), WS_OVERLAPPEDWINDOW,
    100, 100, 100, 100, 0, 0, wcx.hInstance, 0);
    HDC dc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd) };
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;

    int pf = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, pf, &pfd);

    HGLRC hrc = wglCreateContext(dc);
    wglMakeCurrent(dc, hrc);
    wglSwapIntervalEXT = (void*)wglGetProcAddress("wglSwapIntervalEXT");
    wglChoosePixelFormatARB = (void*)wglGetProcAddress("wglChoosePixelFormatARB");
    wglCreatePbufferARB = (void*)wglGetProcAddress("wglCreatePbufferARB");
    wglGetPbufferDCARB = (void*)wglGetProcAddress("wglGetPbufferDCARB");
    wglDestroyPbufferARB = (void*)wglGetProcAddress("wglDestroyPbufferARB");
    wglBindTexImageARB = (void*)wglGetProcAddress("wglBindTexImageARB");
    wglReleaseTexImageARB = (void*)wglGetProcAddress("wglReleaseTexImageARB");
    wglMakeCurrent(0, 0);
    wglDeleteContext(hrc);
    ReleaseDC(hwnd, dc);
}

static
BOOL PFORCEINLINE APIPRIVATE
CreateSurface(
    HDC dc,
    WGLSURFACE* pwglSurf)
{
    //HDC dc = CreateDC(TEXT("DISPLAY"), NULL, NULL, NULL);
    
    int iattribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_DRAW_TO_PBUFFER_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB,  GL_TRUE,
        WGL_BIND_TO_TEXTURE_RGBA_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        0
    };

    int formats[1];
    UINT count;
    wglChoosePixelFormatARB(dc, iattribs, NULL, 1, formats, &count);

    // create pbuffer
    int pattribs[] = {
        WGL_TEXTURE_FORMAT_ARB, WGL_TEXTURE_RGBA_ARB,
        WGL_TEXTURE_TARGET_ARB, WGL_TEXTURE_2D_ARB,
        0
    };

    pwglSurf->hpb = wglCreatePbufferARB(dc, formats[0], 2560,1600, pattribs);
    pwglSurf->pbdc = wglGetPbufferDCARB(pwglSurf->hpb);
    pwglSurf->pbrc = wglCreateContext(pwglSurf->pbdc);
    wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc);
    wglBindTexImageARB(pwglSurf->pbdc, WGL_FRONT_LEFT_ARB);
    //glReadBuffer(GL_FRONT);
    
    //glPixelStorei(GL_PACK_ALIGNMENT, 4);
}

static 
VOID PFORCEINLINE APIPRIVATE
SendPaint(
    LPVOID lpArgToCompletionRoutine,
    DWORD dwTimerLowValue,
    DWORD dwTimerHighValue)
{
    SendMessage((HWND)lpArgToCompletionRoutine, WM_USER + 1, 0L, 0L);
}

static 
VOID PFORCEINLINE APIPRIVATE
PumpInputMessages(
    LPVOID lpArgToCompletionRoutine,
    DWORD dwTimerLowValue,
    DWORD dwTimerHighValue)
{
    MSG msg;

    while (PeekMessage(&msg, (HWND)lpArgToCompletionRoutine, WM_MOUSEFIRST, WM_MOUSELAST, PM_NOYIELD|PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    while (PeekMessage(&msg, (HWND)lpArgToCompletionRoutine, WM_KEYFIRST, WM_KEYLAST, PM_NOYIELD|PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

static
HDC PFORCEINLINE APIPRIVATE
CreateMemoryDevice(
    DWORD dmDisplayFrequency)
{
    D3DKMT_CREATEDCFROMMEMORY cdc = { 0 };

    cdc.Format = D3DDDIFMT_A8R8G8B8;
    cdc.Width = 1;
    cdc.Height = 1;
    cdc.Pitch = 4;
    //cdc.pMemory = VirtualAlloc(0, cdc.Width * cdc.Height * cdc.Pitch, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    cdc.pMemory = HeapAlloc(GetProcessHeap(), 0, cdc.Width * cdc.Height * cdc.Pitch);
    
    cdc.hDeviceDc = CreateDC(TEXT("DISPLAY"), NULL, NULL, NULL);

    if (!NT_SUCCESS(D3DKMTCreateDCFromMemory(&cdc)))
      __debugbreak();

    //return cdc.hDeviceDc;
    return cdc.hDc;
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnDestroy(
    HWND hWnd)
{
    UNREFERENCED_PARAMETER(hWnd);

    PostQuitMessage(EXIT_SUCCESS);
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnPaint(
    HWND hWnd)
{
    RECT rc;
    PAINTSTRUCT ps;
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    HDC hdc = BeginPaint(hWnd, &ps);
    GetClientRect(hWnd, &rc);
    //SIZE sz = { RECTWIDTH(ps.rcPaint), RECTHEIGHT(ps.rcPaint) };
    SIZE sz = { RECTWIDTH(rc), RECTHEIGHT(rc) };
    //wglReleaseTexImageARB(pwglSurf->pbdc, WGL_FRONT_LEFT_ARB);
    //(void) BitBlt(hdc, 0, 0, sz.cx, sz.cy, pwglSurf->pbdc, 0, 0, SRCCOPY|CAPTUREBLT);
    //StretchBlt(hdc, 0, 0, sz.cx, sz.cy, pwglSurf->pbdc, 0, 0, 256, 256, SRCCOPY);
    //wglBindTexImageARB(pwglSurf->pbdc, WGL_FRONT_LEFT_ARB);
    BITMAPINFOHEADER bmih = { sizeof(bmih), sz.cx, sz.cy, 1, 32, BI_RGB };
    static BYTE pixels[2560 * 1600 * 4] = { 0 };
    glReadPixels(0, 0, sz.cx, sz.cy, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    StretchDIBits(hdc, 0, 0, sz.cx, sz.cy, 0, 0, sz.cx, sz.cy, pixels, (const BITMAPINFO*)&bmih, DIB_RGB_COLORS, SRCCOPY);
    (void) EndPaint(hWnd, &ps);
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnWindowPosChanged(
    HWND              hWnd,
    const LPWINDOWPOS lpwpos)
{
    if ((0 == (SWP_NOSIZE & lpwpos->flags)))
    {
      SwitchToFiber(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }
}

static
BOOL PFORCEINLINE CALLBACK
OGLWindow_OnNCCreate(
    HWND            hWnd,
    LPCREATESTRUCT  lpCreateStruct)
{
    WGLSURFACE* pwglSurf;

    if (pwglSurf = HeapAlloc(GetProcessHeap(), 0, sizeof(WGLSURFACE)))
    {
      InitWGL();

      CreateSurface(GetDC(hWnd), pwglSurf);

      //HDC hdc = CreateMemoryDevice(180);

      SetLastError(NO_ERROR);

      LONG_PTR prev_value = SetWindowLongPtr(hWnd, 0, (LONG_PTR)pwglSurf);
      if (NO_ERROR != GetLastError())
        __debugbreak();
      
      SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lpCreateStruct->lpCreateParams);

      //if (!NT_SUCCESS(D3DKMTInitVerticalBlankEvent(hdc, pVbe)))
      //  __debugbreak();

      //if (!NT_SUCCESS(D3DKMTWaitForVerticalBlankEvent(pVbe)))
      //  __debugbreak();
    }

    return FORWARD_WM_NCCREATE(hWnd, lpCreateStruct, DefWindowProc);
}

static
UINT PFORCEINLINE CALLBACK
OGLWindow_OnNCCalcSize(
    HWND               hWnd,
    BOOL               fCalcValidRects,
    NCCALCSIZE_PARAMS* lpcsp)
{
    LRESULT lResult;
    
    if (DwmDefWindowProc(hWnd, WM_NCCALCSIZE, (WPARAM)fCalcValidRects, (LPARAM)lpcsp, &lResult))
      return (UINT)lResult;

    if (fCalcValidRects && (!IsZoomed(hWnd)))
    {
      DwmFlush();
      SwitchToFiber(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    return FORWARD_WM_NCCALCSIZE(hWnd, fCalcValidRects, lpcsp, DefWindowProc);
}

static
UINT PFORCEINLINE CALLBACK
OGLWindow_OnNCHitTest(
    HWND hWnd,
    int  x,
    int  y)
{
    LRESULT lResult;

    if (DwmDefWindowProc(hWnd, WM_NCHITTEST, 0L, MAKELPARAM(x, y), &lResult))
      return (UINT)lResult;

    return FORWARD_WM_NCHITTEST(hWnd, x, y, DefWindowProc);
}

static
BOOL PFORCEINLINE CALLBACK
OGLWindow_OnEraseBkgnd(
    HWND hWnd,
    HDC  hDC)
{
    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(hDC);
    ValidateRect(hWnd, 0);

    return TRUE;
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnEnterMenuLoop(
    HWND hWnd,
    BOOL fIsTrackPopupMenu)
{
    UNREFERENCED_PARAMETER(fIsTrackPopupMenu);

    SetTimer(hWnd, 1, USER_TIMER_MINIMUM, NULL);
    SetTimer(hWnd, 2, USER_TIMER_MINIMUM + 2, NULL);
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnExitMenuLoop(
    HWND hWnd,
    BOOL fIsShortcutMenu)
{
    UNREFERENCED_PARAMETER(fIsShortcutMenu);

    KillTimer(hWnd, 1);
    KillTimer(hWnd, 2);

    SwitchToFiber(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    DwmFlush();
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnEnterSizeMove(
    HWND hWnd)
{
    SetTimer(hWnd, 1, USER_TIMER_MINIMUM, NULL);
    SetTimer(hWnd, 2, USER_TIMER_MINIMUM+1, NULL);
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnExitSizeMove(
    HWND hWnd)
{
    KillTimer(hWnd, 1);
    KillTimer(hWnd, 2);
    
    //D3DKMT_WAITFORVERTICALBLANKEVENT* pVbe;
    //
    //pVbe = GetWindowLongPtr(hWnd, 0);
    //
    //if (!NT_SUCCESS(D3DKMTWaitForVerticalBlankEvent(pVbe)))
    //  __debugbreak();
    //
    SwitchToFiber(GetWindowLongPtr(hWnd, GWLP_USERDATA));
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnTimer(
    HWND hWnd,
    UINT uId)
{
    //D3DKMT_WAITFORVERTICALBLANKEVENT* pVbe;
    //
    //pVbe = GetWindowLongPtr(hWnd, 0);
    //
    //if (!NT_SUCCESS(D3DKMTWaitForVerticalBlankEvent(pVbe)))
    //  __debugbreak();
    SwitchToFiber(GetWindowLongPtr(hWnd, GWLP_USERDATA));
}

/****** Public Interface Implementation **************************************/

WINOGLWINDOWAPI VOID WINAPI InitOGLControls(VOID)
{
    RegisterOGLWindowClass();
}

EXTERN_C
NTSTATUS PFORCEINLINE WINAPI
D3DKMTInitVerticalBlankEvent(
    HDC                               hdc,
    D3DKMT_WAITFORVERTICALBLANKEVENT* pVbe)
{
    NTSTATUS status;
    D3DKMT_OPENADAPTERFROMHDC oa = { hdc };
  
    if (NT_SUCCESS(status = D3DKMTOpenAdapterFromHdc(&oa)))
    {
      pVbe->hAdapter = oa.hAdapter;
      pVbe->VidPnSourceId = oa.VidPnSourceId;
      pVbe->hDevice = NULL;
    }

    return status;
}

WINOGLWINDOWAPI HDC WINAPI BeginOGLWindowPaint(HWND hWnd)
{
    return NULL;
}

WINOGLWINDOWAPI BOOL WINAPI EndOGLWindowPaint(HDC hDC)
{
    return 1;
}

WINOGLWINDOWAPI VOID WINAPI MessageFiberProc(void* unused)
{
    for (;;)
    {
        MSG msg;

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE|PM_NOYIELD))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
                ExitProcess(0);
        }

        SwitchToFiber(unused);
    }
}

WINOGLWINDOWAPI LRESULT CALLBACK DefOGLWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg) {
    HANDLE_MSG(hWnd, WM_DESTROY, OGLWindow_OnDestroy);
    HANDLE_MSG(hWnd, WM_PAINT, OGLWindow_OnPaint);
    HANDLE_MSG(hWnd, WM_WINDOWPOSCHANGED, OGLWindow_OnWindowPosChanged);
    HANDLE_MSG(hWnd, WM_NCCREATE, OGLWindow_OnNCCreate);
    HANDLE_MSG(hWnd, WM_NCCALCSIZE, OGLWindow_OnNCCalcSize);
    HANDLE_MSG(hWnd, WM_NCHITTEST, OGLWindow_OnNCHitTest);
    HANDLE_MSG(hWnd, WM_ERASEBKGND, OGLWindow_OnEraseBkgnd);
    HANDLE_MSG(hWnd, WM_ENTERMENULOOP, OGLWindow_OnEnterMenuLoop);
    HANDLE_MSG(hWnd, WM_EXITMENULOOP, OGLWindow_OnExitMenuLoop);
    HANDLE_MSG(hWnd, WM_ENTERSIZEMOVE, OGLWindow_OnEnterSizeMove);
    HANDLE_MSG(hWnd, WM_EXITSIZEMOVE, OGLWindow_OnExitSizeMove);
    HANDLE_MSG(hWnd, WM_TIMER, OGLWindow_OnTimer);
    FORWARD_MSG(hWnd, uMsg, wParam, lParam, DefWindowProc);
    }
}