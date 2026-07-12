/*
 * dwmframe.c -- WGLWindow caption chrome, ported from Win32X dwmframex.c (the in-process uDWM caption
 * compositor) and conformed to the imguiapp_impl_win32_d2ddxgi chrome tier.
 *
 * This module owns the CHROME: caption band, system icon, title (DWrite, system caption font), the four
 * caption buttons (light/dark, minimize, maximize/restore, close) with uDWM's 160ms hover/press/theme/
 * activation crossfades, the FindNCHit-order hit test, WM_NCCALCSIZE / WM_GETMINMAXINFO geometry, and the
 * capture-tracked button press flow (xxxTrackCaptionButton shape).
 *
 * It does NOT own a pipeline: drawing happens into the presenter's D2D device context (dxgipresent.cpp,
 * target = the composition swapchain's buffer 0), between the GL fill and the present -- chrome and
 * client content are ONE present.  The context/device arrive as opaque pointers and are called through
 * hand-declared C vtables (ABI-identical to the real interfaces; the D2D/DWrite headers are C++-only).
 *
 * Repaint policy: input/theme changes here only update state, arm the 160ms timer, and invalidate the
 * window; the window control renders full app frames (chrome included) through its one present path.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define CINTERFACE          /* C lpVtbl layout for the COM headers that support it (d3d11/dxgi) */
#define COBJMACROS

#include <windows.h>
#include <windowsx.h>
#include <dxgi.h>
#include <d3d11.h>
#include <d2d1.h>
#include <d2d1_1.h>

#include "dwmframe.h"

#pragma comment(lib, "dwrite")

/* winuser.h maps DrawText to DrawTextW; the vtable slot below is the real method name. */
#ifdef DrawText
#undef DrawText
#endif

/* ---- our own COBJMACROS: a COM call is a vtable deref. ------------------------------------------ */
#define CCALL(p, m, ...)  ((p)->lpVtbl->m((p), __VA_ARGS__))
#define CCALL0(p, m)      ((p)->lpVtbl->m((p)))

/* ---- hand-declared D2D / DWrite interfaces (C++-only headers; slot order per SDK) ---------------- */
typedef struct IDWriteFactory    IDWriteFactory;
typedef struct IDWriteTextFormat IDWriteTextFormat;
typedef struct DWF_ID2D1Bitmap1  DWF_ID2D1Bitmap1;

#define DWF_FACTORY_TYPE_SHARED         0u
#define DWF_FONT_STYLE_NORMAL           0u
#define DWF_FONT_STYLE_ITALIC           2u
#define DWF_FONT_STRETCH_NORMAL         5u
#define DWF_TEXT_ALIGNMENT_LEADING      0u
#define DWF_TEXT_ALIGNMENT_CENTER       2u
#define DWF_PARAGRAPH_ALIGNMENT_CENTER  2u
#define DWF_WORD_WRAPPING_NO_WRAP       1u
#define DWF_MEASURING_MODE_NATURAL      0u

typedef struct ID2D1DeviceContextVtbl_
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(ID2D1DeviceContext*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE* AddRef)(ID2D1DeviceContext*);
    ULONG   (STDMETHODCALLTYPE* Release)(ID2D1DeviceContext*);
    void*   rsvd0[5];   /* slots 3..7 */
    HRESULT (STDMETHODCALLTYPE* CreateSolidColorBrush)(ID2D1DeviceContext*, const D2D1_COLOR_F*, const D2D1_BRUSH_PROPERTIES*, ID2D1SolidColorBrush**); /* 8 */
    void*   rsvd1[8];   /* slots 9..16 */
    void    (STDMETHODCALLTYPE* FillRectangle)(ID2D1DeviceContext*, const D2D1_RECT_F*, ID2D1Brush*); /* slot 17 */
    void*   rsvd2[8];   /* slots 18..25 */
    void    (STDMETHODCALLTYPE* DrawBitmap)(ID2D1DeviceContext*, ID2D1Bitmap*, const D2D1_RECT_F*, FLOAT, D2D1_BITMAP_INTERPOLATION_MODE, const D2D1_RECT_F*); /* 26 */
    void    (STDMETHODCALLTYPE* DrawText)(ID2D1DeviceContext*, const WCHAR*, UINT32, IDWriteTextFormat*, const D2D1_RECT_F*, ID2D1Brush*, D2D1_DRAW_TEXT_OPTIONS, UINT); /* 27 */
    void*   rsvd3[2];   /* slots 28..29 */
    void    (STDMETHODCALLTYPE* SetTransform)(ID2D1DeviceContext*, const D2D1_MATRIX_3X2_F*); /* slot 30 */
    void*   rsvd4[31];  /* slots 31..61 */
    HRESULT (STDMETHODCALLTYPE* CreateBitmapFromDxgiSurface)(ID2D1DeviceContext*, IDXGISurface*, const D2D1_BITMAP_PROPERTIES1*, DWF_ID2D1Bitmap1**); /* slot 62 */
} ID2D1DeviceContextVtbl_;
struct ID2D1DeviceContext { const ID2D1DeviceContextVtbl_* lpVtbl; };

typedef struct DWF_ID2D1Bitmap1Vtbl
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(DWF_ID2D1Bitmap1*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE* AddRef)(DWF_ID2D1Bitmap1*);
    ULONG   (STDMETHODCALLTYPE* Release)(DWF_ID2D1Bitmap1*);
} DWF_ID2D1Bitmap1Vtbl;
struct DWF_ID2D1Bitmap1 { const DWF_ID2D1Bitmap1Vtbl* lpVtbl; };

typedef struct IDWriteFactoryVtbl
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(IDWriteFactory*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE* AddRef)(IDWriteFactory*);
    ULONG   (STDMETHODCALLTYPE* Release)(IDWriteFactory*);
    void*   rsvd[12];   /* slots 3..14 */
    HRESULT (STDMETHODCALLTYPE* CreateTextFormat)(IDWriteFactory*, const WCHAR*, void*, UINT, UINT, UINT, FLOAT, const WCHAR*, IDWriteTextFormat**); /* 15 */
} IDWriteFactoryVtbl;
struct IDWriteFactory { const IDWriteFactoryVtbl* lpVtbl; };

