/*
 * dwmframe.c -- WGLWindow caption chrome, ported from Win32X dwmframex.c (the in-process uDWM caption
 * compositor), drawn with OpenGL.
 *
 * This module owns the CHROME: caption band, system icon, title (system caption font), the four caption
 * buttons (light/dark, minimize, maximize/restore, close) with uDWM's 160ms hover/press/theme/activation
 * crossfades, the FindNCHit-order hit test, WM_NCCALCSIZE / WM_GETMINMAXINFO geometry, and the
 * capture-tracked button press flow (xxxTrackCaptionButton shape).
 *
 * It does NOT own a pipeline: drawing is plain OpenGL into the window's GL frame (the pbuffer), after the
 * client content and before the present -- chrome and client content are ONE atomic present.  Band and
 * button highlights are untextured quads; the title, the button glyphs (Segoe Fluent Icons / Segoe MDL2
 * Assets) and the system icon are GDI-rasterized into cached GL textures -- the same asset-prep role GDI
 * plays for dwmframex's own icon path.  No D2D, no DWrite.
 *
 * Repaint policy: input/theme changes here only update state, arm the 160ms timer, and invalidate the
 * window; the window control renders full app frames (chrome included) through its one present path.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <gl/gl.h>

#include "dwmframe.h"

#pragma comment(lib, "opengl32")

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT       0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE  0x812F
#endif

enum DWF_BTN { DWB_NONE = 0, DWB_LIGHTDARK, DWB_MIN, DWB_MAX, DWB_CLOSE };

/* Glyph texture slots (Segoe Fluent Icons codepoints, dwmframex's set). */
enum DWF_GLYPH { DWFG_SUN, DWFG_MOON, DWFG_MIN, DWFG_MAX, DWFG_RESTORE, DWFG_CLOSE, DWFG_COUNT };
static const WCHAR c_dwfGlyphCp[DWFG_COUNT] = { 0xE706, 0xE708, 0xE921, 0xE922, 0xE923, 0xE8BB };

#define DWF_DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWF_DWMWCP_ROUND                   2

/* Glyph masks rasterize at 4x and draw at 1:1 scale-down: GDI grayscale AA
 * at caption-glyph sizes (~13px symbol font) has too few coverage levels —
 * visible quantization; the supersample restores smooth edges. */
#define DWF_GLYPH_SS 4

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

typedef struct DWFCOLOR { float r; float g; float b; float a; } DWFCOLOR;
typedef struct DWFTEX   { GLuint id; int w; int h; } DWFTEX;

/* ---- per-window chrome state (the reference's g_dwf, pipeline-less) ------------------------------ */
struct DWMFRAME
{
    HWND     hwnd;
    DWMFRAMETHEMEPROC pfnTheme;   /* app theme flip on the light/dark commit */
    BOOL     fIconTried;
    BOOL     fFirstActivate;
    int      idHot;
    int      idPressed;
    BOOL     fTracking;
    BOOL     fCapturing;
    BOOL     fDark;
    BOOL     fWndActive;
    BOOL     fAnim;
    DWORD    dwAnimStart;
    BOOL     fDarkFrom;
    BOOL     fActiveFrom;
    float    flAnimT;
    float    flBtnOpacity[5];
    /* GL chrome assets, owned by the window's GL context (current at every
     * DrawChrome/Destroy per the header contract). */
    UINT     assetDpi;                /* dpi the glyph/title assets were rasterized at */
    DWFTEX   texIcon;                 /* system icon, BGRA straight alpha */
    DWFTEX   texGlyph[DWFG_COUNT];    /* button glyphs, alpha masks */
    DWFTEX   texTitle;                /* caption title: opaque ClearType BGRA strip */
    WCHAR    szTitle[256];            /* text texTitle was rasterized from */
    COLORREF crTitleText;             /* colors baked into texTitle (ClearType */
    COLORREF crTitleBack;             /* needs the real background) */
    /* Cached title rasterization resources: one memory DC, a grow-only DIB,
     * the caption font for assetDpi, and the reused GL texture id — the
     * theme/activation crossfade re-rasterizes per tick, which must not
     * create/destroy GDI or GL objects. */
    HDC      titleDC;
    HBITMAP  titleDib;
    HBITMAP  titleDibPrev;
    void*    titleBits;
    int      titleDibW;
    int      titleDibH;
    HFONT    titleFont;
};

/* ---- helpers -------------------------------------------------------------------------------------- */

static DWFCOLOR DwfColor(COLORREF cr)
{
    DWFCOLOR c;
    c.r = (float)GetRValue(cr) / 255.0f;
    c.g = (float)GetGValue(cr) / 255.0f;
    c.b = (float)GetBValue(cr) / 255.0f;
    c.a = 1.0f;
    return c;
}

static DWFCOLOR DwfLerp(DWFCOLOR a, DWFCOLOR b, float t)
{
    DWFCOLOR c;
    c.r = a.r + (b.r - a.r) * t;
    c.g = a.g + (b.g - a.g) * t;
    c.b = a.b + (b.b - a.b) * t;
    c.a = a.a + (b.a - a.a) * t;
    return c;
}

