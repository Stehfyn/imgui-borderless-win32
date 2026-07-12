/*****************************************************************************\
*                                                                             *
* dxgipresent.cpp - imguiapp_impl_win32_d2ddxgi presenter, ported for         *
*                   WGLWindow (C++ TU: real d3d11/dxgi/d2d1/dcomp headers).   *
*                                                                             *
* Transcribed from the working imguiapp_impl_win32_d2ddxgi backend:           *
* creation ladder (feature levels 10_0-first, AUTHORITATIVE|SINGLETHREADED|   *
* BGRA device, desktop-sized premultiplied FLIP_DISCARD composition           *
* swapchain with TEARING|MODE_SWITCH|WAITABLE, buffer 0 bound once, DComp     *
* target/visual/SetContent/root/Commit), the two-step present ladder, the R6  *
* pin, the R4 latch, the R2 wait, the D3DKMT vblank wait, and the R5 pace     *
* thread.  GL pixels enter buffer 0 via WGL_NV_DX_interop2 (dxgi-noflicker    *
* §5) with a glReadPixels + UpdateSubresource fallback so creation cannot     *
* fail on interop-less boxes.  NO D2D anywhere: the caption chrome is drawn   *
* by dwmframe.c in OpenGL into the GL frame BEFORE the fill, so buffer 0      *
* receives client + chrome in one copy.                                       *
*                                                                             *
\*****************************************************************************/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dxgi1_5.h>
#include <dcomp.h>
#include <gl/gl.h>

#include "dxgipresent.h"

#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "dcomp")
#pragma comment(lib, "gdi32")

/* Reference macro tier (ImmersiveWindow.c): named composites over raw flags. */
#define D3D11_DEVICE_SINGLETHREADED (                                    \
    D3D11_CREATE_DEVICE_SINGLETHREADED                           |       \
    D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS         \
    )
#define D3D11_DEVICE_AUTHORITATIVE (                                     \
    D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT                           |  \
    D3D11_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY    \
    )
#define D3D11_DEVICE_D2D_COMPATIBLE ( \
    D3D11_CREATE_DEVICE_BGRA_SUPPORT  \
    )
#define DXGI_SWAPCHAIN_ENABLE_IMMEDIATE_PRESENT ( \
    DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING     |      \
    DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH        \
    )
#define DXGI_SWAPCHAIN_ENABLE_WAIT_FOR_NEXT_FRAME_RESOURCES ( \
    DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT        \
    )

#ifndef DXGI_PRESENT_ALLOW_TEARING
#define DXGI_PRESENT_ALLOW_TEARING 0x00000200UL
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

/* D3DKMT vblank wait (gdi32 exports; declarations mirrored from d3dkmthk.h). */
typedef UINT DWF_D3DKMT_HANDLE;
typedef UINT DWF_D3DDDI_VIDEO_PRESENT_SOURCE_ID;
typedef struct DWF_D3DKMT_OPENADAPTERFROMHDC
{
    HDC                              hDc;
    DWF_D3DKMT_HANDLE                hAdapter;
    LUID                             AdapterLuid;
    DWF_D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} DWF_D3DKMT_OPENADAPTERFROMHDC;
typedef struct DWF_D3DKMT_WAITFORVERTICALBLANKEVENT
{
    DWF_D3DKMT_HANDLE                hAdapter;
    DWF_D3DKMT_HANDLE                hDevice;
    DWF_D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} DWF_D3DKMT_WAITFORVERTICALBLANKEVENT;
typedef struct DWF_D3DKMT_GETSCANLINE
{
    DWF_D3DKMT_HANDLE                hAdapter;
    DWF_D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    BOOLEAN                          InVerticalBlank;
    UINT                             ScanLine;
} DWF_D3DKMT_GETSCANLINE;
typedef struct DWF_D3DKMT_CLOSEADAPTER
{
    DWF_D3DKMT_HANDLE hAdapter;
} DWF_D3DKMT_CLOSEADAPTER;
extern "C" LONG APIENTRY D3DKMTOpenAdapterFromHdc(DWF_D3DKMT_OPENADAPTERFROMHDC*);
extern "C" LONG APIENTRY D3DKMTWaitForVerticalBlankEvent(const DWF_D3DKMT_WAITFORVERTICALBLANKEVENT*);
extern "C" LONG APIENTRY D3DKMTGetScanLine(DWF_D3DKMT_GETSCANLINE*);
extern "C" LONG APIENTRY D3DKMTCloseAdapter(const DWF_D3DKMT_CLOSEADAPTER*);

/* Minimal GL/WGL extension subset (the SDK ships neither glext.h nor
 * wglext.h); loaded once through wglGetProcAddress. */