typedef struct IDWriteTextFormatVtbl
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(IDWriteTextFormat*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE* AddRef)(IDWriteTextFormat*);
    ULONG   (STDMETHODCALLTYPE* Release)(IDWriteTextFormat*);
    HRESULT (STDMETHODCALLTYPE* SetTextAlignment)(IDWriteTextFormat*, UINT);       /* slot 3 */
    HRESULT (STDMETHODCALLTYPE* SetParagraphAlignment)(IDWriteTextFormat*, UINT);  /* slot 4 */
    HRESULT (STDMETHODCALLTYPE* SetWordWrapping)(IDWriteTextFormat*, UINT);        /* slot 5 */
} IDWriteTextFormatVtbl;
struct IDWriteTextFormat { const IDWriteTextFormatVtbl* lpVtbl; };

static const GUID DWF_IID_IDXGISurface =
    { 0xcafcb56cu, 0x6ac3u, 0x4889u, { 0xbfu, 0x47u, 0x9eu, 0x23u, 0xbbu, 0xd2u, 0x60u, 0xecu } };
static const GUID DWF_IID_IDWriteFactory =
    { 0xb859ee5au, 0xd838u, 0x4b5bu, { 0xa2u, 0xe8u, 0x1au, 0xdcu, 0x7du, 0x93u, 0xdbu, 0x48u } };

enum DWF_BTN { DWB_NONE = 0, DWB_LIGHTDARK, DWB_MIN, DWB_MAX, DWB_CLOSE };

EXTERN_C HRESULT WINAPI DWriteCreateFactory(UINT, REFIID, IUnknown**);

#define DWF_DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWF_DWMWCP_ROUND                   2

typedef struct DWF_MARGINS { int cxLeft; int cxRight; int cyTop; int cyBottom; } DWF_MARGINS;
typedef struct DWF_BLURBEHIND
{
    DWORD dwFlags;
    BOOL  fEnable;
    HRGN  hRgnBlur;
    BOOL  fTransitionOnMaximized;
} DWF_BLURBEHIND;
#define DWF_BB_ENABLE     0x00000001
#define DWF_BB_BLURREGION 0x00000002
typedef HRESULT (WINAPI* PFN_DWF_EXTEND)(HWND, const DWF_MARGINS*);
typedef HRESULT (WINAPI* PFN_DWF_SETATTR)(HWND, DWORD, const void*, DWORD);
typedef HRESULT (WINAPI* PFN_DWF_BLURBEHIND)(HWND, const DWF_BLURBEHIND*);

/* dwmapi loaded once; process-global facts, mirroring the reference. */
static HMODULE             g_dwfDwmapi;
static PFN_DWF_EXTEND      g_dwfExtend;
static PFN_DWF_SETATTR     g_dwfSetAttr;
static PFN_DWF_BLURBEHIND  g_dwfBlurBehind;

#define DWF_ANIM_TIMER_ID  ((UINT_PTR)0x0DF00001u)
#define DWF_ANIM_INTERVAL  8u
#define DWF_ANIM_DURATION  160u

/* ---- per-window chrome state (the reference's g_dwf, pipeline-less) ------------------------------ */
struct DWMFRAME
{
    HWND               hwnd;
    IDWriteFactory*    pDWrite;
    IDWriteTextFormat* pTextFormat;
    IDWriteTextFormat* pIconFormat;
    DWF_ID2D1Bitmap1*  pIconBmp;
    BOOL               fIconTried;
    BOOL               fFirstActivate;
    int                idHot;
    int                idPressed;
    BOOL               fTracking;
    BOOL               fCapturing;
    BOOL               fDark;
    BOOL               fWndActive;
    BOOL               fAnim;
    DWORD              dwAnimStart;
    BOOL               fDarkFrom;
    BOOL               fActiveFrom;
    float              flAnimT;
    float              flBtnOpacity[5];
};

/* ---- helpers -------------------------------------------------------------------------------------- */

static void DwfRelease(IUnknown** ppUnk)
{
    if (*ppUnk)
    {
      CCALL0(*ppUnk, Release);
      *ppUnk = NULL;
    }
}

static D2D1_COLOR_F DwfColor(COLORREF cr)
{
    D2D1_COLOR_F c;
    c.r = (FLOAT)GetRValue(cr) / 255.0f;
    c.g = (FLOAT)GetGValue(cr) / 255.0f;
    c.b = (FLOAT)GetBValue(cr) / 255.0f;
    c.a = 1.0f;
    return c;
}

static D2D1_COLOR_F DwfLerp(D2D1_COLOR_F a, D2D1_COLOR_F b, float t)
{
    D2D1_COLOR_F c;
    c.r = a.r + (b.r - a.r) * t;
    c.g = a.g + (b.g - a.g) * t;
    c.b = a.b + (b.b - a.b) * t;
    c.a = a.a + (b.a - a.a) * t;
    return c;
}

static D2D1_COLOR_F DwfTextColor(BOOL fDark, BOOL fActive)
{
    D2D1_COLOR_F c = DwfColor(fDark ? RGB(255, 255, 255) : RGB(0, 0, 0));
    if (!fActive)
      c.a = 0.60f;
    return c;
}

static D2D1_COLOR_F DwfGlyphColor(BOOL fDark, BOOL fActive)
{
    return DwfColor(fActive ? (fDark ? RGB(255, 255, 255) : RGB(0, 0, 0))
                            : (fDark ? RGB(0xAA, 0xAA, 0xAA) : RGB(0x64, 0x64, 0x64)));
}

static D2D1_COLOR_F DwfCaptionColor(BOOL fDark, BOOL fActive)
{
    COLORREF cr;
    if (fDark)
      cr = fActive ? RGB(0x20, 0x20, 0x20) : RGB(0x2B, 0x2B, 0x2B);
    else
      cr = fActive ? RGB(0xF3, 0xF3, 0xF3) : RGB(0xFB, 0xFB, 0xFB);
    return DwfColor(cr);
}

