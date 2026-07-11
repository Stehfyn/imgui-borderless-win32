#include "oglwindow.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <GL/gl.h>

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
typedef int(WINAPI* PFNWGLRELEASEPBUFFERDCARBPROC)(HPBUFFERARB, HDC);
typedef BOOL(WINAPI* PFNWGLDESTROYPBUFFERARBPROC)(HPBUFFERARB);
typedef BOOL(WINAPI* PFNWGLQUERYPBUFFERARBPROC)(HPBUFFERARB, int, int*);
typedef BOOL(WINAPI* PFNWGLBINDTEXIMAGEARBPROC)(HPBUFFERARB, int);
typedef BOOL(WINAPI* PFNWGLRELEASETEXIMAGEARBPROC)(HPBUFFERARB, int);
PFNWGLSWAPINTERVALEXTPROC      wglSwapIntervalEXT;
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;
PFNWGLCREATEPBUFFERARBPROC     wglCreatePbufferARB;
PFNWGLGETPBUFFERDCARBPROC      wglGetPbufferDCARB;
PFNWGLRELEASEPBUFFERDCARBPROC  wglReleasePbufferDCARB;
PFNWGLDESTROYPBUFFERARBPROC    wglDestroyPbufferARB;
PFNWGLBINDTEXIMAGEARBPROC      wglBindTexImageARB;
PFNWGLRELEASETEXIMAGEARBPROC   wglReleaseTexImageARB;
#define GL_BGR                 (0x80E0)
#define GL_BGRA                (0x80E1)
#define OGLWINDOW_SURFACE_WIDTH  2560
#define OGLWINDOW_SURFACE_HEIGHT 1600

static DWM_TIMING_INFO g_DwmTiming = { sizeof(DWM_TIMING_INFO) };
static LONGLONG        g_LastPresentTicks;
static LONGLONG        g_LastResizeRenderTicks;
static UINT            g_SizingEdge;
static HWND            g_WindowPosChangedHwnd;
static HWND            g_SynchronousResizeHwnd;
static LONG            g_SynchronousResizeRenderDepth;
static HWND            g_PaintHwnd;
static HDC             g_PaintHdc;
static BOOL            g_PaintPresented;

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
BOOL PFORCEINLINE APIPRIVATE
PresentSurfaceToDC(
    HWND hWnd,
    HDC  hdc
    );

static
BOOL PFORCEINLINE APIPRIVATE
IsLayeredPresentWindow(
    HWND hWnd
    );

