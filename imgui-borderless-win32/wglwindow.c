#include "wglwindow.h"
#include "dwmframe.h"
#include "dxgipresent.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <GL/gl.h>

#pragma comment (lib, "dwmapi")
#pragma comment (lib, "uxtheme")
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

/* BOOL Cls_OnSizing(HWND hwnd, UINT edge, RECT *prc) */
#define HANDLE_WM_SIZING(hwnd, wParam, lParam, fn) \
        ((LRESULT)(DWORD)(BOOL)(fn)((hwnd), (UINT)(wParam), (RECT*)(lParam)))

/* void Cls_OnPrintClient(HWND hwnd, HDC hDC, UINT options) */
#define HANDLE_WM_PRINTCLIENT(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd), (HDC)(wParam), (UINT)(lParam)), 0L)

/* LRESULT Cls_OnSetText(HWND hwnd, LPCTSTR lpszText) */
#undef HANDLE_WM_SETTEXT
#define HANDLE_WM_SETTEXT(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd), (LPCTSTR)(lParam)))

/* LRESULT Cls_OnSetIcon(HWND hwnd, UINT fType, HICON hicon) */
#undef HANDLE_WM_SETICON
#define HANDLE_WM_SETICON(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd), (UINT)(wParam), (HICON)(lParam)))

/* void Cls_OnNCMouseLeave(HWND hwnd) */
#define HANDLE_WM_NCMOUSELEAVE(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd)), 0L)

/* BOOL Cls_OnMoving(HWND hwnd, RECT *prc) */
#define HANDLE_WM_MOVING(hwnd, wParam, lParam, fn) \
        ((LRESULT)(DWORD)(BOOL)(fn)((hwnd), (RECT*)(lParam)))

/* void Cls_OnDxgiPace(HWND hwnd) -- HANDLE_MSG token-pastes the message
 * name, so the cracker carries the full DXGIPRESENT_WM_PACE identifier. */
#define HANDLE_DXGIPRESENT_WM_PACE(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd)), 0L)

/* void Cls_OnDpiChanged(HWND hwnd, UINT dpiX, UINT dpiY, RECT *prcSuggested) */
#define HANDLE_WM_DPICHANGED(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd), (UINT)LOWORD(wParam), (UINT)HIWORD(wParam), (RECT*)(lParam)), 0L)

/* void Cls_OnDwmNCRenderingChanged(HWND hwnd, BOOL fEnabled) */
#define HANDLE_WM_DWMNCRENDERINGCHANGED(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd), (BOOL)(wParam)), 0L)

/* DWM window attributes as plain values (the C enum tag is not typedef'd in
 * every dwmapi.h vintage; DwmSetWindowAttribute takes a DWORD). */
#define DWMWA_ALLOW_NCPAINT_VALUE       (4)
#define DWMWA_PASSIVE_UPDATE_MODE_VALUE (16)

/* void Cls_OnCaptureChanged(HWND hwnd, HWND hwndNewCapture) */
#define HANDLE_WM_CAPTURECHANGED(hwnd, wParam, lParam, fn) \
        ((fn)((hwnd), (HWND)(lParam)), 0L)

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
/* First WGLWindow pbuffer context: share-group root (see CreateSurface). */
static HGLRC pbuff_share_root;

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
#define WGLWINDOW_SURFACE_WIDTH  2560
#define WGLWINDOW_SURFACE_HEIGHT 1600

/* Cross-window state lives in the window class's extra bytes (cbClsExtra
 * slot 0 -> heap WGLCLASSSTATE), never in file-scope variables; per-window
 * state lives in the WGLSURFACE reached via GetWindowLongPtr(hWnd, 0). */
typedef struct _WGLCLASSSTATE
{
  HWND sync_resize_hwnd;
  LONG sync_resize_depth;

} WGLCLASSSTATE, *PWGLCLASSSTATE;