static UINT DwfDpi(HWND hwnd)
{
    UINT d = GetDpiForWindow(hwnd);
    return d ? d : 96u;
}

UINT WINAPI DwmFrameCaptionHeight(HWND hwnd)
{
    UINT dpi = DwfDpi(hwnd);
    int  cy  = GetSystemMetricsForDpi(SM_CYCAPTION, dpi) + GetSystemMetricsForDpi(SM_CYFRAME, dpi) +
               GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    if (cy <= 0)
      cy = 32;
    return (UINT)cy;
}

/* The invisible resize border (SM_CXFRAME/SM_CYFRAME + SM_CXPADDEDBORDER). */
static void DwfWindowBorders(HWND hwnd, SIZE* psz)
{
    UINT dpi = DwfDpi(hwnd);
    int  pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    psz->cx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + pad;
    psz->cy = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + pad;
}

/* Caption-button cells in CLIENT coords: native ~47-DIP cells scaled by DPI,
 * right-aligned, Close..light/dark right-to-left (uDWM UpdateNCAreaButton). */
static int DwfButtonRects(HWND hwnd, RECT* prcClose, RECT* prcMax, RECT* prcMin, RECT* prcLD)
{
    RECT rc;
    UINT dpi  = DwfDpi(hwnd);
    int  capH = (int)DwmFrameCaptionHeight(hwnd);
    int  btnW = MulDiv(47, (int)dpi, 96);
    int  r;

    GetClientRect(hwnd, &rc);
    r = rc.right;
    prcClose->right = r; prcClose->left = r - btnW; r -= btnW;
    prcMax->right   = r; prcMax->left   = r - btnW; r -= btnW;
    prcMin->right   = r; prcMin->left   = r - btnW; r -= btnW;
    prcLD->right    = r; prcLD->left    = r - btnW;
    prcClose->top = 0; prcClose->bottom = capH;
    prcMax->top   = 0; prcMax->bottom   = capH;
    prcMin->top   = 0; prcMin->bottom   = capH;
    prcLD->top    = 0; prcLD->bottom    = capH;
    return 1;
}

/* The reference's WM_ACTIVATE window dressing (imguiapp ApplyWindowDressing,
 * itself the ImmersiveWindow OnActivate): DwmExtendFrameIntoClientArea with
 * the canonical {1, 1, -1, 1} margins plus blur-behind with an EMPTY region.
 * Together they make the window's un-drawn area genuinely transparent — the
 * visible face ends exactly at the composed content instead of DWM giving
 * the whole window rect a default backdrop.  Idempotent; must be (re-)
 * applied in response to WM_ACTIVATE for the frame to settle. */
static void DwfApplyDwmFrame(HWND hwnd)
{
    union { FARPROC fp; PFN_DWF_EXTEND ex; PFN_DWF_SETATTR sa; PFN_DWF_BLURBEHIND bb; } u;
    DWF_MARGINS m;
    UINT        corner;

    if (!g_dwfDwmapi)
    {
      g_dwfDwmapi = LoadLibraryW(L"dwmapi.dll");
      if (g_dwfDwmapi)
      {
        u.fp = GetProcAddress(g_dwfDwmapi, "DwmExtendFrameIntoClientArea"); g_dwfExtend     = u.ex;
        u.fp = GetProcAddress(g_dwfDwmapi, "DwmSetWindowAttribute");        g_dwfSetAttr    = u.sa;
        u.fp = GetProcAddress(g_dwfDwmapi, "DwmEnableBlurBehindWindow");    g_dwfBlurBehind = u.bb;
      }
    }
    if (g_dwfExtend)
    {
      /* Reference margins, verbatim. */
      m.cxLeft = 1; m.cxRight = 1; m.cyTop = -1; m.cyBottom = 1;
      (void)g_dwfExtend(hwnd, &m);
    }
    if (g_dwfBlurBehind)
    {
      DWF_BLURBEHIND bb;
      bb.dwFlags                = DWF_BB_ENABLE | DWF_BB_BLURREGION;
      bb.fEnable                = TRUE;
      bb.hRgnBlur               = CreateRectRgn(0, 0, -1, -1);   /* empty region: no blur, pure transparency */
      bb.fTransitionOnMaximized = FALSE;
      (void)g_dwfBlurBehind(hwnd, &bb);
      if (bb.hRgnBlur)
        (void)DeleteObject(bb.hRgnBlur);
    }
    if (g_dwfSetAttr)
    {
      corner = DWF_DWMWCP_ROUND;
      (void)g_dwfSetAttr(hwnd, DWF_DWMWA_WINDOW_CORNER_PREFERENCE, &corner, (DWORD)sizeof(corner));
    }
}

/* ---- formats + icon -------------------------------------------------------------------------------- */

static NONCLIENTMETRICSW g_dwfNcm;   /* ~500 bytes: off-stack, GUI thread only (reference remedy) */

static void DwfCreateTextFormat(DWMFRAME* f)
{
    FLOAT size;

    if (!f->pDWrite)
      return;
    DwfRelease((IUnknown**)&f->pTextFormat);
    ZeroMemory(&g_dwfNcm, sizeof(g_dwfNcm));
    g_dwfNcm.cbSize = (DWORD)sizeof(g_dwfNcm);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, (UINT)sizeof(g_dwfNcm), &g_dwfNcm, 0))
      return;
    size = (FLOAT)((g_dwfNcm.lfCaptionFont.lfHeight < 0) ? -g_dwfNcm.lfCaptionFont.lfHeight
                                                         : g_dwfNcm.lfCaptionFont.lfHeight);
    if (size < 1.0f)
      size = 12.0f;
    (void)CCALL(f->pDWrite, CreateTextFormat, g_dwfNcm.lfCaptionFont.lfFaceName, NULL,
                (UINT)g_dwfNcm.lfCaptionFont.lfWeight,
                g_dwfNcm.lfCaptionFont.lfItalic ? DWF_FONT_STYLE_ITALIC : DWF_FONT_STYLE_NORMAL,
                DWF_FONT_STRETCH_NORMAL, size, L"", &f->pTextFormat);
    if (f->pTextFormat)
    {
      (void)CCALL(f->pTextFormat, SetTextAlignment, DWF_TEXT_ALIGNMENT_LEADING);
      (void)CCALL(f->pTextFormat, SetParagraphAlignment, DWF_PARAGRAPH_ALIGNMENT_CENTER);
      (void)CCALL(f->pTextFormat, SetWordWrapping, DWF_WORD_WRAPPING_NO_WRAP);
    }
}