#define GL_READ_FRAMEBUFFER              0x8CA8
#define GL_DRAW_FRAMEBUFFER              0x8CA9
#define GL_COLOR_ATTACHMENT0             0x8CE0
#define GL_FRAMEBUFFER_COMPLETE          0x8CD5
#define GL_RENDERBUFFER                  0x8D41
#define GL_BGRA_EXT                      0x80E1
#define WGL_ACCESS_WRITE_DISCARD_NV      0x0002

typedef void   (WINAPI* PFNGLGENFRAMEBUFFERSPROC)(GLsizei, GLuint*);
typedef void   (WINAPI* PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei, const GLuint*);
typedef void   (WINAPI* PFNGLBINDFRAMEBUFFERPROC)(GLenum, GLuint);
typedef void   (WINAPI* PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum (WINAPI* PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum);
typedef void   (WINAPI* PFNGLBLITFRAMEBUFFERPROC)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void   (WINAPI* PFNGLGENRENDERBUFFERSPROC)(GLsizei, GLuint*);
typedef void   (WINAPI* PFNGLDELETERENDERBUFFERSPROC)(GLsizei, const GLuint*);
typedef HANDLE (WINAPI* PFNWGLDXOPENDEVICENVPROC)(void*);
typedef BOOL   (WINAPI* PFNWGLDXCLOSEDEVICENVPROC)(HANDLE);
typedef HANDLE (WINAPI* PFNWGLDXREGISTEROBJECTNVPROC)(HANDLE, void*, GLuint, GLenum, GLenum);
typedef BOOL   (WINAPI* PFNWGLDXUNREGISTEROBJECTNVPROC)(HANDLE, HANDLE);
typedef BOOL   (WINAPI* PFNWGLDXSETRESOURCESHAREHANDLENVPROC)(void*, HANDLE);
typedef BOOL   (WINAPI* PFNWGLDXLOCKOBJECTSNVPROC)(HANDLE, GLint, HANDLE*);
typedef BOOL   (WINAPI* PFNWGLDXUNLOCKOBJECTSNVPROC)(HANDLE, GLint, HANDLE*);

static PFNGLGENFRAMEBUFFERSPROC             s_glGenFramebuffers;
static PFNGLDELETEFRAMEBUFFERSPROC          s_glDeleteFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC             s_glBindFramebuffer;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC     s_glFramebufferRenderbuffer;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC      s_glCheckFramebufferStatus;
static PFNGLBLITFRAMEBUFFERPROC             s_glBlitFramebuffer;
static PFNGLGENRENDERBUFFERSPROC            s_glGenRenderbuffers;
static PFNGLDELETERENDERBUFFERSPROC         s_glDeleteRenderbuffers;
static PFNWGLDXOPENDEVICENVPROC             s_wglDXOpenDeviceNV;
static PFNWGLDXCLOSEDEVICENVPROC            s_wglDXCloseDeviceNV;
static PFNWGLDXREGISTEROBJECTNVPROC         s_wglDXRegisterObjectNV;
static PFNWGLDXUNREGISTEROBJECTNVPROC       s_wglDXUnregisterObjectNV;
static PFNWGLDXSETRESOURCESHAREHANDLENVPROC s_wglDXSetResourceShareHandleNV;
static PFNWGLDXLOCKOBJECTSNVPROC            s_wglDXLockObjectsNV;
static PFNWGLDXUNLOCKOBJECTSNVPROC          s_wglDXUnlockObjectsNV;

/* DCompositionWaitForCompositorClock (dcomp.dll, Win10 2004+), loaded once. */
typedef DWORD (WINAPI* PFN_DWF_WAITFORCOMPOSITORCLOCK)(UINT, const HANDLE*, DWORD);
static PFN_DWF_WAITFORCOMPOSITORCLOCK s_pfnWaitForCompositorClock;
static BOOL s_fWaitForCompositorClockLoaded;

static void DwfLoadGLProcs()
{
    if (s_wglDXLockObjectsNV && s_glBlitFramebuffer)
        return;
    s_glGenFramebuffers         = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
    s_glDeleteFramebuffers      = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
    s_glBindFramebuffer         = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
    s_glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glFramebufferRenderbuffer");
    s_glCheckFramebufferStatus  = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
    s_glBlitFramebuffer         = (PFNGLBLITFRAMEBUFFERPROC)wglGetProcAddress("glBlitFramebuffer");
    s_glGenRenderbuffers        = (PFNGLGENRENDERBUFFERSPROC)wglGetProcAddress("glGenRenderbuffers");
    s_glDeleteRenderbuffers     = (PFNGLDELETERENDERBUFFERSPROC)wglGetProcAddress("glDeleteRenderbuffers");
    s_wglDXOpenDeviceNV         = (PFNWGLDXOPENDEVICENVPROC)wglGetProcAddress("wglDXOpenDeviceNV");
    s_wglDXCloseDeviceNV        = (PFNWGLDXCLOSEDEVICENVPROC)wglGetProcAddress("wglDXCloseDeviceNV");
    s_wglDXRegisterObjectNV     = (PFNWGLDXREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXRegisterObjectNV");
    s_wglDXUnregisterObjectNV   = (PFNWGLDXUNREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXUnregisterObjectNV");
    s_wglDXSetResourceShareHandleNV = (PFNWGLDXSETRESOURCESHAREHANDLENVPROC)wglGetProcAddress("wglDXSetResourceShareHandleNV");
    s_wglDXLockObjectsNV        = (PFNWGLDXLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXLockObjectsNV");
    s_wglDXUnlockObjectsNV      = (PFNWGLDXUNLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXUnlockObjectsNV");
}

struct DXGIPRESENT
{
    HWND                    hwnd;
    ID3D11Device*           dev;
    ID3D11DeviceContext*    ctx;
    IDXGIDevice1*           dxgidev;
    IDXGIFactory2*          factory;
    IDXGISwapChain2*        sc;             /* composition swapchain; NEVER resized */
    ID3D11Texture2D*        backbuf;        /* buffer 0, bound once (flip-model alias) */
    ID3D11RenderTargetView* rtv;            /* buffer 0 RTV, bound once (canon MainRTV) */
    IDCompositionDevice*    dcomp;
    IDCompositionTarget*    target;
    IDCompositionVisual*    visual;
    HANDLE                  frame_latency_waitable;
    BOOL                    tearing;
    int                     width;          /* buffer size (desktop) */
    int                     height;

    /* GL delivery: interop fast path or readback fallback */
    BOOL                    use_interop;
    ID3D11Texture2D*        shared_tex;     /* interop intermediate */
    HANDLE                  gldev;
    HANDLE                  globj;
    GLuint                  rbo;
    GLuint                  fbo;
    BYTE*                   rb_pixels;      /* readback fallback staging (bottom-up) */
    BYTE*                   rb_flip;        /* top-down flip for UpdateSubresource */
    SIZE_T                  rb_capacity;

    /* R6 content pin state (client screen origin at last present) */
    POINT                   content_origin;
    SIZE                    content_size;
    BOOL                    content_valid;
    BOOL                    offset_active;

    /* R4 compositor-frame repaint latch */
    LONGLONG                latched_frame_time;
    int                     latched_cx;
    int                     latched_cy;

    /* D3DKMT vblank wait (adapter opened on first use) */
    DWF_D3DKMT_HANDLE       vblank_adapter;
    UINT                    vblank_source;

    /* R5 pace thread */
    HANDLE                  pace_thread;
    HANDLE                  pace_wake;
    volatile BOOL           pace_stop;
    volatile BOOL           pace_modal_live;
    volatile LONG           pace_pending;
};

/* ---- R5 pace thread (reference PaceThreadProc: event-driven, no polling) -------------------------- */

static float DwfPrimaryRefreshHz()
{
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        return (float)dm.dmDisplayFrequency;
    return 0.0f;
}

static DWORD WINAPI DwfPaceThreadProc(LPVOID param)
{
    DXGIPRESENT* p = (DXGIPRESENT*)param;
    HWND hwnd = p->hwnd;

    DWF_D3DKMT_OPENADAPTERFROMHDC oa = {};
    oa.hDc = GetDC(hwnd);
    LONG open_status = D3DKMTOpenAdapterFromHdc(&oa);
    ReleaseDC(hwnd, oa.hDc);
    HANDLE timer = nullptr;

    while (!p->pace_stop)
    {
        if (!p->pace_modal_live)
        {
            WaitForSingleObject(p->pace_wake, INFINITE);   /* parked */
            continue;
        }

        bool tick = false;
        if (s_pfnWaitForCompositorClock != nullptr)
        {
            const DWORD wait = s_pfnWaitForCompositorClock(1, &p->pace_wake, INFINITE);
            tick = (wait == WAIT_OBJECT_0 + 1);
        }
        else if (open_status == 0)
        {
            DWF_D3DKMT_WAITFORVERTICALBLANKEVENT vbe = {};
            vbe.hAdapter      = oa.hAdapter;
            vbe.VidPnSourceId = oa.VidPnSourceId;
            if (D3DKMTWaitForVerticalBlankEvent(&vbe) == 0)
                tick = true;
            else
                open_status = -1;   /* adapter lost: drop to the timer tier */
        }
        else
        {
            if (timer == nullptr)
            {
                timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
                if (timer == nullptr)
                    timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
                if (timer == nullptr)
                    break;   /* no clock source; the WM_TIMER fallback still paces */
                float hz = DwfPrimaryRefreshHz();
                if (hz <= 0.0f)
                    hz = 60.0f;
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG)(10000000.0 / hz);
                SetWaitableTimer(timer, &due, (LONG)(1000.0f / hz + 0.5f), nullptr, nullptr, FALSE);
            }
            const HANDLE handles[2] = { p->pace_wake, timer };
            const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            tick = (wait == WAIT_OBJECT_0 + 1);
        }

        if (tick && !p->pace_stop && p->pace_modal_live &&
            InterlockedCompareExchange(&p->pace_pending, 1, 0) == 0)
            PostMessage(hwnd, DXGIPRESENT_WM_PACE, 0, 0);
    }

    if (timer != nullptr)
    {
        CancelWaitableTimer(timer);
        CloseHandle(timer);
    }
    if (open_status == 0)
    {
        DWF_D3DKMT_CLOSEADAPTER close_adapter = { oa.hAdapter };
        D3DKMTCloseAdapter(&close_adapter);
    }
    return 0;
}

/* ---- creation ladder (reference CreateDeviceObjects, verbatim parameters) ------------------------- */

/* The process-wide D3D stack every presenter borrows (accessor-owned
 * singleton, same shape as the GL proc table above): built on first use,
 * lives for the process.  Returns nullptr when no D3D11 device can be
 * created at all. */
typedef struct DXGIPRESENTDEVICE
{
    ID3D11Device*        dev;
    ID3D11DeviceContext* ctx;
    IDXGIDevice1*        dxgidev;
    IDXGIFactory2*       factory;
    BOOL                 tearing;
} DXGIPRESENTDEVICE;

static const DXGIPRESENTDEVICE* DxgiPresent_GetSharedDevice(void)
{
    static DXGIPRESENTDEVICE shared;

    if (!shared.dev)
    {
        /* Canonical feature-level array, order preserved: D3D11CreateDevice
         * takes the FIRST level it can create (10_0-first, the reference's
         * behavior). */
        static const D3D_FEATURE_LEVEL c_levels[] =
        {
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_12_1,
        };
        const UINT flags = D3D11_DEVICE_AUTHORITATIVE | D3D11_DEVICE_SINGLETHREADED | D3D11_DEVICE_D2D_COMPATIBLE;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       c_levels, ARRAYSIZE(c_levels), D3D11_SDK_VERSION, &shared.dev, nullptr, &shared.ctx);
        if (FAILED(hr))
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                   c_levels, ARRAYSIZE(c_levels), D3D11_SDK_VERSION, &shared.dev, nullptr, &shared.ctx);
        if (FAILED(hr))
            return nullptr;
        if (FAILED(shared.dev->QueryInterface(IID_PPV_ARGS(&shared.dxgidev))) ||
            FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&shared.factory))))
        {
            if (shared.dxgidev) { shared.dxgidev->Release(); shared.dxgidev = nullptr; }
            if (shared.ctx)     { shared.ctx->Release();     shared.ctx = nullptr; }
            shared.dev->Release();
            shared.dev = nullptr;
            return nullptr;
        }

        IDXGIFactory5* factory5 = nullptr;
        if (SUCCEEDED(shared.factory->QueryInterface(IID_PPV_ARGS(&factory5))))
        {
            BOOL allow = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
                shared.tearing = allow;
            factory5->Release();
        }
    }
    return &shared;
}