static DWFCOLOR DwfTextColor(BOOL fDark, BOOL fActive)
{
    DWFCOLOR c = DwfColor(fDark ? RGB(255, 255, 255) : RGB(0, 0, 0));
    if (!fActive)
      c.a = 0.60f;
    return c;
}

static DWFCOLOR DwfGlyphColor(BOOL fDark, BOOL fActive)
{
    return DwfColor(fActive ? (fDark ? RGB(255, 255, 255) : RGB(0, 0, 0))
                            : (fDark ? RGB(0xAA, 0xAA, 0xAA) : RGB(0x64, 0x64, 0x64)));
}

static DWFCOLOR DwfCaptionColor(BOOL fDark, BOOL fActive)
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
 * right-aligned, Close..light/dark right-to-left (uDWM UpdateNCAreaButton).
 * cxClient is the caller's DRIVEN client width — during the pre-geometry
 * resize repaint GetClientRect still reports the old size, which would park
 * the cluster at the stale right edge. */
static int DwfButtonRects(HWND hwnd, int cxClient, RECT* prcClose, RECT* prcMax, RECT* prcMin, RECT* prcLD)
{
    UINT dpi  = DwfDpi(hwnd);
    int  capH = (int)DwmFrameCaptionHeight(hwnd);
    int  btnW = MulDiv(47, (int)dpi, 96);
    int  r;

    r = cxClient;
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

/* Re-enable the DWM-drawn frame the NC removal stripped, the dwmframex way:
 * a 1px top sheet-of-glass — the SMALLEST extension that makes DWM render
 * the drop shadow, the window border, and report
 * DWMWA_EXTENDED_FRAME_BOUNDS; the opaque composed face (client == window,
 * alpha forced to 1) paints over the 1px.  The old {1,1,-1,1} + blur-behind
 * dressing was for content that did NOT cover the window: any -1 margin is
 * a FULL sheet of glass, which suppresses the standard frame shadow/border
 * and leaves the window a flat floating slab.  Idempotent; must be (re-)
 * applied in response to WM_ACTIVATE to settle; dwmapi loaded on first
 * use. */
static void DwfApplyDwmFrame(HWND hwnd)
{
    union { FARPROC fp; PFN_DWF_EXTEND ex; PFN_DWF_SETATTR sa; PFN_DWF_BLURBEHIND bb; } u;
    DWF_MARGINS m;

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
      /* {1,1,1,1}: the 1px all-sides extension that turns on the DWM drop
       * shadow.  NEVER sheet-of-glass (-1) margins — solid backdrop
       * material on Win11. */
      m.cxLeft = 1; m.cxRight = 1; m.cyTop = 1; m.cyBottom = 1;
      (void)g_dwfExtend(hwnd, &m);
    }
    if (g_dwfBlurBehind)
    {
      /* Blur-behind with an EMPTY region: suppresses DWM's frame/backdrop
       * material so the invisible-border strips (window minus client) are
       * genuinely transparent — the visible face ends at the client.
       * Without this the strips render as a solid ring. */
      DWF_BLURBEHIND bb;
      bb.dwFlags                = DWF_BB_ENABLE | DWF_BB_BLURREGION;
      bb.fEnable                = TRUE;
      bb.hRgnBlur               = CreateRectRgn(0, 0, -1, -1);
      bb.fTransitionOnMaximized = FALSE;
      (void)g_dwfBlurBehind(hwnd, &bb);
      if (bb.hRgnBlur)
        (void)DeleteObject(bb.hRgnBlur);
    }
}

/* ---- GL texture assets ------------------------------------------------------------------------------
 * Rasterization is GDI into a DIB (asset prep, once per text/dpi change);
 * every per-frame draw is GL. */

static void DwfFreeTex(DWFTEX* pTex)
{
    if (pTex->id)
      glDeleteTextures(1, &pTex->id);
    pTex->id = 0;
    pTex->w  = 0;
    pTex->h  = 0;
}

static void DwfTexFromAlpha(DWFTEX* pTex, const BYTE* pAlpha, int w, int h)
{
    glGenTextures(1, &pTex->id);
    if (!pTex->id)
      return;
    glBindTexture(GL_TEXTURE_2D, pTex->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, w, h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, pAlpha);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    pTex->w = w;
    pTex->h = h;
}

/* White-on-black grayscale-AA GDI text, rasterized at DWF_GLYPH_SS x the
 * target size (the caller's font is already supersampled) and box-filtered
 * down on the CPU to a 1:1 GL_ALPHA mask: every output pixel is the exact
 * SSxSS coverage average.  (GPU LINEAR minification at 4:1 reads only 4 of
 * the 16 source texels — undersampling, not smoothing.)  The draw tints the
 * mask with the vertex color (GL_MODULATE). */