static void DwfCreateIconFormat(DWMFRAME* f)
{
    FLOAT size;

    if (!f->pDWrite)
      return;
    DwfRelease((IUnknown**)&f->pIconFormat);
    size = (FLOAT)DwmFrameCaptionHeight(f->hwnd) * 0.36f;
    if (size < 8.0f)
      size = 8.0f;
    (void)CCALL(f->pDWrite, CreateTextFormat, L"Segoe Fluent Icons", NULL, 400u,
                DWF_FONT_STYLE_NORMAL, DWF_FONT_STRETCH_NORMAL, size, L"", &f->pIconFormat);
    if (!f->pIconFormat)
      (void)CCALL(f->pDWrite, CreateTextFormat, L"Segoe MDL2 Assets", NULL, 400u,
                  DWF_FONT_STYLE_NORMAL, DWF_FONT_STRETCH_NORMAL, size, L"", &f->pIconFormat);
    if (f->pIconFormat)
    {
      (void)CCALL(f->pIconFormat, SetTextAlignment, DWF_TEXT_ALIGNMENT_CENTER);
      (void)CCALL(f->pIconFormat, SetParagraphAlignment, DWF_PARAGRAPH_ALIGNMENT_CENTER);
      (void)CCALL(f->pIconFormat, SetWordWrapping, DWF_WORD_WRAPPING_NO_WRAP);
    }
}

/* High-res caption icon: the window's big icon rasterized once into a
 * premultiplied BGRA DIB, uploaded as an ID3D11Texture2D on the PRESENTER's
 * device, wrapped as a D2D bitmap on the presenter's context (reference
 * DwfEnsureIcon). */
static void DwfEnsureIcon(DWMFRAME* f, HWND hwnd, ID3D11Device* pDev, ID2D1DeviceContext* pDC)
{
    HICON                   hIcon;
    ICONINFO                ii;
    BITMAP                  bm;
    BITMAPINFO              biH;
    HDC                     hdcScreen;
    HDC                     hdcMem;
    HBITMAP                 hDib;
    HBITMAP                 hOld;
    void*                   pBits;
    BYTE*                   p;
    int                     n;
    int                     i;
    ID3D11Texture2D*        pTex;
    IDXGISurface*           pSurf;
    D3D11_TEXTURE2D_DESC    td;
    D3D11_SUBRESOURCE_DATA  sd;
    D2D1_BITMAP_PROPERTIES1 bp;

    if (f->pIconBmp || f->fIconTried || !pDev || !pDC)
      return;
    f->fIconTried = TRUE;

    hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
    if (!hIcon) hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0);
    if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
    if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
    if (!hIcon) hIcon = LoadIconW(NULL, IDI_APPLICATION);
    if (!hIcon) return;

    n = 32;
    ZeroMemory(&ii, sizeof(ii));
    if (GetIconInfo(hIcon, &ii))
    {
      if (ii.hbmColor && (0 != GetObjectW(ii.hbmColor, (int)sizeof(bm), &bm))) n = bm.bmWidth;
      if (ii.hbmColor) (void)DeleteObject(ii.hbmColor);
      if (ii.hbmMask)  (void)DeleteObject(ii.hbmMask);
    }
    if (n < 16)  n = 16;
    if (n > 256) n = 256;

    ZeroMemory(&biH, sizeof(biH));
    biH.bmiHeader.biSize        = (DWORD)sizeof(BITMAPINFOHEADER);
    biH.bmiHeader.biWidth       = n;
    biH.bmiHeader.biHeight      = -n;
    biH.bmiHeader.biPlanes      = 1;
    biH.bmiHeader.biBitCount    = 32;
    biH.bmiHeader.biCompression = BI_RGB;

    pBits     = NULL;
    pTex      = NULL;
    pSurf     = NULL;
    hdcScreen = GetDC(NULL);
    hdcMem    = CreateCompatibleDC(hdcScreen);
    hDib      = CreateDIBSection(hdcScreen, &biH, DIB_RGB_COLORS, &pBits, NULL, 0u);
    if (hdcScreen) (void)ReleaseDC(NULL, hdcScreen);

    if (hdcMem && hDib && pBits)
    {
      hOld = (HBITMAP)SelectObject(hdcMem, hDib);
      (void)DrawIconEx(hdcMem, 0, 0, hIcon, n, n, 0u, NULL, DI_NORMAL);
      (void)GdiFlush();
      (void)SelectObject(hdcMem, hOld);

      p = (BYTE*)pBits;
      for (i = 0; i < n * n; ++i)
      {
        UINT a = p[3];
        p[0] = (BYTE)(((UINT)p[0] * a) / 255u);
        p[1] = (BYTE)(((UINT)p[1] * a) / 255u);
        p[2] = (BYTE)(((UINT)p[2] * a) / 255u);
        p += 4;
      }

      ZeroMemory(&td, sizeof(td));
      td.Width            = (UINT)n;
      td.Height           = (UINT)n;
      td.MipLevels        = 1u;
      td.ArraySize        = 1u;
      td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
      td.SampleDesc.Count = 1u;
      td.Usage            = D3D11_USAGE_DEFAULT;
      td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
      sd.pSysMem          = pBits;
      sd.SysMemPitch      = (UINT)(n * 4);
      sd.SysMemSlicePitch = 0u;
      (void)CCALL(pDev, CreateTexture2D, &td, &sd, &pTex);
      if (pTex)
        (void)CCALL((IUnknown*)pTex, QueryInterface, &DWF_IID_IDXGISurface, (void**)&pSurf);
      if (pSurf)
      {
        bp.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
        bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        bp.dpiX                  = 96.0f;
        bp.dpiY                  = 96.0f;
        bp.bitmapOptions         = D2D1_BITMAP_OPTIONS_NONE;
        bp.colorContext          = NULL;
        (void)CCALL(pDC, CreateBitmapFromDxgiSurface, pSurf, &bp, &f->pIconBmp);
      }
    }

    DwfRelease((IUnknown**)&pSurf);
    DwfRelease((IUnknown**)&pTex);
    if (hDib)   (void)DeleteObject(hDib);
    if (hdcMem) (void)DeleteDC(hdcMem);
}