DXGIPRESENT* WINAPI DxgiPresent_Create(HWND hWnd)
{
    if (!hWnd)
        return nullptr;

    DXGIPRESENT* p = (DXGIPRESENT*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p)
        return nullptr;
    p->hwnd = hWnd;

    HRESULT hr;
    {
        HDC screen = GetDC(nullptr);
        p->width  = GetDeviceCaps(screen, DESKTOPHORZRES);
        p->height = GetDeviceCaps(screen, DESKTOPVERTRES);
        ReleaseDC(nullptr, screen);
    }
    if (p->width <= 0 || p->height <= 0)
        goto fail;

    /* ONE D3D stack for every presenter: device creation is the dominant
     * cost of DxgiPresent_Create, and secondary viewports are created
     * MID-DRAG — a per-window device is a visible hitch.  The swapchain,
     * the DComp device/target/visual, and the interop registration stay
     * per-window (DComp Commit is device-wide; the R1-R6 latch protocol
     * depends on per-window commits).  Each presenter holds its own
     * references; the shared stack lives for the process. */
    {
        const DXGIPRESENTDEVICE* shared = DxgiPresent_GetSharedDevice();
        if (!shared)
            goto fail;
        p->dev = shared->dev;         p->dev->AddRef();
        p->ctx = shared->ctx;         p->ctx->AddRef();
        p->dxgidev = shared->dxgidev; p->dxgidev->AddRef();
        p->factory = shared->factory; p->factory->AddRef();
        p->tearing = shared->tearing;
    }

    {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width              = (UINT)p->width;
        desc.Height             = (UINT)p->height;
        desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode          = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Scaling            = DXGI_SCALING_STRETCH;
        desc.SampleDesc.Count   = 1;
        desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount        = 5;   /* canonical "3 + 1 + 1" */
        desc.Flags              = DXGI_SWAPCHAIN_ENABLE_WAIT_FOR_NEXT_FRAME_RESOURCES |
                                  (p->tearing ? DXGI_SWAPCHAIN_ENABLE_IMMEDIATE_PRESENT
                                              : DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

        IDXGISwapChain1* sc1 = nullptr;
        hr = p->factory->CreateSwapChainForComposition(p->dxgidev, &desc, nullptr, &sc1);
        if (FAILED(hr))
            goto fail;
        hr = sc1->QueryInterface(IID_PPV_ARGS(&p->sc));
        sc1->Release();
        if (FAILED(hr))
            goto fail;
    }

    p->factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN);
    p->frame_latency_waitable = p->sc->GetFrameLatencyWaitableObject();

    /* Bind buffer 0 ONCE: D3D11 flip-model buffer 0 aliases the current back
     * buffer and this swapchain never resizes, so the D2D target bitmap
     * stays valid for the swapchain's lifetime. */
    if (FAILED(p->sc->GetBuffer(0, IID_PPV_ARGS(&p->backbuf))))
        goto fail;
    if (FAILED(p->dev->CreateRenderTargetView(p->backbuf, nullptr, &p->rtv)))
        goto fail;

    if (FAILED(DCompositionCreateDevice(p->dxgidev, IID_PPV_ARGS(&p->dcomp))) ||
        FAILED(p->dcomp->CreateTargetForHwnd(hWnd, TRUE, &p->target)) ||
        FAILED(p->dcomp->CreateVisual(&p->visual)) ||
        FAILED(p->visual->SetContent(p->sc)) ||
        FAILED(p->target->SetRoot(p->visual)) ||
        FAILED(p->dcomp->Commit()))
        goto fail;

    /* GL delivery: interop fast path (dxgi-noflicker §5); readback fallback
     * keeps the presenter alive when NV_DX_interop2 is absent or refuses. */
    DwfLoadGLProcs();
    p->use_interop = FALSE;
    if (s_wglDXOpenDeviceNV && s_wglDXRegisterObjectNV && s_wglDXLockObjectsNV &&
        s_glGenFramebuffers && s_glBlitFramebuffer && s_glGenRenderbuffers)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width            = (UINT)p->width;
        td.Height           = (UINT)p->height;
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags        = D3D11_RESOURCE_MISC_SHARED;

        if (SUCCEEDED(p->dev->CreateTexture2D(&td, nullptr, &p->shared_tex)))
        {
            HANDLE share_handle = nullptr;
            IDXGIResource* resource = nullptr;
            if (SUCCEEDED(p->shared_tex->QueryInterface(IID_PPV_ARGS(&resource))))
            {
                resource->GetSharedHandle(&share_handle);
                resource->Release();
            }
            p->gldev = s_wglDXOpenDeviceNV(p->dev);
            if (p->gldev)
            {
                if (share_handle && s_wglDXSetResourceShareHandleNV)
                    s_wglDXSetResourceShareHandleNV(p->shared_tex, share_handle);
                s_glGenRenderbuffers(1, &p->rbo);
                p->globj = s_wglDXRegisterObjectNV(p->gldev, p->shared_tex, p->rbo,
                                                   GL_RENDERBUFFER, WGL_ACCESS_WRITE_DISCARD_NV);
                if (p->globj)
                {
                    s_glGenFramebuffers(1, &p->fbo);
                    if (s_wglDXLockObjectsNV(p->gldev, 1, &p->globj))
                    {
                        s_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p->fbo);
                        s_glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, p->rbo);
                        const GLenum status = s_glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
                        s_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                        s_wglDXUnlockObjectsNV(p->gldev, 1, &p->globj);
                        p->use_interop = (status == GL_FRAMEBUFFER_COMPLETE);
                    }
                }
            }
        }
        if (!p->use_interop)
        {
            /* Tear the half-built interop down; the readback path needs none of it. */
            if (p->globj && p->gldev) { s_wglDXUnregisterObjectNV(p->gldev, p->globj); p->globj = nullptr; }
            if (p->gldev)             { s_wglDXCloseDeviceNV(p->gldev); p->gldev = nullptr; }
            if (p->fbo)               { s_glDeleteFramebuffers(1, &p->fbo); p->fbo = 0; }
            if (p->rbo)               { s_glDeleteRenderbuffers(1, &p->rbo); p->rbo = 0; }
            if (p->shared_tex)        { p->shared_tex->Release(); p->shared_tex = nullptr; }
        }
    }

    /* R5 pace thread. */
    if (!s_fWaitForCompositorClockLoaded)
    {
        s_fWaitForCompositorClockLoaded = TRUE;
        if (HMODULE dcomp_dll = GetModuleHandleA("dcomp.dll"))
            s_pfnWaitForCompositorClock = (PFN_DWF_WAITFORCOMPOSITORCLOCK)(void*)GetProcAddress(dcomp_dll, "DCompositionWaitForCompositorClock");
    }
    p->pace_wake   = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    p->pace_thread = p->pace_wake ? CreateThread(nullptr, 0, DwfPaceThreadProc, p, 0, nullptr) : nullptr;

    return p;