static BOOL DwfRasterizeTextAlpha(HFONT hFont, LPCWSTR psz, int cch, DWFTEX* pTex)
{
    HDC        hdcScreen;
    HDC        hdcMem;
    HFONT      hOldFont;
    HBITMAP    hDib;
    HBITMAP    hOldBmp;
    BITMAPINFO bmi;
    void*      pBits;
    BYTE*      pAlpha;
    SIZE       ext;
    int        w;
    int        h;
    int        lw;
    int        lh;
    BOOL       ok = FALSE;

    DwfFreeTex(pTex);
    if (!hFont || !psz || cch <= 0)
      return FALSE;

    hdcScreen = GetDC(NULL);
    hdcMem    = CreateCompatibleDC(hdcScreen);
    if (hdcScreen)
      (void)ReleaseDC(NULL, hdcScreen);
    if (!hdcMem)
      return FALSE;

    hOldFont = (HFONT)SelectObject(hdcMem, hFont);
    ext.cx = 0;
    ext.cy = 0;
    (void)GetTextExtentPoint32W(hdcMem, psz, cch, &ext);
    w = (int)ext.cx;
    h = (int)ext.cy;
    if (w < DWF_GLYPH_SS) w = DWF_GLYPH_SS;
    if (h < DWF_GLYPH_SS) h = DWF_GLYPH_SS;
    if (w > 8192) w = 8192;
    if (h > 1024) h = 1024;
    w -= w % DWF_GLYPH_SS;                      /* exact SSxSS blocks */
    h -= h % DWF_GLYPH_SS;
    lw = w / DWF_GLYPH_SS;
    lh = h / DWF_GLYPH_SS;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;           /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    pBits = NULL;
    hDib  = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (hDib && pBits)
    {
      hOldBmp = (HBITMAP)SelectObject(hdcMem, hDib);
      ZeroMemory(pBits, (SIZE_T)w * (SIZE_T)h * 4u);
      (void)SetBkMode(hdcMem, TRANSPARENT);
      (void)SetTextColor(hdcMem, RGB(255, 255, 255));
      (void)TextOutW(hdcMem, 0, 0, psz, cch);
      (void)GdiFlush();
      (void)SelectObject(hdcMem, hOldBmp);

      pAlpha = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)lw * (SIZE_T)lh);
      if (pAlpha)
      {
        const BYTE* src = (const BYTE*)pBits;
        int         ox;
        int         oy;

        for (oy = 0; oy < lh; ++oy)
        {
          for (ox = 0; ox < lw; ++ox)
          {
            UINT sum = 0;
            int  sx;
            int  sy;

            for (sy = 0; sy < DWF_GLYPH_SS; ++sy)
            {
              const BYTE* row = src + (((SIZE_T)(oy * DWF_GLYPH_SS + sy) * (SIZE_T)w +
                                        (SIZE_T)(ox * DWF_GLYPH_SS)) * 4u);
              for (sx = 0; sx < DWF_GLYPH_SS; ++sx)
                sum += row[sx * 4 + 1];         /* green carries the grayscale coverage */
            }
            pAlpha[(SIZE_T)oy * (SIZE_T)lw + (SIZE_T)ox] =
                (BYTE)(sum / (DWF_GLYPH_SS * DWF_GLYPH_SS));
          }
        }
        DwfTexFromAlpha(pTex, pAlpha, lw, lh);
        HeapFree(GetProcessHeap(), 0, pAlpha);
        ok = (pTex->id != 0);
      }
    }

    if (hDib)
      (void)DeleteObject(hDib);
    (void)SelectObject(hdcMem, hOldFont);
    (void)DeleteDC(hdcMem);
    return ok;
}

/* System caption font at the window's dpi (the same font uDWM titles with),
 * at native size with ClearType quality: the title rasterizes as a full-
 * color strip against the live caption color (subpixel AA cannot ride an
 * alpha mask — it needs per-channel coverage over a known opaque
 * background). */
static HFONT DwfCreateCaptionFont(UINT dpi)
{
    static NONCLIENTMETRICSW ncm;   /* ~500 bytes: off-stack, GUI thread only (reference remedy) */

    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = (DWORD)sizeof(ncm);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, (UINT)sizeof(ncm), &ncm, 0, dpi) &&
        !SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, (UINT)sizeof(ncm), &ncm, 0))
      return NULL;
    ncm.lfCaptionFont.lfQuality = CLEARTYPE_QUALITY;
    return CreateFontIndirectW(&ncm.lfCaptionFont);
}

/* ClearType caption title: crText on crBack, opaque BGRA strip, through the
 * frame's CACHED resources (memory DC, grow-only DIB, per-dpi font, reused
 * GL texture id) — the theme/activation crossfade re-rasterizes per tick.
 * GDI writes 0 into the DIB alpha channel; forced to 0xFF for the upload
 * (the strip draws as an opaque quad over the identically-colored band). */
