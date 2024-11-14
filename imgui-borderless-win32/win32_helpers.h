#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>

#define MAX_ENUM_DISPLAYS (100U)
#define MILLISECONDS_TO_100NANOSECONDS(durationMs)      ((durationMs) * 1000 * 10)
#define MILLISECONDS_FROM_100NANOSECONDS(durationNanoS) ((durationNanoS) / (1000 * 10))

typedef struct win32_monitor_info_s win32_monitor_info_t;
typedef struct win32_display_info_s win32_display_info_t;

struct win32_monitor_info_s
{
    CHAR                             deviceName [CCHDEVICENAME + 1];
    WCHAR                            deviceNameW[CCHDEVICENAME + 1];
    DEVMODE                          deviceMode;
    MONITORINFOEX                    monitorInfoEx;
    DISPLAYCONFIG_TARGET_DEVICE_NAME targetName;
    DISPLAYCONFIG_ADAPTER_NAME       adapterName;
    CHAR                             monitorFriendlyDeviceName[64 + 1];
    CHAR                             monitorDevicePath[128 + 1];
    CHAR                             adapterDevicePath[128 + 1];
};

struct win32_display_info_s
{
    RECT                 boundingRect;
    UINT                 numDisplays;
    win32_monitor_info_t displays[MAX_ENUM_DISPLAYS];
};

FLOAT
win32_rect_get_width(
    CONST LPRECT r);

FLOAT
win32_rect_get_height(
    CONST LPRECT r);

INT
win32_dpi_scale(
    INT  value,
    UINT dpi);

BOOL
win32_get_display_info(
    win32_display_info_t* displayInfo);

LONG
win32_set_timer_resolution(
    ULONG  hnsDesiredResolution,
    BOOL   setResolution,
    PULONG hnsCurrentResolution);

HANDLE
win32_create_high_resolution_timer(
    LPSECURITY_ATTRIBUTES lpTimerAttributes,
    PTCHAR                lpTimerName,
    DWORD                 dwDesiredAccess
);

BOOL
win32_yield_on_high_resolution_timer(
    HANDLE               hTimer,
    CONST PLARGE_INTEGER dueTime);

BOOL
win32_hectonano_sleep(
    LONGLONG hns);