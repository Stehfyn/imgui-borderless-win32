
#include "wglex.h"

#include <winuser.h>
#include <wingdi.h>
#include <tchar.h>
#include <winbase.h>
#include <KHR\khrplatform.h>

typedef void GLvoid;
typedef unsigned int GLenum;

typedef khronos_float_t GLfloat;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef double GLdouble;
typedef unsigned int GLuint;
typedef unsigned char GLboolean;
typedef khronos_uint8_t GLubyte;
typedef khronos_float_t GLclampf;
typedef double GLclampd;
typedef khronos_ssize_t GLsizeiptr;
typedef khronos_intptr_t GLintptr;
typedef char GLchar;
typedef khronos_int16_t GLshort;
typedef khronos_int8_t GLbyte;
typedef khronos_uint16_t GLushort;
typedef khronos_uint16_t GLhalf;
typedef struct __GLsync* GLsync;
typedef khronos_uint64_t GLuint64;
typedef khronos_int64_t GLint64;
#include <GL\glext.h>
#include <GL\wglext.h>
#define IMAGE_FILE_OPENGL32 (_T("opengl32.dll"))

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
static HMODULE __opengl32;
static ATOM __wglexDummyWindowFodderAtom;

PFNWGLGETPROCADDRESS              __wglGetProcAddress;
PFNWGLCHOOSEPIXELFORMATARBPROC    __wglChoosePixelFormatARB;
PFNWGLCREATECONTEXTATTRIBSARBPROC __wglCreateContextAttribsARB;
PFNWGLMAKECONTEXTCURRENTARBPROC   __wglMakeContextCurrentARB;
PFNWGLSWAPINTERVALEXTPROC         __wglSwapIntervalEXT;
#define D3DKMT_PTR(Type, Name) Type Name
typedef LONG NTSTATUS;
typedef UINT D3DKMT_HANDLE;
typedef UINT D3DDDI_VIDEO_PRESENT_SOURCE_ID;
typedef struct _D3DKMT_OPENADAPTERFROMHDC
{
  D3DKMT_PTR(HDC, hDc);           // in:  DC that maps to a single display
  D3DKMT_HANDLE                   hAdapter;       // out: adapter handle
  LUID                            AdapterLuid;    // out: adapter LUID
  D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;  // out: VidPN source ID for that particular display
} D3DKMT_OPENADAPTERFROMHDC;
typedef struct _D3DKMT_WAITFORVERTICALBLANKEVENT
{
  D3DKMT_HANDLE                   hAdapter;      // in: adapter handle
  D3DKMT_HANDLE                   hDevice;       // in: device handle [Optional]
  D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId; // in: adapter's VidPN Source ID
} D3DKMT_WAITFORVERTICALBLANKEVENT;
typedef struct _D3DKMT_GETSCANLINE
{
  D3DKMT_HANDLE                   hAdapter;           // in: Adapter handle
  D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;      // in: Adapter's VidPN Source ID
  BOOLEAN                         InVerticalBlank;    // out: Within vertical blank
  UINT                            ScanLine;           // out: Current scan line
} D3DKMT_GETSCANLINE;
typedef struct _D3DKMT_CLOSEADAPTER
{
  D3DKMT_HANDLE   hAdapter;   // in: adapter handle
} D3DKMT_CLOSEADAPTER;
typedef struct _D3DKMT_CHECKOCCLUSION
{
  D3DKMT_PTR(HWND, hWindow);        // in:  Destination window handle
} D3DKMT_CHECKOCCLUSION;
EXTERN_C _Check_return_ NTSTATUS APIENTRY D3DKMTOpenAdapterFromHdc(_Inout_ D3DKMT_OPENADAPTERFROMHDC*);
EXTERN_C _Check_return_ NTSTATUS APIENTRY D3DKMTWaitForVerticalBlankEvent(_In_ CONST D3DKMT_WAITFORVERTICALBLANKEVENT*);
EXTERN_C _Check_return_ NTSTATUS APIENTRY D3DKMTGetScanLine(_Inout_ D3DKMT_GETSCANLINE*);
EXTERN_C _Check_return_ NTSTATUS APIENTRY D3DKMTCloseAdapter(_In_ CONST D3DKMT_CLOSEADAPTER*);
EXTERN_C _Check_return_ NTSTATUS APIENTRY D3DKMTCheckOcclusion(_In_ CONST D3DKMT_CHECKOCCLUSION*);