static BOOL DwfRasterizeTitle(DWMFRAME* f, LPCWSTR psz, int cch, COLORREF crText, COLORREF crBack)
{
    HFONT   hOldFont;
    HBITMAP hOldBmp;
    SIZE    ext;
    RECT    rc;
    int     w;
    int     h;
    int     y;

    if (!psz || cch <= 0)
      return FALSE;

    if (!f->titleDC)
      f->titleDC = CreateCompatibleDC(NULL);
    if (!f->titleDC)
      return FALSE;
    if (!f->titleFont)
      f->titleFont = DwfCreateCaptionFont(f->assetDpi ? f->assetDpi : 96u);
    if (!f->titleFont)
      return FALSE;

    hOldFont = (HFONT)SelectObject(f->titleDC, f->titleFont);
    ext.cx = 0;
    ext.cy = 0;
    (void)GetTextExtentPoint32W(f->titleDC, psz, cch, &ext);
    w = (int)ext.cx;
    h = (int)ext.cy;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > 4096) w = 4096;
    if (h > 256)  h = 256;

    /* Grow-only DIB (stride = titleDibW; the upload declares it via
     * GL_UNPACK_ROW_LENGTH). */
    if (!f->titleDib || w > f->titleDibW || h > f->titleDibH)
    {
      BITMAPINFO bmi;
      void*      pBits = NULL;
      HBITMAP    hDib;
      int        dw = (w > f->titleDibW) ? w : f->titleDibW;
      int        dh = (h > f->titleDibH) ? h : f->titleDibH;

      dw = (dw + 63) & ~63;   /* grow in 64px steps */
      dh = (dh + 15) & ~15;

      ZeroMemory(&bmi, sizeof(bmi));
      bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
      bmi.bmiHeader.biWidth       = dw;
      bmi.bmiHeader.biHeight      = -dh;        /* top-down */
      bmi.bmiHeader.biPlanes      = 1;
      bmi.bmiHeader.biBitCount    = 32;
      bmi.bmiHeader.biCompression = BI_RGB;
      hDib = CreateDIBSection(f->titleDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
      if (!hDib || !pBits)
      {
        if (hDib)
          (void)DeleteObject(hDib);
        (void)SelectObject(f->titleDC, hOldFont);
        return FALSE;
      }
      if (f->titleDibPrev)
        (void)SelectObject(f->titleDC, f->titleDibPrev);
      if (f->titleDib)
        (void)DeleteObject(f->titleDib);
      f->titleDib     = hDib;
      f->titleBits    = pBits;
      f->titleDibW    = dw;
      f->titleDibH    = dh;
      f->titleDibPrev = (HBITMAP)SelectObject(f->titleDC, f->titleDib);
    }

    rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
    (void)SetBkColor(f->titleDC, crBack);
    (void)SetBkMode(f->titleDC, OPAQUE);
    (void)SetTextColor(f->titleDC, crText);
    (void)ExtTextOutW(f->titleDC, 0, 0, ETO_OPAQUE, &rc, psz, (UINT)cch, NULL);
    (void)GdiFlush();
    (void)SelectObject(f->titleDC, hOldFont);

    for (y = 0; y < h; ++y)
    {
      BYTE* p = (BYTE*)f->titleBits + (SIZE_T)y * (SIZE_T)f->titleDibW * 4u;
      int   x;
      for (x = 0; x < w; ++x)
        p[x * 4 + 3] = 0xFF;
    }

    if (!f->texTitle.id)
    {
      glGenTextures(1, &f->texTitle.id);
      if (!f->texTitle.id)
        return FALSE;
      glBindTexture(GL_TEXTURE_2D, f->texTitle.id);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
      glBindTexture(GL_TEXTURE_2D, f->texTitle.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, f->titleDibW);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, f->titleBits);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    f->texTitle.w = w;
    f->texTitle.h = h;
    return TRUE;
}

/* Icon glyph font: Segoe Fluent Icons (Win11) / Segoe MDL2 Assets (Win10) --
 * the same glyphs uDWM bakes into its button atlas.  pxSize is the exact
 * pixel size to rasterize at (callers pass the supersampled size). */
static HFONT DwfCreateIconFont(int pxSize)
{
    static const WCHAR* faces[2] = { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" };
    HDC   hdc;
    int   size;
    int   i;
    HFONT hFont;

    size = pxSize;
    if (size < 8)
      size = 8;

    hdc = GetDC(NULL);
    for (i = 0; i < 2; ++i)
    {
      hFont = CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, faces[i]);
      if (hFont && hdc)
      {
        WCHAR face[LF_FACESIZE];
        HFONT hOld = (HFONT)SelectObject(hdc, hFont);
        face[0] = 0;
        (void)GetTextFaceW(hdc, LF_FACESIZE, face);
        (void)SelectObject(hdc, hOld);
        if (0 == lstrcmpiW(face, faces[i]))
          break;
        (void)DeleteObject(hFont);
        hFont = NULL;
      }
      if (hFont)
        break;
    }
    if (hdc)
      (void)ReleaseDC(NULL, hdc);
    return hFont;
}