static
PWGLCLASSSTATE PFORCEINLINE APIPRIVATE
GetWGLClassState(
    HWND hWnd)
{
    WGLCLASSSTATE* state = (WGLCLASSSTATE*)GetClassLongPtr(hWnd, 0);

    if (!state)
    {
      state = (WGLCLASSSTATE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
      if (state)
        SetClassLongPtr(hWnd, 0, (LONG_PTR)state);
    }

    return state;
}

/****** Private API **********************************************************/

static 
ATOM PFORCEINLINE APIPRIVATE
RegisterWGLWindowClass(
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
RepaintNow(
    HWND hWnd
    );

/****** Message Handlers *****************************************************/

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnDestroy(
    HWND hWnd
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnPaint(
    HWND hWnd
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnPrintClient(
    HWND hWnd,
    HDC  hDC,
    UINT options
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnWindowPosChanged(
    HWND              hWnd,
    const LPWINDOWPOS lpwpos
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnSize(
    HWND hWnd,
    UINT state,
    int  cx,
    int  cy
    );

static
BOOL PFORCEINLINE CALLBACK
WGLWindow_OnSizing(
    HWND  hWnd,
    UINT  edge,
    RECT* prc
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnNCPaint(
    HWND hWnd,
    HRGN hrgn
    );

static
BOOL PFORCEINLINE CALLBACK
WGLWindow_OnNCActivate(
    HWND hWnd,
    BOOL fActive,
    HWND hwndActDeact,
    BOOL fMinimized
    );

static
LRESULT PFORCEINLINE CALLBACK
WGLWindow_OnSetText(
    HWND    hWnd,
    LPCTSTR lpszText
    );

static
LRESULT PFORCEINLINE CALLBACK
WGLWindow_OnSetIcon(
    HWND  hWnd,
    UINT  fType,
    HICON hicon
    );

static
BOOL PFORCEINLINE CALLBACK
WGLWindow_OnNCCreate(
    HWND            hWnd,
    LPCREATESTRUCT  lpCreateStruct
    );

static
UINT PFORCEINLINE CALLBACK
WGLWindow_OnNCCalcSize(
    HWND               hWnd,
    BOOL               fCalcValidRects,
    NCCALCSIZE_PARAMS* lpcsp
    );

static
UINT PFORCEINLINE CALLBACK
WGLWindow_OnNCHitTest(
    HWND hWnd,
    int  x,
    int  y
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnGetMinMaxInfo(
    HWND          hWnd,
    LPMINMAXINFO  lpMinMaxInfo
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnActivate(
    HWND hWnd,
    UINT state,
    HWND hwndActDeact,
    BOOL fMinimized
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnNCMouseMove(
    HWND hWnd,
    int  x,
    int  y,
    UINT codeHitTest
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnNCMouseLeave(
    HWND hWnd
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnNCLButtonDown(
    HWND hWnd,
    BOOL fDoubleClick,
    int  x,
    int  y,
    UINT codeHitTest
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnMouseMove(
    HWND hWnd,
    int  x,
    int  y,
    UINT keyFlags
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnLButtonUp(
    HWND hWnd,
    int  x,
    int  y,
    UINT keyFlags
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnCaptureChanged(
    HWND hWnd,
    HWND hwndNewCapture
    );

static
BOOL PFORCEINLINE CALLBACK
WGLWindow_OnEraseBkgnd(
    HWND hWnd,
    HDC  hDC
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnEnterMenuLoop(
    HWND hWnd,
    BOOL fIsTrackPopupMenu
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnExitMenuLoop(
    HWND hWnd,
    BOOL fIsShortcutMenu
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnEnterSizeMove(
    HWND hWnd
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnExitSizeMove(
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
    HWND hWnd
    );

static
BOOL PFORCEINLINE APIPRIVATE
SwitchToRenderFiber(
    HWND hWnd,
    BOOL synchronous_resize
    );

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnTimer(
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

    /* Cover the primary monitor so a maximized/fullscreen window never
     * stretch-blits; the constants are the floor. */
    pwglSurf->width = max(WGLWINDOW_SURFACE_WIDTH, GetSystemMetrics(SM_CXSCREEN));
    pwglSurf->height = max(WGLWINDOW_SURFACE_HEIGHT, GetSystemMetrics(SM_CYSCREEN));
    pwglSurf->hpb = wglCreatePbufferARB(dc, formats[0], pwglSurf->width, pwglSurf->height, pattribs);
    if (!pwglSurf->hpb)
      return FALSE;

    pwglSurf->pbdc = wglGetPbufferDCARB(pwglSurf->hpb);
    if (!pwglSurf->pbdc)
      return FALSE;

    pwglSurf->pbrc = wglCreateContext(pwglSurf->pbdc);
    if (!pwglSurf->pbrc)
      return FALSE;

    /* One share group for every WGLWindow context, established at BIRTH —
     * before the presenter's GL⇄DX interop registration puts objects into
     * the context (wglShareLists fails on a context that already owns
     * objects; sharing is the window control's job, not the app's).  The
     * first context is the root; the group outlives it (share groups are
     * refcounted by member contexts). */
    if (!pbuff_share_root)
      pbuff_share_root = pwglSurf->pbrc;
    else if (!wglShareLists(pbuff_share_root, pwglSurf->pbrc))
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

/* Grows the CPU pixel buffer to hold cx*cy BGRA pixels. */
static
BOOL PFORCEINLINE APIPRIVATE
EnsurePixelCapacity(
    WGLSURFACE* pwglSurf,
    int         cx,
    int         cy)
{
    SIZE_T pixel_count = (SIZE_T)cx * (SIZE_T)cy * 4u;

    if (pixel_count <= pwglSurf->pixels_capacity)
      return TRUE;

    {
      BYTE* next_pixels = pwglSurf->pixels
        ? (BYTE*)HeapReAlloc(GetProcessHeap(), 0, pwglSurf->pixels, pixel_count)
        : (BYTE*)HeapAlloc(GetProcessHeap(), 0, pixel_count);
      if (!next_pixels)
        return FALSE;

      pwglSurf->pixels = next_pixels;
      pwglSurf->pixels_capacity = pixel_count;
    }

    return TRUE;
}

/* glReadPixels of the pbuffer's bottom-left cx*cy rect into pixels[]. */
static
BOOL PFORCEINLINE APIPRIVATE
ReadbackSurface(
    WGLSURFACE* pwglSurf,
    int         cx,
    int         cy)
{
    if (!EnsurePixelCapacity(pwglSurf, cx, cy))
      return FALSE;
    if (!wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc))
      return FALSE;

    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, cx, cy, GL_BGRA, GL_UNSIGNED_BYTE, pwglSurf->pixels);
    return TRUE;
}

/* 1:1 blit of pixels[] (bottom-up BGRA) to the DC.  This is the only work
 * that may run in the commit-critical window: sub-millisecond, GDI only. */
static
VOID PFORCEINLINE APIPRIVATE
BlitPixelsToDC(
    WGLSURFACE* pwglSurf,
    HDC         hdc,
    int         cx,
    int         cy,
    int         dst_cx,
    int         dst_cy)
{
    BITMAPINFOHEADER bmih = { sizeof(bmih), cx, cy, 1, 32, BI_RGB };

    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, 0, 0, dst_cx, dst_cy, 0, 0, cx, cy, pwglSurf->pixels, (const BITMAPINFO*)&bmih, DIB_RGB_COLORS, SRCCOPY);
    GdiFlush();
}

static
BOOL PFORCEINLINE APIPRIVATE
PresentSurfaceToDC(
    HWND hWnd,
    HDC  hdc)
{
    RECT rc;
    SIZE sz;
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

    QueryPerformanceCounter(&qpc_start);
    if (!ReadbackSurface(pwglSurf, sz.cx, sz.cy))
      return FALSE;

    BlitPixelsToDC(pwglSurf, hdc, sz.cx, sz.cy, RECTWIDTH(rc), RECTHEIGHT(rc));
    QueryPerformanceCounter(&qpc_end);
    pwglSurf->last_present_ticks = qpc_end.QuadPart - qpc_start.QuadPart;

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
    if (IsWGLWindowInSynchronousResizeRender(hWnd) || IsWGLWindowInModalSizeMove(hWnd))
      ulwi.dwFlags |= ULW_EX_NORESIZE;

    if (!UpdateLayeredWindowIndirect(hWnd, &ulwi))
      return FALSE;

    QueryPerformanceCounter(&qpc_end);
    pwglSurf->last_present_ticks = qpc_end.QuadPart - qpc_start.QuadPart;

    return TRUE;
}

/* Synchronous full repaint through the real paint path: invalidate
 * everything, then dispatch WM_PAINT before returning.  Used for windows
 * whose geometry changes programmatically mid-frame (secondary viewports),
 * where waiting for the message pump would present stale content. */
static
VOID PFORCEINLINE APIPRIVATE
RepaintNow(
    HWND hWnd)
{
    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE | RDW_NOCHILDREN);
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
    return GetPropA(hWnd, WGLWINDOW_SECONDARY_VIEWPORT_PROP) != NULL;
}

typedef DWMFRAME* PDWMFRAME;

static
PDWMFRAME PFORCEINLINE APIPRIVATE
GetDwmFrame(
    HWND hWnd)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    return pwglSurf ? pwglSurf->frame : NULL;
}

static
BOOL PFORCEINLINE APIPRIVATE
IsDXGIPresentWindow(
    HWND hWnd)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    return pwglSurf && pwglSurf->dxgi != NULL;
}

/* R4: TRUE when the last present already produced content for the upcoming
 * compositor frame at the window's CURRENT client size. */
static
BOOL PFORCEINLINE APIPRIVATE
ModalContentCurrentWGL(
    HWND hWnd)
{
    RECT rc;
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    if (!pwglSurf || !pwglSurf->dxgi)
      return FALSE;
    GetClientRect(hWnd, &rc);
    return DxgiPresent_ContentCurrent(pwglSurf->dxgi, RECTWIDTH(rc), RECTHEIGHT(rc));
}

/* WndProc-driven repaint (reference ModalRepaint): one full app frame with
 * the present flavor pinned to the caller's (fRestart, fVsync) pair.  The
 * re-entrancy latches keep repaint paths from nesting and from re-entering
 * a frame already on the stack (viewport churn sends WM_WINDOWPOSCHANGED /
 * WM_ACTIVATE here synchronously mid-frame). */
static
VOID PFORCEINLINE APIPRIVATE
ModalRepaintWGL(
    HWND hWnd,
    BOOL fRestart,
    BOOL fVsync,
    BOOL fWaitForVBlank)
{
    SIZE sz;
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    if (!pwglSurf || !pwglSurf->dxgi || pwglSurf->in_modal_repaint || pwglSurf->in_frame)
      return;

    pwglSurf->in_modal_repaint = TRUE;
    if (fWaitForVBlank)
      DxgiPresent_WaitForVBlank(pwglSurf->dxgi, hWnd);

    /* Stamp the compositor frame this repaint targets + the size it draws —
     * the DRIVEN size: the pending rgrc[0] during the WM_NCCALCSIZE
     * pending-rect repaint, the live client otherwise. */
    if (GetWGLWindowDrivenClientSize(hWnd, &sz))
      DxgiPresent_StampLatch(pwglSurf->dxgi, (int)sz.cx, (int)sz.cy);

    pwglSurf->modal_present_active = TRUE;
    pwglSurf->modal_restart = fRestart;
    pwglSurf->modal_vsync = fVsync;
    (void)SwitchToRenderFiber(hWnd, TRUE);
    pwglSurf->modal_present_active = FALSE;
    pwglSurf->in_modal_repaint = FALSE;
}

static
VOID PFORCEINLINE APIPRIVATE
FlushIfOriginMovingResize(
    HWND hWnd)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    if (!pwglSurf || !IsOriginMovingSizingEdge(pwglSurf->sizing_edge))
      return;

    /* Origin-moving edges: pace the modal loop to composition so at most one
     * geometry+content pair is in flight per compositor frame. */
    DwmFlush();
}

static
BOOL PFORCEINLINE APIPRIVATE
SwitchToRenderFiber(
    HWND hWnd,
    BOOL synchronous_resize)
{
    LPVOID fiber = (LPVOID)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    WGLCLASSSTATE* state = GetWGLClassState(hWnd);
    LARGE_INTEGER qpc_start;
    LARGE_INTEGER qpc_end;
    HWND previous_resize_hwnd;

    if (!fiber || fiber == GetCurrentFiber() || !state)
      return FALSE;

    if (synchronous_resize)
    {
      QueryPerformanceCounter(&qpc_start);
      previous_resize_hwnd = state->sync_resize_hwnd;
      state->sync_resize_hwnd = hWnd;
      state->sync_resize_depth++;
    }

    SwitchToFiber(fiber);

    if (synchronous_resize)
    {
      QueryPerformanceCounter(&qpc_end);
      if (pwglSurf)
        pwglSurf->last_resize_render_ticks = qpc_end.QuadPart - qpc_start.QuadPart;
      state->sync_resize_depth--;
      state->sync_resize_hwnd = previous_resize_hwnd;
    }

    return TRUE;
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnDestroy(
    HWND hWnd)
{
    BOOL post_quit = (GetPropA(hWnd, WGLWINDOW_NO_QUIT_ON_DESTROY_PROP) == NULL);
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    RemovePropA(hWnd, WGLWINDOW_NO_QUIT_ON_DESTROY_PROP);

    if (pwglSurf)
    {
      /* GL context current first: the frame's cached chrome textures and the
       * presenter's interop teardown both need it. */
      if (pwglSurf->pbdc && pwglSurf->pbrc)
        wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc);
      if (pwglSurf->frame)
      {
        DwmFrameDestroy(pwglSurf->frame);
        pwglSurf->frame = NULL;
      }
      if (pwglSurf->dxgi)
      {
        DxgiPresent_Destroy(pwglSurf->dxgi);
        pwglSurf->dxgi = NULL;
      }
      if (pwglSurf->hpb && wglReleaseTexImageARB)
        wglReleaseTexImageARB(pwglSurf->hpb, WGL_FRONT_LEFT_ARB);
      if (wglGetCurrentContext() == pwglSurf->pbrc)
        wglMakeCurrent(NULL, NULL);
      if (pwglSurf->pbrc)
      {
        if (pwglSurf->pbrc == pbuff_share_root)
          pbuff_share_root = NULL;
        wglDeleteContext(pwglSurf->pbrc);
      }
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
WGLWindow_OnPaint(
    HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc;
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    if (pwglSurf && pwglSurf->dxgi)
    {
      /* WS_EX_NOREDIRECTIONBITMAP: there is nothing to paint — the DComp
       * visual is the only content source, and the run loop / modal repaint
       * paths own the presents (painting here would inject out-of-band
       * frames between theirs; reference behavior). */
      ValidateRect(hWnd, NULL);
      return;
    }

    hdc = BeginPaint(hWnd, &ps);
    if (IsLayeredPresentWindow(hWnd))
    {
      if (!PresentSurfaceLayered(hWnd))
        PresentSurfaceToDC(hWnd, hdc);
    }
    else if (pwglSurf)
    {
      /* Redirection-surface present (TheScratchProgram model): render a full
       * frame and blit it between BeginPaint/EndPaint.  During a live resize
       * this runs synchronously from the resize tick (RepaintNow), so the
       * update region is validated with content at the NEW size before the
       * SetWindowPos transaction completes — DWM's resize hold then releases
       * the new geometry together with the fresh content, atomically. */
      HDC previous_paint_hdc = pwglSurf->paint_hdc;
      BOOL previous_paint_presented = pwglSurf->paint_presented;

      pwglSurf->paint_hdc = hdc;
      pwglSurf->paint_presented = FALSE;

      BOOL is_modal_sizing = IsWGLWindowInModalSizeMove(hWnd);
      (void)SwitchToRenderFiber(hWnd, is_modal_sizing);
      if (!pwglSurf->paint_presented && !is_modal_sizing)
        PresentSurfaceToDC(hWnd, hdc);
      if (IsSecondaryViewportWindow(hWnd))
        FlushIfOriginMovingResize(hWnd);

      pwglSurf->paint_hdc = previous_paint_hdc;
      pwglSurf->paint_presented = previous_paint_presented;
    }
    else
    {
      PresentSurfaceToDC(hWnd, hdc);
    }
    (void) EndPaint(hWnd, &ps);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnPrintClient(
    HWND hWnd,
    HDC  hDC,
    UINT options)
{
    UNREFERENCED_PARAMETER(options);
    PresentSurfaceToDC(hWnd, hDC);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnWindowPosChanged(
    HWND              hWnd,
    const LPWINDOWPOS lpwpos)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    BOOL previous_in_windowpos = pwglSurf ? pwglSurf->in_windowpos_changed : FALSE;

    /* Composition-swapchain path (reference, NOT forwarded — no WM_SIZE /
     * WM_MOVE generation, no waits: the geometry already committed).  Order
     * is the flicker guarantee:
     *   1. R6 self-heal: if this change RESIZED the window, pin the
     *      presented content at its render-time screen position (pure moves
     *      carry content along and are not pinned);
     *   2. repaint at the new size, (1, 1), coalesced to one rendered
     *      present per compositor frame (R4); its present unpins the content
     *      in the same compositor latch.
     * Pure z-order/activation churn changes no geometry: skip entirely. */
    if (pwglSurf && pwglSurf->dxgi)
    {
      RECT rc;

      if (lpwpos && (lpwpos->flags & SWP_NOMOVE) && (lpwpos->flags & SWP_NOSIZE))
        return;
      if (pwglSurf->in_modal_repaint)
        return;

      GetClientRect(hWnd, &rc);
      if (DxgiPresent_SizeChanged(pwglSurf->dxgi, RECTWIDTH(rc), RECTHEIGHT(rc)))
        DxgiPresent_PinContent(pwglSurf->dxgi, hWnd);
      if (!ModalContentCurrentWGL(hWnd))
        ModalRepaintWGL(hWnd, TRUE, TRUE, FALSE);
      return;
    }

    if (pwglSurf)
      pwglSurf->in_windowpos_changed = TRUE;
    FORWARD_WM_WINDOWPOSCHANGED(hWnd, lpwpos, DefWindowProc);
    if (pwglSurf)
      pwglSurf->in_windowpos_changed = previous_in_windowpos;

    if ((0 == (SWP_NOSIZE & lpwpos->flags)))
    {
      /* WM_SIZE (generated by the forward above) normally handles it; this
       * is the fallback for size changes that arrive without one. */
      if (pwglSurf && pwglSurf->rendered_during_windowpos)
      {
        pwglSurf->rendered_during_windowpos = FALSE;
        return;
      }

      if (IsLayeredPresentWindow(hWnd))
      {
        if (SwitchToRenderFiber(hWnd, TRUE))
          FlushIfOriginMovingResize(hWnd);
        return;
      }

      if (IsSecondaryViewportWindow(hWnd))
      {
        RepaintNow(hWnd);
        FlushIfOriginMovingResize(hWnd);
        return;
      }

      InvalidateRect(hWnd, NULL, FALSE);
    }
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnSize(
    HWND hWnd,
    UINT state,
    int  cx,
    int  cy)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    UNREFERENCED_PARAMETER(state);
    UNREFERENCED_PARAMETER(cx);
    UNREFERENCED_PARAMETER(cy);

    if (pwglSurf)
      pwglSurf->rendered_during_windowpos = FALSE;

    if (IsLayeredPresentWindow(hWnd))
    {
      if (SwitchToRenderFiber(hWnd, TRUE))
      {
        if (pwglSurf && pwglSurf->in_windowpos_changed)
          pwglSurf->rendered_during_windowpos = TRUE;
        FlushIfOriginMovingResize(hWnd);
      }
      return;
    }

    if (IsSecondaryViewportWindow(hWnd))
    {
      /* imgui resizes these programmatically mid-frame; the paint must land
       * before the SetWindowPos transaction returns to the frame. */
      RepaintNow(hWnd);
      if (pwglSurf && pwglSurf->in_windowpos_changed)
        pwglSurf->rendered_during_windowpos = TRUE;
      FlushIfOriginMovingResize(hWnd);
      return;
    }

    /* TheScratchProgram shape: invalidate; the message pump delivers WM_PAINT
     * and the full frame renders inside BeginPaint/EndPaint. */
    InvalidateRect(hWnd, NULL, FALSE);
}

static
BOOL PFORCEINLINE CALLBACK
WGLWindow_OnSizing(
    HWND  hWnd,
    UINT  edge,
    RECT* prc)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    if (pwglSurf)
      pwglSurf->sizing_edge = edge;
    /* Canon lets DefWindowProc see WM_SIZING (drag-rect min/max enforcement). */
    return (BOOL)DefWindowProc(hWnd, WM_SIZING, (WPARAM)edge, (LPARAM)prc);
}

static
BOOL PFORCEINLINE CALLBACK
WGLWindow_OnMoving(
    HWND  hWnd,
    RECT* prc)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    UNREFERENCED_PARAMETER(prc);

    /* Pure move loop: the visual travels with the window, so the
     * WM_NCCALCSIZE repaint path must stay quiet. */
    if (pwglSurf)
      pwglSurf->moving = TRUE;
    return TRUE;
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnDwmNCRenderingChanged(
    HWND hWnd,
    BOOL fEnabled)
{
    /* DWM re-enables its NC rendering across composition restarts; re-assert
     * the reference attributes (imguiapp WM_DWMNCRENDERINGCHANGED). */
    if (fEnabled && IsDXGIPresentWindow(hWnd))
    {
      BOOL allow_ncpaint  = FALSE;
      BOOL passive_update = TRUE;
      DwmSetWindowAttribute(hWnd, DWMWA_ALLOW_NCPAINT_VALUE, &allow_ncpaint, sizeof(allow_ncpaint));
      DwmSetWindowAttribute(hWnd, DWMWA_PASSIVE_UPDATE_MODE_VALUE, &passive_update, sizeof(passive_update));
    }
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnDpiChanged(
    HWND  hWnd,
    UINT  dpiX,
    UINT  dpiY,
    RECT* prcSuggested)
{
    UNREFERENCED_PARAMETER(dpiX);
    UNREFERENCED_PARAMETER(dpiY);

    /* Snap to the system's suggested rect (reference behavior); every
     * dpi-scaled metric re-derives from the window's new dpi next frame. */
    if (prcSuggested)
      SetWindowPos(hWnd, NULL, prcSuggested->left, prcSuggested->top,
                   prcSuggested->right - prcSuggested->left,
                   prcSuggested->bottom - prcSuggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnSysCommand(
    HWND hWnd,
    UINT cmd,
    int  x,
    int  y)
{
    if ((cmd & 0xFFF0) == SC_KEYMENU && IsDXGIPresentWindow(hWnd))
    {
      /* Alt+Space (char code in x): the system menu, tracked ourselves at
       * the caption anchor — DefWindowProc's placement assumes a standard
       * NC caption.  Bare ALT stays swallowed (no classic menu bar). */
      DWMFRAME* frame = GetDwmFrame(hWnd);
      if (frame && ' ' == x)
        DwmFrameShowSystemMenu(frame, hWnd, -1, -1);
      return;
    }

    FORWARD_WM_SYSCOMMAND(hWnd, cmd, x, y, DefWindowProc);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnDxgiPace(
    HWND hWnd)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    /* Refresh-rate modal tick from the pace thread (R5).  The thread already
     * supplied the vblank phase, so the repaint runs waitless; R4 coalescing
     * dedupes against the resize-tick repaints. */
    if (pwglSurf && pwglSurf->dxgi)
    {
      DxgiPresent_PaceTickHandled(pwglSurf->dxgi);
      if (!ModalContentCurrentWGL(hWnd))
        ModalRepaintWGL(hWnd, FALSE, TRUE, FALSE);
    }
}

static
BOOL PFORCEINLINE CALLBACK
WGLWindow_OnNCCreate(
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

      SetLastError(NO_ERROR);

      LONG_PTR prev_value = SetWindowLongPtr(hWnd, 0, (LONG_PTR)pwglSurf);
      if (NO_ERROR != GetLastError())
        __debugbreak();

      SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lpCreateStruct->lpCreateParams);

      /* WS_EX_NOREDIRECTIONBITMAP: no GDI surface exists — presentation goes
       * through the composition-swapchain presenter (GL fill + D2D chrome +
       * the two-step present ladder; imguiapp_impl_win32_d2ddxgi model).
       * CreateSurface left the pbuffer GL context current, which the
       * presenter's interop registration requires. */
      if (GetWindowLongPtr(hWnd, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP)
      {
        /* Reference WM_NCCREATE lines: forbid DWM's own nonclient rendering
         * — with the frame extended and the hit test reporting caption
         * buttons, DWM otherwise draws ITS caption band, buttons, and frame
         * ring over the composed chrome. */
        BOOL allow_ncpaint = FALSE;
        EnableNonClientDpiScaling(hWnd);
        DwmSetWindowAttribute(hWnd, DWMWA_ALLOW_NCPAINT_VALUE, &allow_ncpaint, sizeof(allow_ncpaint));

        /* Dissociate the window from uxtheme.  DWM nonclient rendering goes
         * OFF for this window once the composed content attaches
         * (WM_DWMNCRENDERINGCHANGED), and uxtheme treats a THEMED
         * WS_CAPTION window without DWM NC rendering as a classic-framed
         * caption window: its DefWindowProc hooks re-apply the Luna
         * rounded-top caption region (top corners round, bottom square,
         * region in unscaled NC coords) behind virtually every forwarded
         * message — unbeatable by message-level SetWindowRgn(NULL) games.
         * With no theme association the hook has no theme data and never
         * computes a region (measured, ncrtest 2026-07-12).  The window
         * draws all of its own UI, so it loses nothing. */
        SetWindowTheme(hWnd, L" ", L" ");

        pwglSurf->dxgi = DxgiPresent_Create(hWnd);
        if (!pwglSurf->dxgi)
        {
          SetWindowLongPtr(hWnd, 0, 0);
          HeapFree(GetProcessHeap(), 0, pwglSurf);
          return FALSE;
        }
        /* Composed caption chrome only for CAPTION windows: undecorated
         * secondary viewports (WS_POPUP; imgui draws their dressing) get
         * the presenter but no chrome. */
        if (GetWindowLongPtr(hWnd, GWL_STYLE) & WS_CAPTION)
          pwglSurf->frame = DwmFrameCreate(hWnd);
      }
    }

    return FORWARD_WM_NCCREATE(hWnd, lpCreateStruct, DefWindowProc);
}

static
UINT PFORCEINLINE CALLBACK
WGLWindow_OnNCCalcSize(
    HWND               hWnd,
    BOOL               fCalcValidRects,
    NCCALCSIZE_PARAMS* lpcsp)
{
    /* dwmframe windows.  Gated on the ex-style so the very first
     * WM_NCCALCSIZE (before WM_NCCREATE allocates state) already strips the
     * frame — else the system caption ghosts under the composited one at
     * startup. */
    if (GetWindowLongPtr(hWnd, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP)
    {
      UINT uResult = DwmFrameNCCalcSize(hWnd, fCalcValidRects, lpcsp);
      WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

      /* R1 + R2 (dxgi-noflicker §4), pending-rect variant (ImmersiveWindow:
       * "paint the incoming rgrc[0]").  This message runs inside the
       * SetWindowPos transaction, BEFORE the window manager commits the new
       * rect.  R1: render a full frame AT THE SIZE JUST DICTATED in rgrc[0]
       * and run the (1, 0) ladder — content for the tick queues before the
       * geometry that accompanies it, ALREADY AT THE NEW SIZE.  R2: Commit +
       * WaitForCommitCompletion before returning — a compositor frame start
       * has then passed that latched the R1 present, so the geometry commit
       * can never overtake its content.  When geometry lands, the latched
       * content already matches it: the post-geometry WM_WINDOWPOSCHANGED
       * correction is skipped by R4 (size stamp equal) and R6 stays unarmed
       * (content size equals the new client) — there is NO post-geometry
       * render whose latency could expose a mismatched frame.  This is the
       * canon's protocol made latency-robust: imguiapp renders the CURRENT
       * size here only because its ~1ms D2D frame lets the R3 correction
       * beat the next latch; this GL frame cannot, so the fresh content must
       * be the pre-geometry present itself.
       * Pure move loops skip (the visual travels with the window); zoomed
       * passes skip (canon's zoomed branch returns before this leg). */
      if (pwglSurf && pwglSurf->dxgi && fCalcValidRects && !pwglSurf->moving && !IsZoomed(hWnd))
      {
        GUITHREADINFO gti = { sizeof(gti) };
        int cx = lpcsp->rgrc[0].right - lpcsp->rgrc[0].left;
        int cy = lpcsp->rgrc[0].bottom - lpcsp->rgrc[0].top;

        if (cx < 1) cx = 1;
        if (cy < 1) cy = 1;
        if (!DxgiPresent_ContentCurrent(pwglSurf->dxgi, cx, cy))
        {
          pwglSurf->pending_cx = cx;
          pwglSurf->pending_cy = cy;
          ModalRepaintWGL(hWnd, TRUE, FALSE, TRUE);
          pwglSurf->pending_cx = 0;
          pwglSurf->pending_cy = 0;
        }
        if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndMoveSize != NULL)
          DxgiPresent_WaitForCommit(pwglSurf->dxgi);
      }
      return uResult;
    }

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

    /* TheScratchProgram shape: no NCCALCSIZE games — DefWindowProc's stock
     * behavior (standard frame, standard bit preservation). */
    return FORWARD_WM_NCCALCSIZE(hWnd, fCalcValidRects, lpcsp, DefWindowProc);
}

static
UINT PFORCEINLINE CALLBACK
WGLWindow_OnNCHitTest(
    HWND hWnd,
    int  x,
    int  y)
{
    LRESULT lResult;
    DWMFRAME* frame = GetDwmFrame(hWnd);

    if (frame)
      return DwmFrameHitTest(frame, hWnd, x, y);

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
VOID PFORCEINLINE CALLBACK
WGLWindow_OnNCPaint(
    HWND hWnd,
    HRGN hrgn)
{
    if (IsDXGIPresentWindow(hWnd))
    {
      /* Nothing to paint: the DComp visual carries chrome + client; the
       * classic frame DefWindowProc would draw fights every present. */
      ValidateRgn(hWnd, (hrgn != (HRGN)1) ? hrgn : NULL);
      return;
    }

    /* Layered present mode: the ULW sprite is the only content source; the
     * classic frame DefWindowProc paints would fight every sprite update. */
    if (IsLayeredPresentWindow(hWnd))
      return;

    FORWARD_WM_NCPAINT(hWnd, hrgn, DefWindowProc);
}

static
BOOL PFORCEINLINE CALLBACK
WGLWindow_OnNCActivate(
    HWND hWnd,
    BOOL fActive,
    HWND hwndActDeact,
    BOOL fMinimized)
{
    DWMFRAME* frame = GetDwmFrame(hWnd);

    if (frame)
    {
      /* Activation crossfade, then DefWindowProc with lParam -1: update the
       * window's activation state WITHOUT repainting the standard NC (we own
       * the caption) — DWM then tracks activation and renders the correct
       * frame.  Always claiming active freezes DWM's frame. */
      DwmFrameOnNCActivate(frame, hWnd, fActive);
      return (BOOL)DefWindowProc(hWnd, WM_NCACTIVATE, (WPARAM)fActive, (LPARAM)-1);
    }

    if (IsLayeredPresentWindow(hWnd))
      return TRUE;

    return FORWARD_WM_NCACTIVATE(hWnd, fActive, hwndActDeact, fMinimized, DefWindowProc);
}

static
LRESULT PFORCEINLINE APIPRIVATE
DefWindowProcNoRedraw(
    HWND   hWnd,
    UINT   uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    /* DefWindowProc repaints the classic frame for text/icon changes, which
     * corrupts a ULW sprite; hide the window from that redraw pass. */
    LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
    LRESULT lResult;

    SetWindowLongPtr(hWnd, GWL_STYLE, style & ~WS_VISIBLE);
    lResult = DefWindowProc(hWnd, uMsg, wParam, lParam);
    SetWindowLongPtr(hWnd, GWL_STYLE, style);

    return lResult;
}

static
LRESULT PFORCEINLINE CALLBACK
WGLWindow_OnSetText(
    HWND    hWnd,
    LPCTSTR lpszText)
{
    if (IsLayeredPresentWindow(hWnd))
      return DefWindowProcNoRedraw(hWnd, WM_SETTEXT, 0, (LPARAM)lpszText);

    return DefWindowProc(hWnd, WM_SETTEXT, 0, (LPARAM)lpszText);
}

static
LRESULT PFORCEINLINE CALLBACK
WGLWindow_OnSetIcon(
    HWND  hWnd,
    UINT  fType,
    HICON hicon)
{
    if (IsLayeredPresentWindow(hWnd))
      return DefWindowProcNoRedraw(hWnd, WM_SETICON, (WPARAM)fType, (LPARAM)hicon);

    return DefWindowProc(hWnd, WM_SETICON, (WPARAM)fType, (LPARAM)hicon);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnGetMinMaxInfo(
    HWND         hWnd,
    LPMINMAXINFO lpMinMaxInfo)
{
    if (GetDwmFrame(hWnd) ||
        ((GetWindowLongPtr(hWnd, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) &&
         (GetWindowLongPtr(hWnd, GWL_STYLE) & WS_CAPTION)))
    {
      /* Callable pre-state: the first WM_GETMINMAXINFO arrives during
       * CreateWindowEx, before WM_NCCREATE allocates the frame.  Gated on
       * WS_CAPTION: the caption-anatomy min track would clamp undecorated
       * secondary viewports (tooltips, popups) far above their real size. */
      DwmFrameGetMinMaxInfo(hWnd, lpMinMaxInfo);
      return;
    }

    FORWARD_WM_GETMINMAXINFO(hWnd, lpMinMaxInfo, DefWindowProc);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnActivate(
    HWND hWnd,
    UINT state,
    HWND hwndActDeact,
    BOOL fMinimized)
{
    DWMFRAME* frame = GetDwmFrame(hWnd);

    if (frame && !fMinimized)
      DwmFrameOnActivate(frame, hWnd, state);

    FORWARD_WM_ACTIVATE(hWnd, state, hwndActDeact, fMinimized, DefWindowProc);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnNCMouseMove(
    HWND hWnd,
    int  x,
    int  y,
    UINT codeHitTest)
{
    DWMFRAME* frame = GetDwmFrame(hWnd);

    if (frame)
      DwmFrameOnNCMouseMove(frame, hWnd, codeHitTest);

    FORWARD_WM_NCMOUSEMOVE(hWnd, x, y, codeHitTest, DefWindowProc);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnNCMouseLeave(
    HWND hWnd)
{
    DWMFRAME* frame = GetDwmFrame(hWnd);

    if (frame)
      DwmFrameOnNCMouseLeave(frame, hWnd);

    DefWindowProc(hWnd, WM_NCMOUSELEAVE, 0, 0);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnNCLButtonDown(
    HWND hWnd,
    BOOL fDoubleClick,
    int  x,
    int  y,
    UINT codeHitTest)
{
    DWMFRAME* frame = GetDwmFrame(hWnd);

    /* Caption-button press: capture-tracked by the frame; DefWindowProc must
     * never see it (HTCLOSE would get the classic NC button behavior). */
    if (frame && DwmFrameOnNCButtonDown(frame, hWnd, codeHitTest))
      return;

    /* System-menu icon: DefWindowProc's popup placement assumes a standard
     * NC caption — track it ourselves at the caption anchor.  Double-click
     * on the icon closes (native behavior). */
    if (frame && HTSYSMENU == codeHitTest)
    {
      if (fDoubleClick)
        (void)PostMessage(hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
      else
        DwmFrameShowSystemMenu(frame, hWnd, -1, -1);
      return;
    }

    FORWARD_WM_NCLBUTTONDOWN(hWnd, fDoubleClick, x, y, codeHitTest, DefWindowProc);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnNCRButtonUp(
    HWND hWnd,
    int  x,
    int  y,
    UINT codeHitTest)
{
    DWMFRAME* frame = GetDwmFrame(hWnd);

    /* Caption right-click: the system menu at the cursor (canon
     * WM_NCRBUTTONUP; DefWindowProc's placement assumes a standard NC). */
    if (frame && (HTCAPTION == codeHitTest || HTSYSMENU == codeHitTest))
    {
      DwmFrameShowSystemMenu(frame, hWnd, x, y);
      return;
    }

    FORWARD_WM_NCRBUTTONUP(hWnd, x, y, codeHitTest, DefWindowProc);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnMouseMove(
    HWND hWnd,
    int  x,
    int  y,
    UINT keyFlags)
{
    DWMFRAME* frame = GetDwmFrame(hWnd);

    /* Hot-track the pressed caption button while it owns the mouse stream. */
    if (frame && DwmFrameOnMouseMove(frame, hWnd, x, y))
      return;

    FORWARD_WM_MOUSEMOVE(hWnd, x, y, keyFlags, DefWindowProc);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnLButtonUp(
    HWND hWnd,
    int  x,
    int  y,
    UINT keyFlags)
{
    DWMFRAME* frame = GetDwmFrame(hWnd);

    /* Release commits the press if it lands on the pressed button. */
    if (frame && DwmFrameOnLButtonUp(frame, hWnd, x, y))
      return;

    FORWARD_WM_LBUTTONUP(hWnd, x, y, keyFlags, DefWindowProc);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnCaptureChanged(
    HWND hWnd,
    HWND hwndNewCapture)
{
    DWMFRAME* frame = GetDwmFrame(hWnd);

    UNREFERENCED_PARAMETER(hwndNewCapture);

    /* Something genuinely stole the capture: cancel the press. */
    if (frame)
      DwmFrameOnCaptureChanged(frame, hWnd);
}

static
BOOL PFORCEINLINE CALLBACK
WGLWindow_OnEraseBkgnd(
    HWND hWnd,
    HDC  hDC)
{
    UNREFERENCED_PARAMETER(hDC);

    /* Canon (imguiapp WM_ERASEBKGND): validate the update region so the
     * pending erase never turns into WM_PAINT churn against the presenter. */
    if (IsDXGIPresentWindow(hWnd))
    {
      RECT update = { 0 };
      GetUpdateRect(hWnd, &update, FALSE);
      ValidateRect(hWnd, &update);
      return TRUE;
    }

    /* TheScratchProgram shape: claim the erase, paint everything in WM_PAINT. */
    return TRUE;
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnEnterMenuLoop(
    HWND hWnd,
    BOOL fIsTrackPopupMenu)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    UNREFERENCED_PARAMETER(fIsTrackPopupMenu);

    if (pwglSurf && pwglSurf->dxgi)
      DxgiPresent_SetModalLive(pwglSurf->dxgi, TRUE);
    SetTimer(hWnd, 1, USER_TIMER_MINIMUM, NULL);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnExitMenuLoop(
    HWND hWnd,
    BOOL fIsShortcutMenu)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    UNREFERENCED_PARAMETER(fIsShortcutMenu);

    KillTimer(hWnd, 1);
    KillTimer(hWnd, 2);

    if (pwglSurf && pwglSurf->dxgi)
    {
      DxgiPresent_SetModalLive(pwglSurf->dxgi, FALSE);
      ModalRepaintWGL(hWnd, FALSE, TRUE, TRUE);
      return;
    }

    LPVOID fiber = (LPVOID)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (fiber)
      SwitchToFiber(fiber);

    DwmFlush();
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnEnterSizeMove(
    HWND hWnd)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    if (pwglSurf)
    {
      pwglSurf->in_modal_size_move = TRUE;
      pwglSurf->sizing_edge = 0;
      pwglSurf->moving = FALSE;
      /* R5: the pace thread posts refresh-rate ticks through the modal loop. */
      if (pwglSurf->dxgi)
        DxgiPresent_SetModalLive(pwglSurf->dxgi, TRUE);
    }

    if (IsLayeredPresentWindow(hWnd) || (pwglSurf && pwglSurf->dxgi))
    {
      /* Fallback cadence while the modal loop starves the run loop (the
       * pace thread may lack a vblank source). */
      SetTimer(hWnd, 1, USER_TIMER_MINIMUM, NULL);
    }
    else
    {
      /* TheScratchProgram shape: no timers — the resize ticks invalidate and
       * the modal loop's pump delivers WM_PAINT. */
      KillTimer(hWnd, 1);
      KillTimer(hWnd, 2);
    }
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnExitSizeMove(
    HWND hWnd)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    if (pwglSurf)
    {
      pwglSurf->sizing_edge = 0;
      pwglSurf->moving = FALSE;
    }
    KillTimer(hWnd, 1);
    KillTimer(hWnd, 2);
    if (pwglSurf)
      pwglSurf->in_modal_size_move = FALSE;

    if (pwglSurf && pwglSurf->dxgi)
    {
      /* Re-park the pace thread and settle one synced frame (0, 1). */
      DxgiPresent_SetModalLive(pwglSurf->dxgi, FALSE);
      ModalRepaintWGL(hWnd, FALSE, TRUE, TRUE);
      return;
    }

    if (IsLayeredPresentWindow(hWnd) || IsSecondaryViewportWindow(hWnd))
    {
      LPVOID fiber = (LPVOID)GetWindowLongPtr(hWnd, GWLP_USERDATA);
      if (fiber)
        SwitchToFiber(fiber);
      return;
    }

    /* TheScratchProgram shape: settle via the ordinary paint path. */
    InvalidateRect(hWnd, NULL, FALSE);
}

static
VOID PFORCEINLINE CALLBACK
WGLWindow_OnTimer(
    HWND hWnd,
    UINT uId)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);

    /* dwmframe chrome animation timer (160ms hover/theme/activation
     * crossfades): consumed entirely by the frame. */
    if (pwglSurf && pwglSurf->frame && DwmFrameOnTimer(pwglSurf->frame, hWnd, uId))
      return;

    if (pwglSurf && pwglSurf->in_modal_size_move && IsSecondaryViewportWindow(hWnd))
      return;

    /* Modal cadence tick: one rendered present per compositor frame — skip
     * when this frame's content is already queued at the current size (the
     * resize tick's repaints own the cadence during a live resize). */
    if (pwglSurf && pwglSurf->dxgi)
    {
      if (!ModalContentCurrentWGL(hWnd))
      {
        ModalRepaintWGL(hWnd, FALSE, TRUE, TRUE);
        if (!pwglSurf->moving)
          DwmFlush();
      }
      return;
    }

    LPVOID fiber = (LPVOID)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (fiber)
      SwitchToFiber(fiber);
}

/****** Public Interface Implementation **************************************/

WINWGLWINDOWAPI VOID WINAPI InitWGLControls(VOID)
{
    RegisterWGLWindowClass();
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

WINWGLWINDOWAPI HDC WINAPI BeginWGLWindowPaint(HWND hWnd)
{
    return NULL;
}

WINWGLWINDOWAPI BOOL WINAPI EndWGLWindowPaint(HDC hDC)
{
    return 1;
}

WINWGLWINDOWAPI BOOL WINAPI PresentWGLWindow(HWND hWnd)
{
    WGLSURFACE* pwglSurf = (WGLSURFACE*)GetWindowLongPtr(hWnd, 0);
    HDC hdc;
    BOOL presented;

    if (pwglSurf && pwglSurf->dxgi)
    {
      SIZE sz;
      BOOL fRestart;
      BOOL fVsync;

      /* Driven size: the pending rgrc[0] during the WM_NCCALCSIZE
       * pending-rect repaint, the live client rect otherwise. */
      if (!GetWGLWindowDrivenClientSize(hWnd, &sz))
        return FALSE;
      if (sz.cx <= 0 || sz.cy <= 0)
        return FALSE;
      sz.cx = CLAMP(sz.cx, 1, pwglSurf->width);
      sz.cy = CLAMP(sz.cy, 1, pwglSurf->height);

      if (!wglMakeCurrent(pwglSurf->pbdc, pwglSurf->pbrc))
        return FALSE;

      /* Chrome over the client content — drawn in GL into the same frame,
       * so the fill below carries client + chrome into buffer 0 in one copy
       * (one present; no D2D pass). */
      if (pwglSurf->frame)
        DwmFrameDrawChrome(pwglSurf->frame, hWnd, sz.cx, sz.cy);

      if (!DxgiPresent_FillFromGL(pwglSurf->dxgi, sz.cx, sz.cy))
        return FALSE;

      /* Present flavors (dxgi-noflicker §3): the WndProc repaint paths pin
       * their (fRestart, fVsync) pair; everything else is a run-loop frame
       * at (1, 0). */
      if (pwglSurf->modal_present_active)
      {
        fRestart = pwglSurf->modal_restart;
        fVsync = pwglSurf->modal_vsync;
      }
      else
      {
        fRestart = TRUE;
        fVsync = FALSE;
      }

      presented = DxgiPresent_Present(pwglSurf->dxgi, sz.cx, sz.cy, fRestart, fVsync);
      if (presented)
      {
        if (pwglSurf->paint_hdc)
          pwglSurf->paint_presented = TRUE;
        else
          ValidateRect(hWnd, NULL);
      }
      return presented;
    }

    if (IsLayeredPresentWindow(hWnd))
    {
      presented = PresentSurfaceLayered(hWnd);
      if (presented)
      {
        if (pwglSurf && pwglSurf->paint_hdc)
          pwglSurf->paint_presented = TRUE;
        else
          ValidateRect(hWnd, NULL);
        return TRUE;
      }
      /* fall through to the DC blit when the layered present is unavailable
       * (e.g. window grew past the surface) */
    }

    if (pwglSurf && pwglSurf->paint_hdc)
    {
      presented = PresentSurfaceToDC(hWnd, pwglSurf->paint_hdc);
      if (presented)
        pwglSurf->paint_presented = TRUE;
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

WINWGLWINDOWAPI BOOL WINAPI IsWGLWindowCaptionPressActive(HWND hWnd)
{
    WGLSURFACE* pwglSurf = hWnd ? (WGLSURFACE*)GetWindowLongPtr(hWnd, 0) : NULL;
    return pwglSurf && pwglSurf->frame && DwmFrameButtonPressActive(pwglSurf->frame);
}

WINWGLWINDOWAPI BOOL WINAPI GetWGLWindowDrivenClientSize(HWND hWnd, SIZE* psz)
{
    WGLSURFACE* pwglSurf = hWnd ? (WGLSURFACE*)GetWindowLongPtr(hWnd, 0) : NULL;
    RECT rc;

    if (!pwglSurf || !psz)
      return FALSE;
    if (pwglSurf->pending_cx > 0 && pwglSurf->pending_cy > 0)
    {
      psz->cx = pwglSurf->pending_cx;
      psz->cy = pwglSurf->pending_cy;
      return TRUE;
    }
    GetClientRect(hWnd, &rc);
    psz->cx = RECTWIDTH(rc);
    psz->cy = RECTHEIGHT(rc);
    return TRUE;
}

WINWGLWINDOWAPI BOOL WINAPI IsWGLWindowInSynchronousResizeRender(HWND hWnd)
{
    WGLCLASSSTATE* state = hWnd ? GetWGLClassState(hWnd) : NULL;
    return state && state->sync_resize_depth > 0;
}

WINWGLWINDOWAPI HWND WINAPI GetWGLWindowSynchronousResizeHwnd(HWND hWnd)
{
    WGLCLASSSTATE* state = hWnd ? GetWGLClassState(hWnd) : NULL;
    return state ? state->sync_resize_hwnd : NULL;
}


WINWGLWINDOWAPI BOOL WINAPI IsWGLWindowInModalSizeMove(HWND hWnd)
{
    WGLSURFACE* pwglSurf = hWnd ? (WGLSURFACE*)GetWindowLongPtr(hWnd, 0) : NULL;
    return pwglSurf && pwglSurf->in_modal_size_move;
}

static BOOL quit_posted;

WINWGLWINDOWAPI BOOL WINAPI WGLWindowQuitPosted(VOID)
{
    return quit_posted;
}

WINWGLWINDOWAPI VOID WINAPI MessageFiberProc(void* unused)
{
    for (;;)
    {
        MSG msg;

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE|PM_NOYIELD))
        {
            /* Orderly quit: hand WM_QUIT back to the render fiber, whose
             * teardown destroys the secondary platform windows BEFORE the
             * main window (every viewport renders and presents through the
             * main window's GL context).  ExitProcess from inside the pump
             * ripped the presenters' composition channel down mid-flight —
             * CoreMessaging fail-fast (0xC0000602) whenever a secondary
             * viewport was still alive. */
            if (msg.message == WM_QUIT)
            {
                quit_posted = TRUE;
                break;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        SwitchToFiber(unused);
    }
}

WINWGLWINDOWAPI LRESULT CALLBACK DefWGLWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg) {
    HANDLE_MSG(hWnd, WM_DESTROY, WGLWindow_OnDestroy);
    HANDLE_MSG(hWnd, WM_PAINT, WGLWindow_OnPaint);
    HANDLE_MSG(hWnd, WM_PRINTCLIENT, WGLWindow_OnPrintClient);
    HANDLE_MSG(hWnd, WM_SIZE, WGLWindow_OnSize);
    HANDLE_MSG(hWnd, WM_SIZING, WGLWindow_OnSizing);
    HANDLE_MSG(hWnd, WM_MOVING, WGLWindow_OnMoving);
    HANDLE_MSG(hWnd, DXGIPRESENT_WM_PACE, WGLWindow_OnDxgiPace);
    HANDLE_MSG(hWnd, WM_DPICHANGED, WGLWindow_OnDpiChanged);
    HANDLE_MSG(hWnd, WM_DWMNCRENDERINGCHANGED, WGLWindow_OnDwmNCRenderingChanged);
    HANDLE_MSG(hWnd, WM_SYSCOMMAND, WGLWindow_OnSysCommand);
    HANDLE_MSG(hWnd, WM_WINDOWPOSCHANGED, WGLWindow_OnWindowPosChanged);
    HANDLE_MSG(hWnd, WM_NCPAINT, WGLWindow_OnNCPaint);
    HANDLE_MSG(hWnd, WM_NCACTIVATE, WGLWindow_OnNCActivate);
    HANDLE_MSG(hWnd, WM_SETTEXT, WGLWindow_OnSetText);
    HANDLE_MSG(hWnd, WM_SETICON, WGLWindow_OnSetIcon);
    HANDLE_MSG(hWnd, WM_NCCREATE, WGLWindow_OnNCCreate);
    HANDLE_MSG(hWnd, WM_NCCALCSIZE, WGLWindow_OnNCCalcSize);
    HANDLE_MSG(hWnd, WM_NCHITTEST, WGLWindow_OnNCHitTest);
    HANDLE_MSG(hWnd, WM_GETMINMAXINFO, WGLWindow_OnGetMinMaxInfo);
    HANDLE_MSG(hWnd, WM_ACTIVATE, WGLWindow_OnActivate);
    HANDLE_MSG(hWnd, WM_NCMOUSEMOVE, WGLWindow_OnNCMouseMove);
    HANDLE_MSG(hWnd, WM_NCMOUSELEAVE, WGLWindow_OnNCMouseLeave);
    HANDLE_MSG(hWnd, WM_NCLBUTTONDOWN, WGLWindow_OnNCLButtonDown);
    HANDLE_MSG(hWnd, WM_NCLBUTTONDBLCLK, WGLWindow_OnNCLButtonDown);
    HANDLE_MSG(hWnd, WM_NCRBUTTONUP, WGLWindow_OnNCRButtonUp);
    HANDLE_MSG(hWnd, WM_MOUSEMOVE, WGLWindow_OnMouseMove);
    HANDLE_MSG(hWnd, WM_LBUTTONUP, WGLWindow_OnLButtonUp);
    HANDLE_MSG(hWnd, WM_CAPTURECHANGED, WGLWindow_OnCaptureChanged);
    HANDLE_MSG(hWnd, WM_ERASEBKGND, WGLWindow_OnEraseBkgnd);
    HANDLE_MSG(hWnd, WM_ENTERMENULOOP, WGLWindow_OnEnterMenuLoop);
    HANDLE_MSG(hWnd, WM_EXITMENULOOP, WGLWindow_OnExitMenuLoop);
    HANDLE_MSG(hWnd, WM_ENTERSIZEMOVE, WGLWindow_OnEnterSizeMove);
    HANDLE_MSG(hWnd, WM_EXITSIZEMOVE, WGLWindow_OnExitSizeMove);
    HANDLE_MSG(hWnd, WM_TIMER, WGLWindow_OnTimer);
    FORWARD_MSG(hWnd, uMsg, wParam, lParam, DefWindowProc);
    }
}

static 
ATOM PFORCEINLINE APIPRIVATE
RegisterWGLWindowClass(
    VOID)
{
    WNDCLASSEX wcx = { sizeof(wcx) };

    /* NO CS_OWNDC: WS_EX_NOREDIRECTIONBITMAP is documented as unusable with
     * CS_OWNDC/CS_CLASSDC classes — Windows silently DROPS the ex-style at
     * creation, which kills every presenter/chrome/geometry branch gated on
     * it.  GL never binds the window DC (the pbuffer owns its own DC), so
     * own-DC semantics were vestigial here. */
    wcx.style         = CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW;
    wcx.lpfnWndProc   = DefWGLWindowProc;
    wcx.hInstance     = GetModuleHandle(NULL);
    wcx.cbClsExtra    = sizeof(LONG_PTR);   /* WGLCLASSSTATE* */
    wcx.cbWndExtra    = sizeof(void*);
    wcx.hCursor       = LoadCursor(0, IDC_ARROW);
    wcx.hbrBackground = NULL;
    wcx.lpszMenuName  = NULL;
    wcx.lpszClassName = WGLWINDOW_CLASS;

    return RegisterClassEx(&wcx);
}