fail:
    DxgiPresent_Destroy(p);
    return nullptr;
}

VOID WINAPI DxgiPresent_Destroy(DXGIPRESENT* p)
{
    if (!p)
        return;

    if (p->pace_thread)
    {
        p->pace_stop = TRUE;
        SetEvent(p->pace_wake);
        WaitForSingleObject(p->pace_thread, 2000);
        CloseHandle(p->pace_thread);
        p->pace_thread = nullptr;
    }
    if (p->pace_wake)
    {
        CloseHandle(p->pace_wake);
        p->pace_wake = nullptr;
    }

    if (p->globj && p->gldev) s_wglDXUnregisterObjectNV(p->gldev, p->globj);
    if (p->gldev)             s_wglDXCloseDeviceNV(p->gldev);
    if (p->fbo && s_glDeleteFramebuffers)  s_glDeleteFramebuffers(1, &p->fbo);
    if (p->rbo && s_glDeleteRenderbuffers) s_glDeleteRenderbuffers(1, &p->rbo);

    if (p->visual)     p->visual->Release();
    if (p->target)     p->target->Release();
    if (p->dcomp)      p->dcomp->Release();
    if (p->frame_latency_waitable) CloseHandle(p->frame_latency_waitable);
    if (p->rtv)        p->rtv->Release();
    if (p->backbuf)    p->backbuf->Release();
    if (p->shared_tex) p->shared_tex->Release();
    if (p->sc)         p->sc->Release();
    if (p->factory)    p->factory->Release();
    if (p->dxgidev)    p->dxgidev->Release();
    if (p->ctx)        p->ctx->Release();
    if (p->dev)        p->dev->Release();

    if (p->vblank_adapter)
    {
        DWF_D3DKMT_CLOSEADAPTER close_adapter = { p->vblank_adapter };
        D3DKMTCloseAdapter(&close_adapter);
    }
    if (p->rb_pixels) HeapFree(GetProcessHeap(), 0, p->rb_pixels);
    if (p->rb_flip)   HeapFree(GetProcessHeap(), 0, p->rb_flip);
    HeapFree(GetProcessHeap(), 0, p);
}