/* (Re)build the glyph + title mask textures whenever the dpi or the window
 * text changed; cheap no-op otherwise. */
static void DwfEnsureChromeAssets(DWMFRAME* f, HWND hwnd)
{
    UINT dpi = DwfDpi(hwnd);
    int  i;

    if (dpi != f->assetDpi)
    {
      for (i = 0; i < DWFG_COUNT; ++i)
        DwfFreeTex(&f->texGlyph[i]);
      DwfFreeTex(&f->texTitle);
      DwfFreeTex(&f->texIcon);
      if (f->titleFont)
      {
        (void)DeleteObject(f->titleFont);   /* caption font is dpi-sized */
        f->titleFont = NULL;
      }
      f->fIconTried = FALSE;
      f->szTitle[0] = 1;   /* != any real title: forces the re-rasterize */
      f->szTitle[1] = 0;
      f->assetDpi   = dpi;
    }

    if (!f->texGlyph[DWFG_CLOSE].id)
    {
      /* Native uDWM caption glyph size: a 10-DIP em (NOT a caption-height
       * fraction — the SM-stack caption is taller than the shell's 30-DIP
       * design height and inflates the glyphs).  Rasterized supersampled;
       * the box filter reduces to 1:1. */
      HFONT hFont = DwfCreateIconFont(MulDiv(10, (int)dpi, 96) * DWF_GLYPH_SS);
      if (hFont)
      {
        for (i = 0; i < DWFG_COUNT; ++i)
          (void)DwfRasterizeTextAlpha(hFont, &c_dwfGlyphCp[i], 1, &f->texGlyph[i]);
        (void)DeleteObject(hFont);
      }
    }
}

/* Caption icon: the window's icon rasterized by GDI at EXACTLY the small-
 * icon metric for the window's dpi (DrawIconEx picks/scales the appropriate
 * frame), uploaded as a BGRA GL texture drawn 1:1 — a GPU LINEAR downscale
 * from a 256px frame to ~16px has no mips and shimmers into mush. */
static void DwfEnsureIcon(DWMFRAME* f, HWND hwnd)
{
    HICON      hIcon;
    BITMAPINFO bmi;
    HDC        hdcScreen;
    HDC        hdcMem;
    HBITMAP    hDib;
    HBITMAP    hOld;
    void*      pBits;
    UINT       dpi;
    int        iw;
    int        ih;

    if (f->texIcon.id || f->fIconTried)
      return;
    f->fIconTried = TRUE;

    hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0);
    if (!hIcon) hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
    if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
    if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
    if (!hIcon) hIcon = LoadIconW(NULL, IDI_APPLICATION);
    if (!hIcon) return;

    dpi = DwfDpi(hwnd);
    iw  = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    ih  = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
    if (iw < 8)  iw = 8;
    if (ih < 8)  ih = 8;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = iw;
    bmi.bmiHeader.biHeight      = -ih;          /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    pBits     = NULL;
    hdcScreen = GetDC(NULL);
    hdcMem    = CreateCompatibleDC(hdcScreen);
    hDib      = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0u);
    if (hdcScreen) (void)ReleaseDC(NULL, hdcScreen);

    if (hdcMem && hDib && pBits)
    {
      hOld = (HBITMAP)SelectObject(hdcMem, hDib);
      ZeroMemory(pBits, (SIZE_T)iw * (SIZE_T)ih * 4u);
      (void)DrawIconEx(hdcMem, 0, 0, hIcon, iw, ih, 0u, NULL, DI_NORMAL);
      (void)GdiFlush();
      (void)SelectObject(hdcMem, hOld);

      glGenTextures(1, &f->texIcon.id);
      if (f->texIcon.id)
      {
        glBindTexture(GL_TEXTURE_2D, f->texIcon.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iw, ih, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pBits);
        f->texIcon.w = iw;
        f->texIcon.h = ih;
      }
    }

    if (hDib)   (void)DeleteObject(hDib);
    if (hdcMem) (void)DeleteDC(hdcMem);
}

/* ---- GL drawing ------------------------------------------------------------------------------------ */

static void DwfFillRectGL(float x0, float y0, float x1, float y1, DWFCOLOR c)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(c.r, c.g, c.b, c.a);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

static void DwfDrawTexGL(const DWFTEX* pTex, float x, float y, float w, float h, DWFCOLOR c)
{
    if (!pTex->id)
      return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, pTex->id);
    glColor4f(c.r, c.g, c.b, c.a);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x,     y);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y + h);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x,     y + h);
    glEnd();
}

/* One caption button: hover/press highlight (alpha-faded by flHover, the
 * per-button 160ms opacity) + the glyph mask centered in the cell.  Close's
 * glyph cross-fades to white as its red highlight fades in. */