static
BOOL PFORCEINLINE APIPRIVATE
PresentSurfaceLayered(
    HWND hWnd
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
OGLWindow_OnPrintClient(
    HWND hWnd,
    HDC  hDC,
    UINT options
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnWindowPosChanged(
    HWND              hWnd,
    const LPWINDOWPOS lpwpos
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnSize(
    HWND hWnd,
    UINT state,
    int  cx,
    int  cy
    );

static
BOOL PFORCEINLINE CALLBACK
OGLWindow_OnSizing(
    HWND  hWnd,
    UINT  edge,
    RECT* prc
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnWindowPosChanging(
    HWND        hWnd,
    LPWINDOWPOS lpwpos
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
BOOL PFORCEINLINE APIPRIVATE
IsOriginMovingSizingEdge(
    UINT edge
    );

static
BOOL PFORCEINLINE APIPRIVATE
IsSecondaryViewportWindow(
    HWND hWnd
    );

static
VOID PFORCEINLINE APIPRIVATE
FlushIfOriginMovingResize(
    VOID
    );

static
BOOL PFORCEINLINE APIPRIVATE
SwitchToRenderFiber(
    HWND hWnd,
    BOOL synchronous_resize
    );

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnTimer(
    HWND hWnd,
    UINT uId
    );

/****** Private API Implementation *******************************************/

static
BOOL PFORCEINLINE APIPRIVATE
InitWGL(
    VOID)
{
    static BOOL pbuff_class_registered;
    WNDCLASSEX wcx = { sizeof(wcx) };
    HWND hwnd;
    HDC dc;
    int pf;
    HGLRC hrc;

    wcx.lpfnWndProc = DefWindowProc;
    wcx.hInstance = GetModuleHandle(NULL);
    wcx.lpszClassName = TEXT("pbuff");

    if (!pbuff_class_registered)
    {
      if (!RegisterClassEx(&wcx) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
      pbuff_class_registered = TRUE;
    }

    hwnd = CreateWindow(wcx.lpszClassName, TEXT("pb"), WS_OVERLAPPEDWINDOW, 100, 100, 100, 100, 0, 0, wcx.hInstance, 0);
    if (!hwnd)
      return FALSE;

    dc = GetDC(hwnd);
    if (!dc)
    {
      DestroyWindow(hwnd);
      return FALSE;
    }

    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd) };
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;

    pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd))
    {
      ReleaseDC(hwnd, dc);
      DestroyWindow(hwnd);
      return FALSE;
    }

    hrc = wglCreateContext(dc);
    if (!hrc)
    {
      ReleaseDC(hwnd, dc);
      DestroyWindow(hwnd);
      return FALSE;
    }
    wglMakeCurrent(dc, hrc);
    wglSwapIntervalEXT = (void*)wglGetProcAddress("wglSwapIntervalEXT");
    wglChoosePixelFormatARB = (void*)wglGetProcAddress("wglChoosePixelFormatARB");
    wglCreatePbufferARB = (void*)wglGetProcAddress("wglCreatePbufferARB");
    wglGetPbufferDCARB = (void*)wglGetProcAddress("wglGetPbufferDCARB");
    wglReleasePbufferDCARB = (void*)wglGetProcAddress("wglReleasePbufferDCARB");
    wglDestroyPbufferARB = (void*)wglGetProcAddress("wglDestroyPbufferARB");
    wglBindTexImageARB = (void*)wglGetProcAddress("wglBindTexImageARB");
    wglReleaseTexImageARB = (void*)wglGetProcAddress("wglReleaseTexImageARB");
    wglMakeCurrent(0, 0);
    wglDeleteContext(hrc);
    ReleaseDC(hwnd, dc);

    DestroyWindow(hwnd);

    return wglChoosePixelFormatARB &&
           wglCreatePbufferARB &&
           wglGetPbufferDCARB &&
           wglReleasePbufferDCARB &&
           wglDestroyPbufferARB;
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
    UINT count = 0;
    if (!wglChoosePixelFormatARB(dc, iattribs, NULL, 1, formats, &count) || count == 0)
      return FALSE;

    // create pbuffer
    int pattribs[] = {
        WGL_TEXTURE_FORMAT_ARB, WGL_TEXTURE_RGBA_ARB,
        WGL_TEXTURE_TARGET_ARB, WGL_TEXTURE_2D_ARB,
        0
    };

    pwglSurf->width = OGLWINDOW_SURFACE_WIDTH;
    pwglSurf->height = OGLWINDOW_SURFACE_HEIGHT;
    pwglSurf->hpb = wglCreatePbufferARB(dc, formats[0], pwglSurf->width, pwglSurf->height, pattribs);
    if (!pwglSurf->hpb)
      return FALSE;

    pwglSurf->pbdc = wglGetPbufferDCARB(pwglSurf->hpb);
    if (!pwglSurf->pbdc)
      return FALSE;

    pwglSurf->pbrc = wglCreateContext(pwglSurf->pbdc);
    if (!pwglSurf->pbrc)
      return FALSE;

    wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc);
    //glReadBuffer(GL_FRONT);

    //glPixelStorei(GL_PACK_ALIGNMENT, 4);

    /* Layered-present surface: a bottom-up 32bpp DIB section at pbuffer size.
     * glReadPixels writes rows bottom-up, matching the DIB layout, so the
     * readback lands presentation-ready with no flip or staging copy.
     * Failure is tolerated; the window then presents via the DC blit path. */
    {
      BITMAPINFO bmi = { 0 };
      bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
      bmi.bmiHeader.biWidth = pwglSurf->width;
      bmi.bmiHeader.biHeight = pwglSurf->height;
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;

      pwglSurf->uldc = CreateCompatibleDC(NULL);
      if (pwglSurf->uldc)
        pwglSurf->ulbmp = CreateDIBSection(pwglSurf->uldc, &bmi, DIB_RGB_COLORS, &pwglSurf->ulbits, NULL, 0);
      if (pwglSurf->ulbmp)
        pwglSurf->ulbmp_prev = (HBITMAP)SelectObject(pwglSurf->uldc, pwglSurf->ulbmp);
    }

    return TRUE;
}

static
BOOL PFORCEINLINE APIPRIVATE
PresentSurfaceToDC(
    HWND hWnd,
    HDC  hdc)
{
    RECT rc;
    SIZE sz;
    SIZE_T pixel_count;
    LARGE_INTEGER qpc_start;
    LARGE_INTEGER qpc_end;
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    if (!pwglSurf || !hdc)
      return FALSE;

    GetClientRect(hWnd, &rc);
    sz.cx = RECTWIDTH(rc);
    sz.cy = RECTHEIGHT(rc);
    if (sz.cx <= 0 || sz.cy <= 0)
      return FALSE;

    sz.cx = CLAMP(sz.cx, 1, pwglSurf->width);
    sz.cy = CLAMP(sz.cy, 1, pwglSurf->height);
    pixel_count = (SIZE_T)sz.cx * (SIZE_T)sz.cy * 4u;
    if (pixel_count > pwglSurf->pixels_capacity)
    {
      BYTE* next_pixels = pwglSurf->pixels
        ? (BYTE*)HeapReAlloc(GetProcessHeap(), 0, pwglSurf->pixels, pixel_count)
        : (BYTE*)HeapAlloc(GetProcessHeap(), 0, pixel_count);
      if (!next_pixels)
        return FALSE;

      pwglSurf->pixels = next_pixels;
      pwglSurf->pixels_capacity = pixel_count;
    }

    if (!wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc))
      return FALSE;

    QueryPerformanceCounter(&qpc_start);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, sz.cx, sz.cy, GL_BGRA, GL_UNSIGNED_BYTE, pwglSurf->pixels);
    SetStretchBltMode(hdc, COLORONCOLOR);

    {
      BITMAPINFOHEADER bmih = { sizeof(bmih), sz.cx, sz.cy, 1, 32, BI_RGB };
      StretchDIBits(hdc, 0, 0, RECTWIDTH(rc), RECTHEIGHT(rc), 0, 0, sz.cx, sz.cy, pwglSurf->pixels, (const BITMAPINFO*)&bmih, DIB_RGB_COLORS, SRCCOPY);
    }
    GdiFlush();
    QueryPerformanceCounter(&qpc_end);
    g_LastPresentTicks = qpc_end.QuadPart - qpc_start.QuadPart;

    return TRUE;
}