/* ---- GL delivery ----------------------------------------------------------------------------------- */

BOOL WINAPI DxgiPresent_FillFromGL(DXGIPRESENT* p, int cx, int cy)
{
    if (!p || cx <= 0 || cy <= 0)
        return FALSE;
    if (cx > p->width)  cx = p->width;
    if (cy > p->height) cy = p->height;

    /* Buffer 0 is desktop-sized and bound once: clear the WHOLE buffer to
     * transparent before writing the client sub-rect, so everything beyond
     * the client stays genuinely transparent on the premultiplied swapchain
     * ("buffer contents beyond the client stay transparent", the canon's
     * contract).  Without this, a geometry-vs-content mismatch during a
     * live resize exposes stale pixels from previous frames as a visible
     * band between the window edge and the GL content. */
    if (p->rtv)
    {
        const float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        p->ctx->ClearRenderTargetView(p->rtv, transparent);
    }

    if (p->use_interop)
    {
        /* The GL frame's alpha channel is whatever imgui's blending left
         * behind — garbage on a premultiplied swapchain (DWM would blend
         * the window face against the desktop).  Force alpha = 1 across the
         * frame before the blit; the readback path's 0xFF OR does the same. */
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        /* Blit the pbuffer's bottom-up image into the shared texture (dst Y
         * inverted -> top-down), then copy the client sub-rect into the
         * once-bound buffer 0. */
        if (!s_wglDXLockObjectsNV(p->gldev, 1, &p->globj))
            return FALSE;
        s_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        s_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p->fbo);
        s_glBlitFramebuffer(0, 0, cx, cy, 0, cy, cx, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        s_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        /* Flush BEFORE releasing the object to D3D: the unlock hands the
         * texture to the D3D timeline, and the copy below must read THIS
         * frame's blit, not the previous one. */
        glFlush();
        s_wglDXUnlockObjectsNV(p->gldev, 1, &p->globj);

        const D3D11_BOX box = { 0, 0, 0, (UINT)cx, (UINT)cy, 1 };
        p->ctx->CopySubresourceRegion(p->backbuf, 0, 0, 0, 0, p->shared_tex, 0, &box);
        return TRUE;
    }

    /* Readback fallback: glReadPixels (bottom-up) -> CPU row flip (opaque
     * alpha for the premultiplied buffer) -> UpdateSubresource into buffer 0. */
    {
        const SIZE_T row = (SIZE_T)cx * 4u;
        const SIZE_T need = row * (SIZE_T)cy;
        if (p->rb_capacity < need)
        {
            BYTE* a = p->rb_pixels ? (BYTE*)HeapReAlloc(GetProcessHeap(), 0, p->rb_pixels, need)
                                   : (BYTE*)HeapAlloc(GetProcessHeap(), 0, need);
            BYTE* b = p->rb_flip   ? (BYTE*)HeapReAlloc(GetProcessHeap(), 0, p->rb_flip, need)
                                   : (BYTE*)HeapAlloc(GetProcessHeap(), 0, need);
            if (!a || !b)
            {
                if (a) p->rb_pixels = a;
                if (b) p->rb_flip = b;
                return FALSE;
            }
            p->rb_pixels   = a;
            p->rb_flip     = b;
            p->rb_capacity = need;
        }

        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, cx, cy, GL_BGRA_EXT, GL_UNSIGNED_BYTE, p->rb_pixels);
        for (int y = 0; y < cy; ++y)
        {
            const DWORD* src = (const DWORD*)(p->rb_pixels + (SIZE_T)(cy - 1 - y) * row);
            DWORD*       dst = (DWORD*)(p->rb_flip + (SIZE_T)y * row);
            for (int x = 0; x < cx; ++x)
                dst[x] = src[x] | 0xFF000000u;
        }

        const D3D11_BOX box = { 0, 0, 0, (UINT)cx, (UINT)cy, 1 };
        p->ctx->UpdateSubresource(p->backbuf, 0, &box, p->rb_flip, (UINT)row, 0);
        return TRUE;
    }
}