static void DwfDrawButton(DWMFRAME* f, const RECT* prc, int id, const DWFTEX* pGlyph,
                          BOOL fDark, DWFCOLOR cfGlyph, float flHover)
{
    BOOL     fPressed = (f->idPressed == id);
    COLORREF crFill;
    DWFCOLOR cf;

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

    if (flHover > 0.001f)
    {
      cf   = DwfColor(crFill);
      cf.a = flHover;
      DwfFillRectGL((float)prc->left, (float)prc->top, (float)prc->right, (float)prc->bottom, cf);
    }

    if (pGlyph && pGlyph->id)
    {
      /* The mask is already box-filtered to 1:1; draw at native size,
       * integer-snapped (half-pixel placement under LINEAR doubles edges). */
      int gx = prc->left + ((prc->right - prc->left) - pGlyph->w) / 2;
      int gy = prc->top  + ((prc->bottom - prc->top) - pGlyph->h) / 2;
      DwfDrawTexGL(pGlyph, (float)gx, (float)gy, (float)pGlyph->w, (float)pGlyph->h, cfGlyph);
    }
}

VOID WINAPI DwmFrameDrawChrome(DWMFRAME* f, HWND hwnd, int cx, int cy)
{
    int      capH;
    BOOL     fActive;
    BOOL     fDark;
    DWFCOLOR colCap;
    DWFCOLOR colText;
    DWFCOLOR colGlyph;
    DWFCOLOR colWhite;

    if (!f || cx <= 0 || cy <= 0)
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
    colWhite.r = colWhite.g = colWhite.b = colWhite.a = 1.0f;

    DwfEnsureChromeAssets(f, hwnd);
    DwfEnsureIcon(f, hwnd);

    /* Window-coordinate ortho over the (cx, cy) frame.  The presenter's blit
     * (and the readback flip) turns the GL bottom-up image top-down, so
     * top-of-projection here IS the top of the window. */
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT |
                 GL_VIEWPORT_BIT | GL_SCISSOR_BIT | GL_TRANSFORM_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)cx, (double)cy, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glViewport(0, 0, cx, cy);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    /* Caption band over the client content's top strip (the app lays out
     * below it via the viewport work-area inset). */
    DwfFillRectGL(0.0f, 0.0f, (float)cx, (float)capH, colCap);

    /* Caption system icon at the win32kfull DrawCaptionIcon slot, 1:1 (the
     * texture was rasterized at the small-icon metric). */
    if (f->texIcon.id)
    {
      int ix = (capH - f->texIcon.w) / 2 + 1;
      int iy = (capH - f->texIcon.h) / 2;

      DwfDrawTexGL(&f->texIcon, (float)ix, (float)iy, (float)f->texIcon.w, (float)f->texIcon.h, colWhite);
    }

    /* Caption title (system caption font, ClearType): rasterized as an
     * opaque strip against the LIVE caption color (subpixel AA needs the
     * real background; the inactive dim pre-blends colText's alpha toward
     * the band).  Re-rasterized only when title / colors / dpi change —
     * crossfade ticks re-bake through the frame's cached GDI resources.
     * Starts one caption-height in (xxxDrawCaptionTemp icon slot); drawn
     * 1:1, integer-snapped, colors baked (white modulate). */
    {
      DWFCOLOR eff;
      COLORREF crText;
      COLORREF crBack;
      WCHAR    sz[256];

      eff.r = colCap.r + (colText.r - colCap.r) * colText.a;
      eff.g = colCap.g + (colText.g - colCap.g) * colText.a;
      eff.b = colCap.b + (colText.b - colCap.b) * colText.a;
      crText = RGB((BYTE)(eff.r * 255.0f + 0.5f), (BYTE)(eff.g * 255.0f + 0.5f), (BYTE)(eff.b * 255.0f + 0.5f));
      crBack = RGB((BYTE)(colCap.r * 255.0f + 0.5f), (BYTE)(colCap.g * 255.0f + 0.5f), (BYTE)(colCap.b * 255.0f + 0.5f));

      sz[0] = 0;
      (void)GetWindowTextW(hwnd, sz, ARRAYSIZE(sz));
      if (0 != lstrcmpW(sz, f->szTitle) || crText != f->crTitleText || crBack != f->crTitleBack)
      {
        lstrcpynW(f->szTitle, sz, ARRAYSIZE(f->szTitle));
        f->crTitleText = crText;
        f->crTitleBack = crBack;
        if (sz[0])
          (void)DwfRasterizeTitle(f, sz, lstrlenW(sz), crText, crBack);
        else
          DwfFreeTex(&f->texTitle);
      }

      if (f->texTitle.id && f->szTitle[0])
      {
        int ty = (capH - f->texTitle.h) / 2;
        DwfDrawTexGL(&f->texTitle, (float)capH, (float)ty, (float)f->texTitle.w, (float)f->texTitle.h, colWhite);
      }
    }

    /* Caption buttons: light/dark, Minimize, Maximize/Restore, Close. */
    {
      RECT rcClose, rcMax, rcMin, rcLD;

      if (DwfButtonRects(hwnd, cx, &rcClose, &rcMax, &rcMin, &rcLD))
      {
        DwfDrawButton(f, &rcLD,    DWB_LIGHTDARK, &f->texGlyph[fDark ? DWFG_SUN : DWFG_MOON],          fDark, colGlyph, f->flBtnOpacity[DWB_LIGHTDARK]);
        DwfDrawButton(f, &rcMin,   DWB_MIN,       &f->texGlyph[DWFG_MIN],                              fDark, colGlyph, f->flBtnOpacity[DWB_MIN]);
        DwfDrawButton(f, &rcMax,   DWB_MAX,       &f->texGlyph[IsZoomed(hwnd) ? DWFG_RESTORE : DWFG_MAX], fDark, colGlyph, f->flBtnOpacity[DWB_MAX]);
        DwfDrawButton(f, &rcClose, DWB_CLOSE,     &f->texGlyph[DWFG_CLOSE],                            fDark, colGlyph, f->flBtnOpacity[DWB_CLOSE]);
      }
    }

    /* DWM-style 1px window border ring, drawn by us on the CONTENT edges —
     * the DWMWA border/corner attributes ring the WINDOW rect, one
     * invisible strip outside the visible face.  Win11's border gray,
     * dimmed when inactive, riding the same activation crossfade.  Skipped
     * maximized (native maximized windows draw no ring). */
    if (!IsZoomed(hwnd))
    {
      DWFCOLOR ring;
      float    aTo   = fActive ? 0.55f : 0.30f;
      float    aFrom = (f->fAnim ? f->fActiveFrom : fActive) ? 0.55f : 0.30f;
      float    t     = f->fAnim ? f->flAnimT : 1.0f;

      ring   = DwfColor(RGB(0x75, 0x75, 0x75));
      ring.a = aFrom + (aTo - aFrom) * t;
      DwfFillRectGL(0.0f,            0.0f,            (float)cx,       1.0f,       ring);   /* top    */
      DwfFillRectGL(0.0f,            (float)(cy - 1), (float)cx,       (float)cy,  ring);   /* bottom */
      DwfFillRectGL(0.0f,            0.0f,            1.0f,            (float)cy,  ring);   /* left   */
      DwfFillRectGL((float)(cx - 1), 0.0f,            (float)cx,       (float)cy,  ring);   /* right  */
    }

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
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

    DwfApplyDwmFrame(hwnd);
    return f;
}