static
BOOL PFORCEINLINE APIPRIVATE
IsLayeredPresentWindow(
    HWND hWnd)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    /* Secondary viewports may carry WS_EX_LAYERED from SetWindowAlpha
     * (SetLayeredWindowAttributes mode), which is mutually exclusive with
     * UpdateLayeredWindow-style presentation on the same window. */
    return pwglSurf && pwglSurf->uldc && pwglSurf->ulbits &&
           !IsSecondaryViewportWindow(hWnd) &&
           (GetWindowLongPtr(hWnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
}

static
BOOL PFORCEINLINE APIPRIVATE
PresentSurfaceLayered(
    HWND hWnd)
{
    /* The latch: UpdateLayeredWindowIndirect carries bitmap + geometry in one
     * window-manager transaction, so the compositor can never pair fresh
     * geometry with stale content the way separately presented swapchain/blt
     * content can.  During a synchronous resize render this runs inside the
     * modal loop's SetWindowPos transaction; ULW_EX_NORESIZE makes the content
     * update join that transaction instead of competing for the window size. */
    RECT rc;
    SIZE sz;
    POINT ptSrc;
    UPDATELAYEREDWINDOWINFO ulwi = { sizeof(ulwi) };
    LARGE_INTEGER qpc_start;
    LARGE_INTEGER qpc_end;
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    if (!pwglSurf || !pwglSurf->uldc || !pwglSurf->ulbits)
      return FALSE;

    GetClientRect(hWnd, &rc);
    sz.cx = RECTWIDTH(rc);
    sz.cy = RECTHEIGHT(rc);
    if (sz.cx <= 0 || sz.cy <= 0)
      return FALSE;

    /* ULW would resize the window to psize; a clamped sprite must not shrink
     * the window, so windows larger than the surface use the DC blit path. */
    if (sz.cx > pwglSurf->width || sz.cy > pwglSurf->height)
      return FALSE;

    if (!wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc))
      return FALSE;

    QueryPerformanceCounter(&qpc_start);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, pwglSurf->width);
    glReadPixels(0, 0, sz.cx, sz.cy, GL_BGRA, GL_UNSIGNED_BYTE, pwglSurf->ulbits);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);

    /* Bottom-up DIB: GL rows occupy the bitmap's bottom sz.cy rows, which in
     * DC coordinates start at y = surface_height - sz.cy. */
    ptSrc.x = 0;
    ptSrc.y = pwglSurf->height - sz.cy;

    ulwi.hdcSrc = pwglSurf->uldc;
    ulwi.pptSrc = &ptSrc;
    ulwi.psize = &sz;
    ulwi.dwFlags = ULW_OPAQUE;
    if (IsOGLWindowInSynchronousResizeRender() || IsOGLWindowInModalSizeMove(hWnd))
      ulwi.dwFlags |= ULW_EX_NORESIZE;

    if (!UpdateLayeredWindowIndirect(hWnd, &ulwi))
      return FALSE;

    QueryPerformanceCounter(&qpc_end);
    g_LastPresentTicks = qpc_end.QuadPart - qpc_start.QuadPart;

    return TRUE;
}