/* ---- present ladder + pin + latch ------------------------------------------------------------------ */

BOOL WINAPI DxgiPresent_Present(DXGIPRESENT* p, int cx, int cy, BOOL fRestart, BOOL fVsync)
{
    if (!p || !p->sc || cx <= 0 || cy <= 0)
        return FALSE;
    if (cx > p->width)  cx = p->width;
    if (cy > p->height) cy = p->height;

    const UINT tearing      = p->tearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const UINT flags_first  = tearing | DXGI_PRESENT_DO_NOT_WAIT | (fRestart ? DXGI_PRESENT_RESTART : 0);
    const UINT flags_second = (fRestart || fVsync) ? DXGI_PRESENT_DO_NOT_SEQUENCE : (tearing | DXGI_PRESENT_DO_NOT_WAIT);
    const UINT sync_second  = (fRestart || fVsync) ? 1 : 0;

    HRESULT hr = p->sc->Present(0, flags_first);
    (void)hr;   /* DXGI_ERROR_WAS_STILL_DRAWING is benign */
    hr = p->sc->Present(sync_second, flags_second);
    (void)hr;

    /* Unpin coupled with the present: the offset reset rides the same
     * compositor latch, so fresh content never shows pre-translated. */
    if (p->offset_active)
    {
        p->visual->SetOffsetX(0.0f);
        p->visual->SetOffsetY(0.0f);
        p->dcomp->Commit();
        p->offset_active = FALSE;
    }

    POINT origin = { 0, 0 };
    ClientToScreen(p->hwnd, &origin);
    p->content_origin  = origin;
    p->content_size.cx = cx;
    p->content_size.cy = cy;
    p->content_valid   = TRUE;

    DCOMPOSITION_FRAME_STATISTICS stats = {};
    if (SUCCEEDED(p->dcomp->GetFrameStatistics(&stats)))
        p->latched_frame_time = stats.nextEstimatedFrameTime.QuadPart;
    p->latched_cx = cx;
    p->latched_cy = cy;
    return TRUE;
}