/* ---- chrome drawing (into the presenter's context; imguiapp DrawCallback slot) -------------------- */

static void DwfDrawGlyph(DWMFRAME* f, ID2D1DeviceContext* pDC, ID2D1Brush* pBrush, const RECT* prc, WCHAR glyph)
{
    IDWriteTextFormat* pf = f->pIconFormat ? f->pIconFormat : f->pTextFormat;
    D2D1_RECT_F        rc;
    WCHAR              s[1];

    if (!pf)
      return;
    s[0]      = glyph;
    rc.left   = (FLOAT)prc->left;
    rc.top    = (FLOAT)prc->top;
    rc.right  = (FLOAT)prc->right;
    rc.bottom = (FLOAT)prc->bottom;
    CCALL(pDC, DrawText, s, 1u, pf, &rc, pBrush, D2D1_DRAW_TEXT_OPTIONS_NONE, DWF_MEASURING_MODE_NATURAL);
}

static void DwfDrawButton(DWMFRAME* f, ID2D1DeviceContext* pDC, const RECT* prc, int id, WCHAR glyph,
                          BOOL fDark, D2D1_COLOR_F cfGlyph, float flHover)
{
    BOOL                  fPressed = (f->idPressed == id);
    COLORREF              crFill;
    D2D1_COLOR_F          cf;
    D2D1_RECT_F           rf;
    ID2D1SolidColorBrush* pb;

    if (DWB_CLOSE == id)
    {
      crFill  = fPressed ? RGB(0xC8, 0x3C, 0x2F) : RGB(0xC4, 0x2B, 0x1C);
      cfGlyph = DwfLerp(cfGlyph, DwfColor(RGB(255, 255, 255)), flHover);
    }
    else
    {
      crFill = fPressed ? (fDark ? RGB(0x50, 0x50, 0x50) : RGB(0xCC, 0xCC, 0xCC))
                        : (fDark ? RGB(0x3D, 0x3D, 0x3D) : RGB(0xE9, 0xE9, 0xE9));
    }

    rf.left   = (FLOAT)prc->left;
    rf.top    = (FLOAT)prc->top;
    rf.right  = (FLOAT)prc->right;
    rf.bottom = (FLOAT)prc->bottom;
    if (flHover > 0.001f)
    {
      cf   = DwfColor(crFill);
      cf.a = flHover;
      pb   = NULL;
      (void)CCALL(pDC, CreateSolidColorBrush, &cf, NULL, &pb);
      if (pb)
      {
        CCALL(pDC, FillRectangle, &rf, (ID2D1Brush*)pb);
        DwfRelease((IUnknown**)&pb);
      }
    }
    cf = cfGlyph;
    pb = NULL;
    (void)CCALL(pDC, CreateSolidColorBrush, &cf, NULL, &pb);
    if (pb)
    {
      DwfDrawGlyph(f, pDC, (ID2D1Brush*)pb, prc, glyph);
      DwfRelease((IUnknown**)&pb);
    }
}