static
BOOL PFORCEINLINE APIPRIVATE
IsOriginMovingSizingEdge(
    UINT edge)
{
    return edge == WMSZ_LEFT       ||
           edge == WMSZ_TOP        ||
           edge == WMSZ_TOPLEFT    ||
           edge == WMSZ_TOPRIGHT   ||
           edge == WMSZ_BOTTOMLEFT;
}

static
BOOL PFORCEINLINE APIPRIVATE
IsSecondaryViewportWindow(
    HWND hWnd)
{
    return GetPropA(hWnd, OGLWINDOW_SECONDARY_VIEWPORT_PROP) != NULL;
}

static
VOID PFORCEINLINE APIPRIVATE
FlushIfOriginMovingResize(
    VOID)
{
    if (!IsOriginMovingSizingEdge(g_SizingEdge))
      return;

    DwmFlush();
}

static
BOOL PFORCEINLINE APIPRIVATE
SwitchToRenderFiber(
    HWND hWnd,
    BOOL synchronous_resize)
{
    LPVOID fiber = (LPVOID)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    LARGE_INTEGER qpc_start;
    LARGE_INTEGER qpc_end;
    HWND previous_resize_hwnd;

    if (!fiber || fiber == GetCurrentFiber())
      return FALSE;

    if (synchronous_resize)
    {
      QueryPerformanceCounter(&qpc_start);
      previous_resize_hwnd = g_SynchronousResizeHwnd;
      g_SynchronousResizeHwnd = hWnd;
      g_SynchronousResizeRenderDepth++;
    }

    SwitchToFiber(fiber);

    if (synchronous_resize)
    {
      QueryPerformanceCounter(&qpc_end);
      g_LastResizeRenderTicks = qpc_end.QuadPart - qpc_start.QuadPart;
      g_SynchronousResizeRenderDepth--;
      g_SynchronousResizeHwnd = previous_resize_hwnd;
    }

    return TRUE;
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
    BOOL post_quit = (GetPropA(hWnd, OGLWINDOW_NO_QUIT_ON_DESTROY_PROP) == NULL);
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    RemovePropA(hWnd, OGLWINDOW_NO_QUIT_ON_DESTROY_PROP);

    if (pwglSurf)
    {
      if (pwglSurf->pbdc && pwglSurf->pbrc)
        wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc);
      if (pwglSurf->hpb && wglReleaseTexImageARB)
        wglReleaseTexImageARB(pwglSurf->hpb, WGL_FRONT_LEFT_ARB);
      if (wglGetCurrentContext() == pwglSurf->pbrc)
        wglMakeCurrent(NULL, NULL);
      if (pwglSurf->pbrc)
        wglDeleteContext(pwglSurf->pbrc);
      if (pwglSurf->hpb && pwglSurf->pbdc && wglReleasePbufferDCARB)
        wglReleasePbufferDCARB(pwglSurf->hpb, pwglSurf->pbdc);
      if (pwglSurf->hpb && wglDestroyPbufferARB)
        wglDestroyPbufferARB(pwglSurf->hpb);
      if (pwglSurf->uldc && pwglSurf->ulbmp_prev)
        SelectObject(pwglSurf->uldc, pwglSurf->ulbmp_prev);
      if (pwglSurf->ulbmp)
        DeleteObject(pwglSurf->ulbmp);
      if (pwglSurf->uldc)
        DeleteDC(pwglSurf->uldc);
      if (pwglSurf->pixels)
        HeapFree(GetProcessHeap(), 0, pwglSurf->pixels);
      SetWindowLongPtr(hWnd, 0, 0);
      HeapFree(GetProcessHeap(), 0, pwglSurf);
    }

    if (post_quit)
      PostQuitMessage(EXIT_SUCCESS);
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnPaint(
    HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    if (IsSecondaryViewportWindow(hWnd))
    {
      HWND previous_paint_hwnd = g_PaintHwnd;
      HDC previous_paint_hdc = g_PaintHdc;
      BOOL previous_paint_presented = g_PaintPresented;

      g_PaintHwnd = hWnd;
      g_PaintHdc = hdc;
      g_PaintPresented = FALSE;

      BOOL is_modal_sizing = IsOGLWindowInModalSizeMove(hWnd);
      (void)SwitchToRenderFiber(hWnd, is_modal_sizing);
      if (!g_PaintPresented && !is_modal_sizing)
        PresentSurfaceToDC(hWnd, hdc);
      FlushIfOriginMovingResize();

      g_PaintHwnd = previous_paint_hwnd;
      g_PaintHdc = previous_paint_hdc;
      g_PaintPresented = previous_paint_presented;
    }
    else if (!IsLayeredPresentWindow(hWnd) || !PresentSurfaceLayered(hWnd))
    {
      PresentSurfaceToDC(hWnd, hdc);
    }
    (void) EndPaint(hWnd, &ps);
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnPrintClient(
    HWND hWnd,
    HDC  hDC,
    UINT options)
{
    UNREFERENCED_PARAMETER(options);
    PresentSurfaceToDC(hWnd, hDC);
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnWindowPosChanged(
    HWND              hWnd,
    const LPWINDOWPOS lpwpos)
{
    if ((0 == (SWP_NOSIZE & lpwpos->flags)))
    {
      WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
      BOOL is_secondary = IsSecondaryViewportWindow(hWnd);

      if (pwglSurf && pwglSurf->rendered_during_windowpos)
      {
        pwglSurf->rendered_during_windowpos = FALSE;
        return;
      }

      if (is_secondary)
      {
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE | RDW_NOCHILDREN);
        FlushIfOriginMovingResize();
        return;
      }

      if (SwitchToRenderFiber(hWnd, TRUE))
      {
        if (is_secondary)
          FlushIfOriginMovingResize();
      }
    }
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnSize(
    HWND hWnd,
    UINT state,
    int  cx,
    int  cy)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    BOOL is_secondary = IsSecondaryViewportWindow(hWnd);

    UNREFERENCED_PARAMETER(state);
    UNREFERENCED_PARAMETER(cx);
    UNREFERENCED_PARAMETER(cy);

    if (pwglSurf)
      pwglSurf->rendered_during_windowpos = FALSE;

    if (is_secondary)
    {
      RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE | RDW_NOCHILDREN);
      if (pwglSurf && g_WindowPosChangedHwnd == hWnd)
        pwglSurf->rendered_during_windowpos = TRUE;
      FlushIfOriginMovingResize();
      return;
    }

    if (SwitchToRenderFiber(hWnd, TRUE))
    {
      if (pwglSurf && g_WindowPosChangedHwnd == hWnd)
        pwglSurf->rendered_during_windowpos = TRUE;
      if (is_secondary)
        FlushIfOriginMovingResize();
    }
}