VOID WINAPI DwmFrameDestroy(DWMFRAME* f)
{
    int i;

    if (!f)
      return;
    if (f->hwnd && f->fAnim)
      (void)KillTimer(f->hwnd, DWF_ANIM_TIMER_ID);
    if (wglGetCurrentContext())
    {
      DwfFreeTex(&f->texIcon);
      DwfFreeTex(&f->texTitle);
      for (i = 0; i < DWFG_COUNT; ++i)
        DwfFreeTex(&f->texGlyph[i]);
    }
    if (f->titleDC && f->titleDibPrev)
      (void)SelectObject(f->titleDC, f->titleDibPrev);
    if (f->titleDib)
      (void)DeleteObject(f->titleDib);
    if (f->titleDC)
      (void)DeleteDC(f->titleDC);
    if (f->titleFont)
      (void)DeleteObject(f->titleFont);
    HeapFree(GetProcessHeap(), 0, f);
}

/* ---- geometry (imguiapp / ImmersiveWindow shape, verbatim) ----------------------------------------- */

UINT WINAPI DwmFrameNCCalcSize(HWND hwnd, BOOL fCalcValidRects, NCCALCSIZE_PARAMS* lpcsp)
{
    if (!fCalcValidRects)
      return 0;

    /* CLIENT == WINDOW, in every state — the borderless invariants and
     * nothing else: rgrc[1] = rgrc[2] ("lie to dwm"); maximized, clamp the
     * client to the monitor WORK AREA (pairs with DwmFrameGetMinMaxInfo).
     * MEASURED TWICE (snapprobe 2026-07-12, hand-inset NC and DefWindowProc
     * NC): this window's snap rects arrive zone-EXACT and its extended
     * frame bounds always equal the window rect — the system never
     * classifies its NC as invisible borders, so ANY client inset gaps
     * against snap boundaries by exactly the inset.  The face must fill the
     * window rect; the resize ring synthesizes INSIDE the client edges. */
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
    UINT dpi;
    int  button_width;
    int  caption;

    GUITHREADINFO gti = { sizeof(gti) };
    MONITORINFO   mi  = { sizeof(mi) };

    /* Min track keeps the caption anatomy intact (icon slot + the four
     * buttons; client == window, no border terms).  Maximize and max track
     * are EXACTLY the nearest monitor's work area at its work origin —
     * pairs with the zoomed WM_NCCALCSIZE work-area clamp. */
    dpi          = DwfDpi(hwnd);
    button_width = MulDiv(47, (int)dpi, 96);
    caption      = (int)DwmFrameCaptionHeight(hwnd);
    lpMinMaxInfo->ptMinTrackSize.x = caption + 4 * button_width;
    lpMinMaxInfo->ptMinTrackSize.y = caption;

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

    /* RESIZE RING FIRST (native Win11 precedence: the top band wins over
     * the caption buttons — hovering the very top of Close on any native
     * window gives the resize arrow; buttons checked first make the
     * top-right corner unreachable).  Client == window: the ring lives
     * INSIDE the client edges. */
    if (fSizable)
    {
      /* Client == window: the whole resize ring lives INSIDE the client
       * edges, one frame metric thick.  Corners: within an edge band the
       * corner zone reaches 2x the metric ALONG the edge. */
      int reachX = border.cx * 2;
      int reachY = border.cy * 2;

      row = 1;
      col = 1;
      if (pt.y < border.cy)                        row = 0;
      else if (pt.y >= client.bottom - border.cy)  row = 2;
      if (pt.x < border.cx)                        col = 0;
      else if (pt.x >= client.right - border.cx)   col = 2;

      if (row != 1 && col == 1)
      {
        if (pt.x < reachX)                         col = 0;
        else if (pt.x >= client.right - reachX)    col = 2;
      }
      if (col != 1 && row == 1)
      {
        if (pt.y < reachY)                         row = 0;
        else if (pt.y >= client.bottom - reachY)   row = 2;
      }

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

    /* Caption buttons, then the system-menu icon slot (inset past the left
     * ring band, one small-icon wide), then the caption strip. */
    if (DwfButtonRects(hwnd, (int)client.right, &rcClose, &rcMax, &rcMin, &rcLD))
    {
      if (PtInRect(&rcLD, pt))    return HTLIGHTDARKBTN;
      if (PtInRect(&rcMin, pt))   return HTMINBUTTON;
      if (PtInRect(&rcMax, pt))   return HTMAXBUTTON;
      if (PtInRect(&rcClose, pt)) return HTCLOSE;
    }

    rcIcon.left   = border.cx;
    rcIcon.top    = border.cy;
    rcIcon.right  = border.cx + GetSystemMetricsForDpi(SM_CXSMICON, DwfDpi(hwnd));
    rcIcon.bottom = capH;
    if (PtInRect(&rcIcon, pt))
      return HTSYSMENU;

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
    case DWB_LIGHTDARK:
      DwfBeginTransition(f, hwnd, !f->fDark, f->fWndActive);
      if (f->pfnTheme)
        f->pfnTheme(hwnd, f->fDark);   /* fDark already flipped by the transition */
      break;
    default:            break;
    }
}