VOID WINAPI DwmFrameDrawChrome(DWMFRAME* f, HWND hwnd, void* pD2DContext, void* pD3DDevice, int cx, int cy)
{
    ID2D1DeviceContext*   pDC  = (ID2D1DeviceContext*)pD2DContext;
    ID3D11Device*         pDev = (ID3D11Device*)pD3DDevice;
    ID2D1SolidColorBrush* pBrush;
    int                   capH;
    BOOL                  fActive;
    BOOL                  fDark;
    D2D1_COLOR_F          colCap;
    D2D1_COLOR_F          colText;
    D2D1_COLOR_F          colGlyph;

    if (!f || !pDC || cx <= 0 || cy <= 0)
      return;

    capH    = (int)DwmFrameCaptionHeight(hwnd);
    fDark   = f->fDark;
    fActive = f->fWndActive;

    /* Crossfaded colors (uDWM's shared 160ms timeline, theme AND activation). */
    {
      BOOL  fDk1 = f->fAnim ? f->fDarkFrom   : fDark;
      BOOL  fAc1 = f->fAnim ? f->fActiveFrom : fActive;
      float t    = f->fAnim ? f->flAnimT     : 1.0f;

      colCap   = DwfLerp(DwfCaptionColor(fDk1, fAc1), DwfCaptionColor(fDark, fActive), t);
      colText  = DwfLerp(DwfTextColor(fDk1, fAc1),    DwfTextColor(fDark, fActive),    t);
      colGlyph = DwfLerp(DwfGlyphColor(fDk1, fAc1),   DwfGlyphColor(fDark, fActive),   t);
    }

    /* Caption band over the client content's top strip (the app lays out
     * below it via the viewport work-area inset). */
    {
      ID2D1SolidColorBrush* pCap = NULL;
      D2D1_RECT_F           rcCap;

      (void)CCALL(pDC, CreateSolidColorBrush, &colCap, NULL, &pCap);
      if (pCap)
      {
        rcCap.left   = 0.0f;
        rcCap.top    = 0.0f;
        rcCap.right  = (FLOAT)cx;
        rcCap.bottom = (FLOAT)capH;
        CCALL(pDC, FillRectangle, &rcCap, (ID2D1Brush*)pCap);
        DwfRelease((IUnknown**)&pCap);
      }
    }

    /* Caption system icon at the win32kfull DrawCaptionIcon slot. */
    {
      UINT dpi = DwfDpi(hwnd);
      int  iw  = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
      int  ih  = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
      int  ix  = (capH - iw) / 2 + 1;
      int  iy  = (capH - ih) / 2;

      DwfEnsureIcon(f, hwnd, pDev, pDC);
      if (f->pIconBmp)
      {
        D2D1_RECT_F ri;
        ri.left   = (FLOAT)ix;
        ri.top    = (FLOAT)iy;
        ri.right  = (FLOAT)(ix + iw);
        ri.bottom = (FLOAT)(iy + ih);
        CCALL(pDC, DrawBitmap, (ID2D1Bitmap*)f->pIconBmp, &ri, 1.0f,
              D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
      }
    }

    /* Caption title (system caption font); starts one caption-height in
     * (xxxDrawCaptionTemp: rc.left += capH for the icon slot). */
    {
      WCHAR szTitle[256];
      int   cch;

      szTitle[0] = 0;
      cch = GetWindowTextW(hwnd, szTitle, ARRAYSIZE(szTitle));
      if (f->pTextFormat && szTitle[0])
      {
        pBrush = NULL;
        (void)CCALL(pDC, CreateSolidColorBrush, &colText, NULL, &pBrush);
        if (pBrush)
        {
          D2D1_RECT_F rcText;
          rcText.left   = (FLOAT)capH;
          rcText.top    = 0.0f;
          rcText.right  = (FLOAT)cx;
          rcText.bottom = (FLOAT)capH;
          CCALL(pDC, DrawText, szTitle, (UINT32)cch, f->pTextFormat, &rcText,
                (ID2D1Brush*)pBrush, D2D1_DRAW_TEXT_OPTIONS_NONE, DWF_MEASURING_MODE_NATURAL);
          DwfRelease((IUnknown**)&pBrush);
        }
      }
    }

    /* Caption buttons: light/dark, Minimize, Maximize/Restore, Close. */
    {
      RECT rcClose, rcMax, rcMin, rcLD;

      if (DwfButtonRects(hwnd, &rcClose, &rcMax, &rcMin, &rcLD))
      {
        DwfDrawButton(f, pDC, &rcLD,    DWB_LIGHTDARK, (WCHAR)(fDark ? 0xE706 : 0xE708),          fDark, colGlyph, f->flBtnOpacity[DWB_LIGHTDARK]);
        DwfDrawButton(f, pDC, &rcMin,   DWB_MIN,       (WCHAR)0xE921,                             fDark, colGlyph, f->flBtnOpacity[DWB_MIN]);
        DwfDrawButton(f, pDC, &rcMax,   DWB_MAX,       (WCHAR)(IsZoomed(hwnd) ? 0xE923 : 0xE922), fDark, colGlyph, f->flBtnOpacity[DWB_MAX]);
        DwfDrawButton(f, pDC, &rcClose, DWB_CLOSE,     (WCHAR)0xE8BB,                             fDark, colGlyph, f->flBtnOpacity[DWB_CLOSE]);
      }
    }
}

/* ---- lifecycle ------------------------------------------------------------------------------------- */

DWMFRAME* WINAPI DwmFrameCreate(HWND hwnd)
{
    DWMFRAME* f = (DWMFRAME*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f)
      return NULL;
    f->hwnd       = hwnd;
    f->fDark      = TRUE;    /* app style is dark; seed before the first paint */
    f->fWndActive = TRUE;

    (void)DWriteCreateFactory(DWF_FACTORY_TYPE_SHARED, &DWF_IID_IDWriteFactory, (IUnknown**)&f->pDWrite);
    DwfCreateTextFormat(f);
    DwfCreateIconFormat(f);
    DwfApplyDwmFrame(hwnd);
    return f;
}

VOID WINAPI DwmFrameDestroy(DWMFRAME* f)
{
    if (!f)
      return;
    if (f->hwnd && f->fAnim)
      (void)KillTimer(f->hwnd, DWF_ANIM_TIMER_ID);
    DwfRelease((IUnknown**)&f->pTextFormat);
    DwfRelease((IUnknown**)&f->pIconFormat);
    DwfRelease((IUnknown**)&f->pIconBmp);
    DwfRelease((IUnknown**)&f->pDWrite);
    HeapFree(GetProcessHeap(), 0, f);
}

/* ---- geometry (imguiapp / ImmersiveWindow shape, verbatim) ----------------------------------------- */

UINT WINAPI DwmFrameNCCalcSize(HWND hwnd, BOOL fCalcValidRects, NCCALCSIZE_PARAMS* lpcsp)
{
    if (!fCalcValidRects)
      return 0;

    /* CLIENT == WINDOW, exactly (dwmframex topology).  No nonclient pixels
     * exist at all: the window origin and the client origin coincide, the
     * composed content covers the full window by construction, and the
     * resize ring is synthesized INSIDE the client edges by the hit test.
     * This removes every dependency on where the composition visual anchors
     * for a client-inset window — the two candidate origins are the same
     * point.  rgrc[1] = rgrc[2] is the reference's "lie to dwm"; maximized,
     * the client is the monitor work area EXACTLY (pairs with
     * DwmFrameGetMinMaxInfo). */
    lpcsp->rgrc[1] = lpcsp->rgrc[2];
    if (IsZoomed(hwnd))
    {
      MONITORINFO mi = { sizeof(mi) };
      if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        lpcsp->rgrc[0] = mi.rcWork;
    }
    return 0;
}

VOID WINAPI DwmFrameGetMinMaxInfo(HWND hwnd, MINMAXINFO* lpMinMaxInfo)
{
    SIZE          border;
    UINT          dpi;
    int           button_width;
    int           caption;
    GUITHREADINFO gti = { sizeof(gti) };
    MONITORINFO   mi  = { sizeof(mi) };

    /* Min track keeps the caption anatomy intact; maximize and max track are
     * EXACTLY the nearest monitor's work area at its work origin. */
    DwfWindowBorders(hwnd, &border);
    dpi          = DwfDpi(hwnd);
    button_width = MulDiv(47, (int)dpi, 96);
    caption      = (int)DwmFrameCaptionHeight(hwnd);
    lpMinMaxInfo->ptMinTrackSize.x = caption + 4 * button_width + 2 * border.cx;
    lpMinMaxInfo->ptMinTrackSize.y = caption + border.cy;

    /* Mid move-size loop: leave the max fields alone (reference behavior). */
    if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndMoveSize == hwnd)
      return;

    if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
    {
      lpMinMaxInfo->ptMaxPosition.x  = mi.rcWork.left - mi.rcMonitor.left;
      lpMinMaxInfo->ptMaxPosition.y  = mi.rcWork.top - mi.rcMonitor.top;
      lpMinMaxInfo->ptMaxTrackSize.x = mi.rcWork.right - mi.rcWork.left;
      lpMinMaxInfo->ptMaxTrackSize.y = mi.rcWork.bottom - mi.rcWork.top;
      lpMinMaxInfo->ptMaxSize        = lpMinMaxInfo->ptMaxTrackSize;
    }
}