static
HWND
WINAPI
CreateDummyFodderWindow(
    VOID)
{
    WNDCLASS wc;
    RtlSecureZeroMemory(&wc, sizeof(wc));

    wc.hInstance     = (HINSTANCE)&__ImageBase;
    wc.lpfnWndProc   = DefWindowProc;
    wc.lpszClassName = _T("__wglexDummyWindowFodder");

    __wglexDummyWindowFodderAtom = RegisterClass(&wc);

    if (INVALID_ATOM == __wglexDummyWindowFodderAtom)
    {
      return NULL;
    }

    return CreateWindow(MAKEINTATOM(__wglexDummyWindowFodderAtom), _T(""), 0, 0, 0, 0, 0, 0, 0, (HMODULE)&__ImageBase, 0);
}

static
PIXELFORMATDESCRIPTOR
WINAPI
GetDefaultPixelFormatDescriptor(
    VOID)
{
    PIXELFORMATDESCRIPTOR pfd;
    RtlSecureZeroMemory(&pfd, sizeof(pfd));

    pfd.nSize    = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags  = PFD_SUPPORT_OPENGL;

    return pfd;
}

int
WINAPI
wglGetPixelFormat(
    HDC  hDC,
    const int* ppfAttribs)
{
    int  pf;
    HWND hwndDummyFodder;

    if (!__opengl32)
    {
      __opengl32 = LoadLibraryEx(IMAGE_FILE_OPENGL32, 0, LOAD_LIBRARY_SEARCH_SYSTEM32);

      if (!__opengl32)
      {
        return NULL;
      }

      __wglGetProcAddress = (PFNWGLGETPROCADDRESS)GetProcAddress(__opengl32, "wglGetProcAddress");

      if (!__wglGetProcAddress)
      {
        return NULL;
      }
    }

    pf = 0;
    hwndDummyFodder = CreateDummyFodderWindow();

    if (NULL != hwndDummyFodder)
    {
      int pfDummyFodder;
      HDC hDummyFodderDC;

      hDummyFodderDC = GetDC(hwndDummyFodder);

      if (NULL != hDummyFodderDC)
      {
        PIXELFORMATDESCRIPTOR pfdDummyFodder;

        pfdDummyFodder = GetDefaultPixelFormatDescriptor();
        pfDummyFodder  = ChoosePixelFormat(hDummyFodderDC, &pfdDummyFodder);

        if (NULL != pfDummyFodder)
        {
          if (0 != SetPixelFormat(hDummyFodderDC, pfDummyFodder, &pfdDummyFodder))
          {
            HGLRC hGLRC;

            hGLRC = wglCreateContext(hDummyFodderDC);

            if (NULL != hGLRC)
            {
              if (FALSE == wglMakeCurrent(hDummyFodderDC, hGLRC))
              {
                return NULL;
              }

              if (NULL != __wglGetProcAddress)
              {
                __wglChoosePixelFormatARB    = (PFNWGLCHOOSEPIXELFORMATARBPROC)__wglGetProcAddress("wglChoosePixelFormatARB");
                __wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)__wglGetProcAddress("wglCreateContextAttribsARB");
                __wglMakeContextCurrentARB   = (PFNWGLMAKECONTEXTCURRENTARBPROC)__wglGetProcAddress("wglMakeContextCurrentARB");
                __wglSwapIntervalEXT         = (PFNWGLSWAPINTERVALEXTPROC)__wglGetProcAddress("wglSwapIntervalEXT");

                if ((!__wglChoosePixelFormatARB)    ||
                    (!__wglCreateContextAttribsARB) ||
                    (!__wglMakeContextCurrentARB)   ||
                    (!__wglSwapIntervalEXT))
                {
                  return NULL;
                }
                else
                {
                  UINT nFormats;

                  while (!__wglChoosePixelFormatARB(hDC, ppfAttribs, 0, 1, &pf, &nFormats));
                }
              }

              if (FALSE == wglMakeCurrent(NULL, NULL))
              {
                return NULL;
              }

              if (FALSE == wglDeleteContext(hGLRC))
              {
                return NULL;
              }
            }
          }
        }

        if (FALSE == ReleaseDC(hwndDummyFodder, hDummyFodderDC))
        {
          return NULL;
        }

        if (FALSE == DestroyWindow(hwndDummyFodder))
        {
          return NULL;
        }

        if (FALSE == UnregisterClass(MAKEINTATOM(__wglexDummyWindowFodderAtom), (HINSTANCE)&__ImageBase))
        {
          return NULL;
        }
      }
    }

    return pf;
}