BOOL WINAPI DwmFrameButtonPressActive(DWMFRAME* f)
{
    return f && f->fCapturing;
}

VOID WINAPI DwmFrameSetThemeCallback(DWMFRAME* f, DWMFRAMETHEMEPROC pfnTheme)
{
    if (f)
      f->pfnTheme = pfnTheme;
}

VOID WINAPI DwmFrameShowSystemMenu(DWMFRAME* f, HWND hwnd, int xScreen, int yScreen)
{
    HMENU menu;
    BOOL  fZoomed;
    UINT  cmd;

    UNREFERENCED_PARAMETER(f);

    menu = GetSystemMenu(hwnd, FALSE);
    if (!menu)
      return;

    if (xScreen == -1 && yScreen == -1)
    {
      /* Anchor below the caption, inset one frame border from the window's
       * left edge (native icon-click / Alt+Space placement). */
      RECT rc;
      SIZE border;
      if (GetWindowRect(hwnd, &rc))
      {
        DwfWindowBorders(hwnd, &border);
        xScreen = rc.left + border.cx;
        yScreen = rc.top + (int)DwmFrameCaptionHeight(hwnd);
      }
      else
      {
        xScreen = 0;
        yScreen = 0;
      }
    }

    /* DefWindowProc-equivalent item states. */
    fZoomed = IsZoomed(hwnd);
    (void)EnableMenuItem(menu, SC_RESTORE,  MF_BYCOMMAND | (fZoomed ? MF_ENABLED : MF_GRAYED));
    (void)EnableMenuItem(menu, SC_MOVE,     MF_BYCOMMAND | (fZoomed ? MF_GRAYED : MF_ENABLED));
    (void)EnableMenuItem(menu, SC_SIZE,     MF_BYCOMMAND | (fZoomed ? MF_GRAYED : MF_ENABLED));
    (void)EnableMenuItem(menu, SC_MAXIMIZE, MF_BYCOMMAND | (fZoomed ? MF_GRAYED : MF_ENABLED));
    (void)EnableMenuItem(menu, SC_MINIMIZE, MF_BYCOMMAND | MF_ENABLED);
    (void)EnableMenuItem(menu, SC_CLOSE,    MF_BYCOMMAND | MF_ENABLED);
    (void)SetMenuDefaultItem(menu, SC_CLOSE, FALSE);

    cmd = (UINT)TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                                 xScreen, yScreen, hwnd, NULL);
    if (cmd)
      (void)PostMessageW(hwnd, WM_SYSCOMMAND, (WPARAM)cmd, 0);
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