/* ---- hit test (FindNCHit region order, CLIENT coordinates) ----------------------------------------- */

UINT WINAPI DwmFrameHitTest(DWMFRAME* f, HWND hwnd, int x, int y)
{
    POINT    pt;
    RECT     client;
    RECT     rcClose, rcMax, rcMin, rcLD;
    RECT     rcIcon;
    SIZE     border;
    LONG_PTR style;
    int      capH;
    int      row;
    int      col;
    BOOL     fSizable;

    UNREFERENCED_PARAMETER(f);

    pt.x = x;
    pt.y = y;
    ScreenToClient(hwnd, &pt);
    GetClientRect(hwnd, &client);
    DwfWindowBorders(hwnd, &border);
    capH     = (int)DwmFrameCaptionHeight(hwnd);
    style    = GetWindowLongPtr(hwnd, GWL_STYLE);
    fSizable = (0 != (style & WS_THICKFRAME)) && !IsZoomed(hwnd);

    if (DwfButtonRects(hwnd, &rcClose, &rcMax, &rcMin, &rcLD))
    {
      if (PtInRect(&rcLD, pt))    return HTLIGHTDARKBTN;
      if (PtInRect(&rcMin, pt))   return HTMINBUTTON;
      if (PtInRect(&rcMax, pt))   return HTMAXBUTTON;
      if (PtInRect(&rcClose, pt)) return HTCLOSE;
    }

    rcIcon.left   = 0;
    rcIcon.top    = 0;
    rcIcon.right  = capH;
    rcIcon.bottom = capH;
    if (PtInRect(&rcIcon, pt))
      return HTSYSMENU;

    if (fSizable)
    {
      /* Client == window: the whole resize ring lives INSIDE the client
       * edges (dwmframex DwfHitTest shape). */
      row = 1;
      col = 1;
      if (pt.y < border.cy)                        row = 0;
      else if (pt.y >= client.bottom - border.cy)  row = 2;
      if (pt.x < border.cx)                        col = 0;
      else if (pt.x >= client.right - border.cx)   col = 2;

      if (0 == row)
      {
        if (0 == col) return HTTOPLEFT;
        if (2 == col) return HTTOPRIGHT;
        return HTTOP;
      }
      if (2 == row)
      {
        if (0 == col) return HTBOTTOMLEFT;
        if (2 == col) return HTBOTTOMRIGHT;
        return HTBOTTOM;
      }
      if (0 == col) return HTLEFT;
      if (2 == col) return HTRIGHT;
    }

    if (pt.y < capH)
      return HTCAPTION;
    return HTCLIENT;
}

/* ---- transitions + button flow ---------------------------------------------------------------------
 * State-only: arm the 160ms timer and invalidate; the window control renders
 * full app frames (chrome included) through its one present path. */

static void DwfBeginTransition(DWMFRAME* f, HWND hwnd, BOOL fDarkTo, BOOL fActiveTo)
{
    if ((f->fDark == fDarkTo) && (f->fWndActive == fActiveTo))
      return;
    f->fDarkFrom   = f->fDark;
    f->fActiveFrom = f->fWndActive;
    f->fDark       = fDarkTo;
    f->fWndActive  = fActiveTo;
    f->dwAnimStart = GetTickCount();
    f->flAnimT     = 0.0f;
    f->fAnim       = TRUE;
    (void)SetTimer(hwnd, DWF_ANIM_TIMER_ID, DWF_ANIM_INTERVAL, NULL);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void DwfKickAnim(DWMFRAME* f, HWND hwnd)
{
    UNREFERENCED_PARAMETER(f);
    (void)SetTimer(hwnd, DWF_ANIM_TIMER_ID, DWF_ANIM_INTERVAL, NULL);
    InvalidateRect(hwnd, NULL, FALSE);
}

static float DwfBtnTarget(DWMFRAME* f, int id)
{
    if (f->idPressed == id)
      return 1.0f;
    if ((f->idHot == id) && (f->idPressed == DWB_NONE))
      return 1.0f;
    return 0.0f;
}

static int DwfButtonFromHit(WPARAM hit)
{
    switch (hit)
    {
    case HTCLOSE:        return DWB_CLOSE;
    case HTMAXBUTTON:    return DWB_MAX;
    case HTMINBUTTON:    return DWB_MIN;
    case HTLIGHTDARKBTN: return DWB_LIGHTDARK;
    default:             return DWB_NONE;
    }
}

#ifndef TME_NONCLIENT
#define TME_NONCLIENT 0x00000010
#endif

static void DwfTrackLeave(DWMFRAME* f, HWND hwnd)
{
    TRACKMOUSEEVENT tme;

    if (f->fTracking)
      return;
    ZeroMemory(&tme, sizeof(tme));
    tme.cbSize    = (DWORD)sizeof(tme);
    tme.dwFlags   = TME_LEAVE | TME_NONCLIENT;
    tme.hwndTrack = hwnd;
    if (TrackMouseEvent(&tme))
      f->fTracking = TRUE;
}

static void DwfButtonAction(DWMFRAME* f, HWND hwnd, int id)
{
    switch (id)
    {
    case DWB_MIN:       (void)PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0); break;
    case DWB_MAX:       (void)PostMessageW(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0); break;
    case DWB_CLOSE:     (void)PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0); break;
    case DWB_LIGHTDARK: DwfBeginTransition(f, hwnd, !f->fDark, f->fWndActive); break;
    default:            break;
    }
}