EXTERN_C
BOOL CFORCEINLINE CALLBACK
wglWaitForVerticalBlank2(
    HWND hWnd)
{
  NTSTATUS status;
  
  static D3DKMT_WAITFORVERTICALBLANKEVENT vbe = INIT_ONCE_STATIC_INIT;

  if (!vbe.hAdapter)
  {
    D3DKMT_OPENADAPTERFROMHDC oa;
    oa.hDc = GetDC(hWnd);

    status = D3DKMTOpenAdapterFromHdc(&oa);

    ReleaseDC(hWnd, oa.hDc);
    
    if (0 != status)
    {
      return FALSE;
    }

    if (!vbe.hAdapter)
    {
      return FALSE;
    }

    vbe.hAdapter      = oa.hAdapter;
    vbe.VidPnSourceId = oa.VidPnSourceId;
    vbe.hDevice       = 0;

    return TRUE;
  }
  else
  {
    D3DKMT_GETSCANLINE gsl;

    status = D3DKMTWaitForVerticalBlankEvent(&vbe);
    
    if (0 != status)
    {
      return FALSE;
    }

    gsl.hAdapter      = vbe.hAdapter;
    gsl.VidPnSourceId = vbe.VidPnSourceId;
    gsl.ScanLine        = 0;
    gsl.InVerticalBlank = 0;

    do
    {
      status = D3DKMTGetScanLine(&gsl);

    } while ((0 != status) || (gsl.InVerticalBlank));

    return TRUE;
  }
}

EXTERN_C
BOOL CFORCEINLINE CALLBACK
wglCheckOcclusion(
    HWND hWnd)
{
    NTSTATUS status;
    D3DKMT_CHECKOCCLUSION co;

    co.hWindow = hWnd;

    status = D3DKMTCheckOcclusion(&co);

    return 0 == status;
}

EXTERN_C
BOOL CFORCEINLINE CALLBACK
wglWaitForVerticalBlank(
    HWND hWnd)
{
  NTSTATUS status;
  D3DKMT_GETSCANLINE gsl;
  D3DKMT_CLOSEADAPTER ca;
  D3DKMT_OPENADAPTERFROMHDC oa;
  D3DKMT_WAITFORVERTICALBLANKEVENT vbe;

  oa.hDc = GetDC(hWnd);
  status = D3DKMTOpenAdapterFromHdc(&oa);
  ReleaseDC(hWnd, oa.hDc);
    
  if (0 != status)
  {
    return FALSE;
  }

  if (!oa.hAdapter)
  {
    return FALSE;
  }

  vbe.hAdapter      = ca.hAdapter = oa.hAdapter;
  vbe.VidPnSourceId = oa.VidPnSourceId;
  vbe.hDevice       = 0;

  status = D3DKMTWaitForVerticalBlankEvent(&vbe);
    
  if (0 != status)
  {
    status = D3DKMTCloseAdapter(&ca);

    if (0 != status)
    {
      __debugbreak();
    }

    return FALSE;
  }

  gsl.hAdapter      = vbe.hAdapter;
  gsl.VidPnSourceId = vbe.VidPnSourceId;
  gsl.ScanLine        = 0;
  gsl.InVerticalBlank = 0;

  do
  {
    status = D3DKMTGetScanLine(&gsl);

  } while ((0 != status) || gsl.InVerticalBlank);

  status = D3DKMTCloseAdapter(&ca);

  if (0 != status)
  {
    __debugbreak();
  }

  return TRUE;
}