VOID WINAPI DxgiPresent_StampLatch(DXGIPRESENT* p, int cx, int cy)
{
    if (!p || !p->dcomp)
        return;
    DCOMPOSITION_FRAME_STATISTICS stats = {};
    if (SUCCEEDED(p->dcomp->GetFrameStatistics(&stats)))
        p->latched_frame_time = stats.nextEstimatedFrameTime.QuadPart;
    p->latched_cx = cx;
    p->latched_cy = cy;
}

BOOL WINAPI DxgiPresent_ContentCurrent(DXGIPRESENT* p, int cx, int cy)
{
    if (!p || !p->dcomp || !p->content_valid)
        return FALSE;
    DCOMPOSITION_FRAME_STATISTICS stats = {};
    if (FAILED(p->dcomp->GetFrameStatistics(&stats)))
        return FALSE;
    if (stats.nextEstimatedFrameTime.QuadPart != p->latched_frame_time)
        return FALSE;
    return cx == p->latched_cx && cy == p->latched_cy;
}

BOOL WINAPI DxgiPresent_SizeChanged(DXGIPRESENT* p, int cx, int cy)
{
    return p && p->content_valid &&
           (cx != p->content_size.cx || cy != p->content_size.cy);
}

VOID WINAPI DxgiPresent_PinContent(DXGIPRESENT* p, HWND hWnd)
{
    if (!p || !p->visual || !p->dcomp || !p->content_valid)
        return;

    RECT rc = {};
    POINT pt = { 0, 0 };
    if (!ClientToScreen(hWnd, &pt) || !GetClientRect(hWnd, &rc))
        return;
    /* Only a RESIZE can move the origin out from under the content. */
    if ((rc.right - rc.left) == p->content_size.cx && (rc.bottom - rc.top) == p->content_size.cy)
        return;

    const LONG dx = p->content_origin.x - pt.x;
    const LONG dy = p->content_origin.y - pt.y;
    if (dx == 0 && dy == 0 && !p->offset_active)
        return;

    p->visual->SetOffsetX((float)dx);
    p->visual->SetOffsetY((float)dy);
    p->dcomp->Commit();
    p->offset_active = (dx != 0 || dy != 0);
}