static
BOOL PFORCEINLINE CALLBACK
OGLWindow_OnSizing(
    HWND  hWnd,
    UINT  edge,
    RECT* prc)
{
    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(prc);

    g_SizingEdge = edge;
    return TRUE;
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnWindowPosChanging(
    HWND        hWnd,
    LPWINDOWPOS lpwpos)
{
    UNREFERENCED_PARAMETER(hWnd);

    if (lpwpos && !(lpwpos->flags & SWP_NOSIZE))
      lpwpos->flags |= SWP_NOCOPYBITS;
}

static
BOOL PFORCEINLINE CALLBACK
OGLWindow_OnNCCreate(
    HWND            hWnd,
    LPCREATESTRUCT  lpCreateStruct)
{
    WGLSURFACE* pwglSurf;

    if (pwglSurf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WGLSURFACE)))
    {
      HDC hdc;

      if (!InitWGL())
      {
        HeapFree(GetProcessHeap(), 0, pwglSurf);
        return FALSE;
      }

      hdc = GetDC(hWnd);
      if (!CreateSurface(hdc, pwglSurf))
      {
        ReleaseDC(hWnd, hdc);
        HeapFree(GetProcessHeap(), 0, pwglSurf);
        return FALSE;
      }
      ReleaseDC(hWnd, hdc);

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

    /* Layered latch mode is fully client-area: the ULW sprite covers the
     * exact window rect, so keep client == window (no nonclient frame).
     * When maximized, inset by the invisible resize frame so the client does
     * not spill past the monitor work area. */
    if (IsLayeredPresentWindow(hWnd))
    {
      if (IsZoomed(hWnd))
      {
        int frame = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        InflateRect(&lpcsp->rgrc[0], -frame, -frame);
      }
      return 0;
    }

    if (!fCalcValidRects)
      return FORWARD_WM_NCCALCSIZE(hWnd, fCalcValidRects, lpcsp, DefWindowProc);

    lResult = DefWindowProc(hWnd, WM_NCCALCSIZE, TRUE, (LPARAM)lpcsp);
    if (lResult != 0)
      return (UINT)lResult;

    lpcsp->rgrc[1].right = lpcsp->rgrc[1].left;
    lpcsp->rgrc[1].bottom = lpcsp->rgrc[1].top;
    lpcsp->rgrc[2] = lpcsp->rgrc[1];
    return WVR_VALIDRECTS;
}