BOOL WINAPI DwmFrameButtonPressActive(DWMFRAME* f)
{
    return f && f->fCapturing;
}

VOID WINAPI DwmFrameOnNCActivate(DWMFRAME* f, HWND hwnd, BOOL fActive)
{
    if (f)
      DwfBeginTransition(f, hwnd, f->fDark, fActive);
}

VOID WINAPI DwmFrameOnActivate(DWMFRAME* f, HWND hwnd, UINT state)
{
    if (!f)
      return;
    /* The documented custom-frame contract: (re-)report the frame dressing
     * in response to WM_ACTIVATE so it settles. */
    DwfApplyDwmFrame(hwnd);
    if (!f->fFirstActivate)
    {
      f->fFirstActivate = TRUE;
      (void)SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    DwfBeginTransition(f, hwnd, f->fDark, state != WA_INACTIVE);
}

BOOL WINAPI DwmFrameOnTimer(DWMFRAME* f, HWND hwnd, UINT id)
{
    BOOL  fMore = FALSE;
    float step  = (float)DWF_ANIM_INTERVAL / (float)DWF_ANIM_DURATION;
    int   i;

    if (!f || (UINT_PTR)id != DWF_ANIM_TIMER_ID)
      return FALSE;

    if (f->fAnim)
    {
      DWORD el = GetTickCount() - f->dwAnimStart;
      float t  = (float)el / (float)DWF_ANIM_DURATION;
      if (t >= 1.0f) { t = 1.0f; f->fAnim = FALSE; }
      f->flAnimT = t;
      if (f->fAnim)
        fMore = TRUE;
    }

    for (i = DWB_LIGHTDARK; i <= DWB_CLOSE; ++i)
    {
      float tgt = DwfBtnTarget(f, i);
      float cur = f->flBtnOpacity[i];
      if (cur < tgt)      { cur += step; if (cur > tgt) cur = tgt; }
      else if (cur > tgt) { cur -= step; if (cur < tgt) cur = tgt; }
      f->flBtnOpacity[i] = cur;
      if (cur != tgt)
        fMore = TRUE;
    }

    f->flAnimT = f->fAnim ? f->flAnimT : 1.0f;
    InvalidateRect(hwnd, NULL, FALSE);   /* the paint path renders the frame */
    if (!fMore)
      (void)KillTimer(hwnd, DWF_ANIM_TIMER_ID);
    return TRUE;
}

VOID WINAPI DwmFrameOnNCMouseMove(DWMFRAME* f, HWND hwnd, UINT codeHitTest)
{
    int id;

    if (!f)
      return;
    id = DwfButtonFromHit((WPARAM)codeHitTest);
    DwfTrackLeave(f, hwnd);
    if (f->idHot != id)
    {
      f->idHot = id;
      DwfKickAnim(f, hwnd);
    }
}

VOID WINAPI DwmFrameOnNCMouseLeave(DWMFRAME* f, HWND hwnd)
{
    if (!f)
      return;
    f->fTracking = FALSE;
    if (f->idHot != DWB_NONE)
    {
      f->idHot = DWB_NONE;
      DwfKickAnim(f, hwnd);
    }
}

BOOL WINAPI DwmFrameOnNCButtonDown(DWMFRAME* f, HWND hwnd, UINT codeHitTest)
{
    int id;

    if (!f)
      return FALSE;
    id = DwfButtonFromHit((WPARAM)codeHitTest);
    if (DWB_NONE == id)
      return FALSE;
    f->idPressed  = id;
    f->idHot      = id;
    f->fCapturing = TRUE;
    (void)SetCapture(hwnd);
    DwfKickAnim(f, hwnd);
    return TRUE;
}

BOOL WINAPI DwmFrameOnMouseMove(DWMFRAME* f, HWND hwnd, int x, int y)
{
    POINT pt;
    int   id;

    if (!f || !f->fCapturing)
      return FALSE;
    pt.x = x;
    pt.y = y;
    (void)ClientToScreen(hwnd, &pt);
    id = DwfButtonFromHit((WPARAM)DwmFrameHitTest(f, hwnd, pt.x, pt.y));
    if (f->idHot != id)
    {
      f->idHot = id;
      DwfKickAnim(f, hwnd);
    }
    return TRUE;
}

BOOL WINAPI DwmFrameOnLButtonUp(DWMFRAME* f, HWND hwnd, int x, int y)
{
    POINT pt;
    int   id;
    int   pressed;

    if (!f || !f->fCapturing)
      return FALSE;
    pressed = f->idPressed;
    pt.x = x;
    pt.y = y;
    (void)ClientToScreen(hwnd, &pt);
    id = DwfButtonFromHit((WPARAM)DwmFrameHitTest(f, hwnd, pt.x, pt.y));
    f->fCapturing = FALSE;
    f->idPressed  = DWB_NONE;
    (void)ReleaseCapture();
    DwfKickAnim(f, hwnd);
    if ((DWB_NONE != pressed) && (pressed == id))
      DwfButtonAction(f, hwnd, pressed);
    return TRUE;
}

VOID WINAPI DwmFrameOnCaptureChanged(DWMFRAME* f, HWND hwnd)
{
    if (!f || !f->fCapturing)
      return;
    f->fCapturing = FALSE;
    f->idPressed  = DWB_NONE;
    DwfKickAnim(f, hwnd);
}