VOID WINAPI DxgiPresent_WaitForCommit(DXGIPRESENT* p)
{
    if (p && p->dcomp)
    {
        p->dcomp->Commit();
        p->dcomp->WaitForCommitCompletion();
    }
}

VOID WINAPI DxgiPresent_WaitForVBlank(DXGIPRESENT* p, HWND hWnd)
{
    if (!p)
        return;
    if (p->frame_latency_waitable)
        WaitForSingleObject(p->frame_latency_waitable, 0);   /* zero-timeout poll (reference) */

    if (p->vblank_adapter == 0)
    {
        DWF_D3DKMT_OPENADAPTERFROMHDC oa = {};
        oa.hDc = GetDC(hWnd);
        const LONG status = D3DKMTOpenAdapterFromHdc(&oa);
        ReleaseDC(hWnd, oa.hDc);
        if (status != 0)
            return;
        p->vblank_adapter = oa.hAdapter;
        p->vblank_source  = oa.VidPnSourceId;
        return;   /* first call only opens the adapter (reference) */
    }

    DWF_D3DKMT_WAITFORVERTICALBLANKEVENT vbe = {};
    vbe.hAdapter      = p->vblank_adapter;
    vbe.VidPnSourceId = p->vblank_source;
    if (D3DKMTWaitForVerticalBlankEvent(&vbe) != 0)
        return;

    DWF_D3DKMT_GETSCANLINE gsl = {};
    gsl.hAdapter      = p->vblank_adapter;
    gsl.VidPnSourceId = p->vblank_source;
    LONG status = 0;
    int  guard  = 0;
    do
    {
        status = D3DKMTGetScanLine(&gsl);
    } while ((status != 0 || gsl.InVerticalBlank) && ++guard < 100000);
}

VOID WINAPI DxgiPresent_SetModalLive(DXGIPRESENT* p, BOOL fLive)
{
    if (!p)
        return;
    p->pace_modal_live = fLive ? TRUE : FALSE;
    if (p->pace_wake)
        SetEvent(p->pace_wake);
}

VOID WINAPI DxgiPresent_PaceTickHandled(DXGIPRESENT* p)
{
    if (p)
        InterlockedExchange(&p->pace_pending, 0);
}