static
UINT PFORCEINLINE CALLBACK
OGLWindow_OnNCHitTest(
    HWND hWnd,
    int  x,
    int  y)
{
    LRESULT lResult;

    /* Layered mode has no nonclient area (client == window), so DefWindowProc
     * would report HTCLIENT everywhere; synthesize the resize borders. */
    if (IsLayeredPresentWindow(hWnd) && !IsZoomed(hWnd))
    {
      static const UINT hits[3][3] = {
        { HTTOPLEFT,    HTTOP,    HTTOPRIGHT    },
        { HTLEFT,       HTCLIENT, HTRIGHT      },
        { HTBOTTOMLEFT, HTBOTTOM, HTBOTTOMRIGHT },
      };
      RECT rc;
      int frame = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
      int row = 1;
      int col = 1;

      GetWindowRect(hWnd, &rc);
      if (y < rc.top + frame)
        row = 0;
      else if (y >= rc.bottom - frame)
        row = 2;
      if (x < rc.left + frame)
        col = 0;
      else if (x >= rc.right - frame)
        col = 2;

      return hits[row][col];
    }

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

    LPVOID fiber = (LPVOID)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (fiber)
      SwitchToFiber(fiber);

    DwmFlush();
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnEnterSizeMove(
    HWND hWnd)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    if (pwglSurf)
      pwglSurf->in_modal_size_move = TRUE;

    if (IsSecondaryViewportWindow(hWnd))
    {
      KillTimer(hWnd, 1);
      KillTimer(hWnd, 2);
    }
    else
    {
      SetTimer(hWnd, 1, USER_TIMER_MINIMUM, NULL);
      SetTimer(hWnd, 2, USER_TIMER_MINIMUM+1, NULL);
    }
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnExitSizeMove(
    HWND hWnd)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    g_SizingEdge = 0;
    KillTimer(hWnd, 1);
    KillTimer(hWnd, 2);
    if (pwglSurf)
      pwglSurf->in_modal_size_move = FALSE;
    
    //D3DKMT_WAITFORVERTICALBLANKEVENT* pVbe;
    //
    //pVbe = GetWindowLongPtr(hWnd, 0);
    //
    //if (!NT_SUCCESS(D3DKMTWaitForVerticalBlankEvent(pVbe)))
    //  __debugbreak();
    //
    LPVOID fiber = (LPVOID)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (fiber)
      SwitchToFiber(fiber);
}

static
VOID PFORCEINLINE CALLBACK
OGLWindow_OnTimer(
    HWND hWnd,
    UINT uId)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    if (pwglSurf && pwglSurf->in_modal_size_move && IsSecondaryViewportWindow(hWnd))
      return;

    //D3DKMT_WAITFORVERTICALBLANKEVENT* pVbe;
    //
    //pVbe = GetWindowLongPtr(hWnd, 0);
    //
    //if (!NT_SUCCESS(D3DKMTWaitForVerticalBlankEvent(pVbe)))
    //  __debugbreak();
    LPVOID fiber = (LPVOID)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (fiber)
      SwitchToFiber(fiber);
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

WINOGLWINDOWAPI BOOL WINAPI PresentOGLWindow(HWND hWnd)
{
    HDC hdc;
    BOOL presented;

    if (IsLayeredPresentWindow(hWnd))
    {
      presented = PresentSurfaceLayered(hWnd);
      if (presented)
      {
        if (g_PaintHwnd == hWnd)
          g_PaintPresented = TRUE;
        else
          ValidateRect(hWnd, NULL);
        return TRUE;
      }
      /* fall through to the DC blit when the layered present is unavailable
       * (e.g. window grew past the surface) */
    }

    if (g_PaintHwnd == hWnd && g_PaintHdc)
    {
      presented = PresentSurfaceToDC(hWnd, g_PaintHdc);
      if (presented)
        g_PaintPresented = TRUE;
      return presented;
    }

    hdc = GetDC(hWnd);
    presented = PresentSurfaceToDC(hWnd, hdc);

    if (hdc)
      ReleaseDC(hWnd, hdc);
    if (presented)
      ValidateRect(hWnd, NULL);

    return presented;
}

WINOGLWINDOWAPI BOOL WINAPI IsOGLWindowInSynchronousResizeRender(VOID)
{
    return g_SynchronousResizeRenderDepth > 0;
}

WINOGLWINDOWAPI HWND WINAPI GetOGLWindowSynchronousResizeHwnd(VOID)
{
    return g_SynchronousResizeHwnd;
}

WINOGLWINDOWAPI BOOL WINAPI IsOGLWindowInModalSizeMove(HWND hWnd)
{
    WGLSURFACE* pwglSurf = hWnd ? (WGLSURFACE*)GetWindowLongPtr(hWnd, 0) : NULL;
    return pwglSurf && pwglSurf->in_modal_size_move;
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
    case WM_PRINTCLIENT:
      OGLWindow_OnPrintClient(hWnd, (HDC)wParam, (UINT)lParam);
      return 0;
    HANDLE_MSG(hWnd, WM_SIZE, OGLWindow_OnSize);
    case WM_SIZING:
      return OGLWindow_OnSizing(hWnd, (UINT)wParam, (RECT*)lParam);
    case WM_WINDOWPOSCHANGING:
      OGLWindow_OnWindowPosChanging(hWnd, (LPWINDOWPOS)lParam);
      return DefWindowProc(hWnd, uMsg, wParam, lParam);
    case WM_NCPAINT:
    case WM_NCACTIVATE:
      /* In layered present mode the ULW sprite is the only content source;
       * DefWindowProc would paint the classic (non-DWM) frame into the layer
       * and fight every UpdateLayeredWindowIndirect. */
      if (IsLayeredPresentWindow(hWnd))
        return (uMsg == WM_NCACTIVATE) ? TRUE : 0;
      return DefWindowProc(hWnd, uMsg, wParam, lParam);
    case WM_SETTEXT:
    case WM_SETICON:
      /* Same classic-frame repaint hazard: let DefWindowProc store the text /
       * icon but hide the window from its redraw pass while it does. */
      if (IsLayeredPresentWindow(hWnd))
      {
        LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
        LRESULT lResult;

        SetWindowLongPtr(hWnd, GWL_STYLE, style & ~WS_VISIBLE);
        lResult = DefWindowProc(hWnd, uMsg, wParam, lParam);
        SetWindowLongPtr(hWnd, GWL_STYLE, style);
        return lResult;
      }
      return DefWindowProc(hWnd, uMsg, wParam, lParam);
    case WM_WINDOWPOSCHANGED:
    {
      HWND previous_windowpos_hwnd = g_WindowPosChangedHwnd;
      LRESULT result;
      g_WindowPosChangedHwnd = hWnd;
      result = DefWindowProc(hWnd, uMsg, wParam, lParam);
      g_WindowPosChangedHwnd = previous_windowpos_hwnd;
      OGLWindow_OnWindowPosChanged(hWnd, (const LPWINDOWPOS)lParam);
      return result;
    }
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
    wcx.hbrBackground = NULL;
    wcx.lpszMenuName  = NULL;
    wcx.lpszClassName = OGLWINDOW_CLASS;

    return RegisterClassEx(&wcx);
}
