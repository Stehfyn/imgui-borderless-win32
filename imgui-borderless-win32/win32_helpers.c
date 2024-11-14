#include "win32_helpers.h"

#include <stdlib.h>
#include <winternl.h>
#pragma comment(lib, "ntdll")

#define STATUS_SUCCESS                  (0)
#define STATUS_TIMER_RESOLUTION_NOT_SET (0xC0000245)

NTSYSAPI NTSTATUS NTAPI
NtSetTimerResolution(
    ULONG   DesiredResolution, 
    BOOLEAN SetResolution, 
    PULONG  CurrentResolution);

static BOOL
win32_enum_displays_proc(
    HMONITOR hMonitor,
    HDC      hDC,
    LPRECT   lpRect,
    LPARAM   lParam);

static HRESULT
win32_query_display_config(
    win32_display_info_t* displayInfo,
    PUINT32               numPathElements,
    PUINT32               numModeElements);

FLOAT
win32_rect_get_width(
    CONST LPRECT r)
{
    return (FLOAT)(r->right - r->left);
}

FLOAT 
win32_rect_get_height(
    CONST LPRECT r)
{
    return (FLOAT)(r->bottom - r->top);
}

INT
win32_dpi_scale(
    INT  value,
    UINT dpi) 
{
    return (INT)((FLOAT)value * dpi / 96);
}

BOOL 
win32_get_display_info(
    win32_display_info_t* displayInfo)
{
    (VOID)EnumDisplayMonitors(NULL,
                              NULL,
                              (MONITORENUMPROC)win32_enum_displays_proc,
                              (LPARAM)displayInfo);
    UINT32 num_path_elements = 100U;
    UINT32 num_mode_elements = 100U;
    

    (VOID)win32_query_display_config(displayInfo, &num_path_elements, &num_mode_elements);

    //UINT32 num
    //DISPLAYCONFIG_SOURC
    //(VOID)QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &num_path_elements, qdc_path_info, &num_mode_elements, qdc_mode_info, qdc_topo_info);
    return TRUE;
}

static BOOL
win32_enum_displays_proc(
    HMONITOR hMonitor,
    HDC      hDC,
    LPRECT   lpRect,
    LPARAM   lParam)
{   
    UNREFERENCED_PARAMETER(hMonitor);
    UNREFERENCED_PARAMETER(hDC);
    UNREFERENCED_PARAMETER(lpRect);

    win32_display_info_t* display_info = (win32_display_info_t*)lParam;
    win32_monitor_info_t* monitor_info = (win32_monitor_info_t*)&display_info->displays[display_info->numDisplays];
    LPMONITORINFOEX       mi_ex        = (LPMONITORINFOEX)&monitor_info->monitorInfoEx;
    LPDEVMODE             dev_mode     = (LPDEVMODE)&monitor_info->deviceMode;
    mi_ex->cbSize                      = (DWORD)sizeof(MONITORINFOEX);
    dev_mode->dmSize                   = (WORD)sizeof(DEVMODE);

    (VOID)GetMonitorInfo(hMonitor, (LPMONITORINFO)mi_ex);
    (VOID)UnionRect(&display_info->boundingRect,
                    &display_info->boundingRect,
                    lpRect);
#ifdef UNICODE
    (VOID)wcsncpy_s(monitor_info->deviceNameW, CCHDEVICENAME + 1, mi_ex->szDevice, CCHDEVICENAME);
    (VOID)WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)mi_ex->szDevice, CCHDEVICENAME, (LPSTR)monitor_info->deviceName, CCHDEVICENAME + 1, 0, NULL);
    (VOID)EnumDisplaySettings(monitor_info->deviceNameW, ENUM_CURRENT_SETTINGS, &monitor_info->deviceMode);
#else
    (VOID)strncpy_s(monitor_info->deviceName, CCHDEVICENAME + 1, mi_ex->szDevice, CCHDEVICENAME);
    (VOID)MultiByteToWideChar(CP_UTF8, 0, (LPCCH)mi_ex->szDevice, CCHDEVICENAME, (LPWSTR)monitor_info->deviceNameW, CCHDEVICENAME + 1);
    (VOID)EnumDisplaySettings(monitor_info->deviceName, ENUM_CURRENT_SETTINGS, &monitor_info->deviceMode);
#endif

    return (display_info->numDisplays++ < MAX_ENUM_DISPLAYS);
}

static HRESULT
win32_query_display_config(
    win32_display_info_t* displayInfo,
    PUINT32               numPathElements,
    PUINT32               numModeElements)
{
    UINT32 i, j;
    LONG   result;(VOID)j;
    DISPLAYCONFIG_PATH_INFO   qdc_path_info[100] = { 0 };
    DISPLAYCONFIG_MODE_INFO   qdc_mode_info[100] = { 0 };
    UINT32 flags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE;
    //DISPLAYCONFIG_TOPOLOGY_ID qdc_topo_info[100] = { 0 };

    result = GetDisplayConfigBufferSizes(flags, numPathElements, numModeElements);
    if (!SUCCEEDED(result)) return HRESULT_FROM_WIN32(result);

    result = QueryDisplayConfig(flags, numPathElements, &qdc_path_info[0], numModeElements, &qdc_mode_info[0], NULL);
    if (!SUCCEEDED(result)) return HRESULT_FROM_WIN32(result);

    for (i = 0; i < *numPathElements; ++i)
    {
        win32_monitor_info_t*            monitor_info = &displayInfo->displays[i];
        DISPLAYCONFIG_TARGET_DEVICE_NAME target_name  = { 0 };
        DISPLAYCONFIG_ADAPTER_NAME       adapter_name = { 0 };

        target_name.header.adapterId  = qdc_path_info[i].targetInfo.adapterId;
        target_name.header.id         = qdc_path_info[i].targetInfo.id;
        target_name.header.type       = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target_name.header.size       = sizeof(target_name);

        adapter_name.header.adapterId = qdc_path_info[i].targetInfo.adapterId;
        adapter_name.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
        adapter_name.header.size      = sizeof(adapter_name);

        result = DisplayConfigGetDeviceInfo(&target_name.header);
        if (!SUCCEEDED(result)) return HRESULT_FROM_WIN32(result);

        result = DisplayConfigGetDeviceInfo(&adapter_name.header);
        if (!SUCCEEDED(result)) return HRESULT_FROM_WIN32(result);

        (VOID)memcpy_s(&monitor_info->targetName,  sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME), &target_name,  sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME));
        (VOID)memcpy_s(&monitor_info->adapterName, sizeof(DISPLAYCONFIG_ADAPTER_NAME),       &adapter_name, sizeof(DISPLAYCONFIG_ADAPTER_NAME));
        (VOID)WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)target_name.monitorFriendlyDeviceName, 64, (LPSTR)monitor_info->monitorFriendlyDeviceName, 64 + 1, 0, NULL);
        (VOID)WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)target_name.monitorDevicePath,  128, (LPSTR)monitor_info->monitorDevicePath, 128 + 1, 0, NULL);
        (VOID)WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)adapter_name.adapterDevicePath, 128, (LPSTR)monitor_info->adapterDevicePath, 128 + 1, 0, NULL);
    }

    return (HRESULT)0;
}

LONG
win32_set_timer_resolution(
    ULONG  hnsDesiredResolution,
    BOOL   setResolution,
    PULONG hnsCurrentResolution)
{
    ULONG _;
    NTSTATUS status = NtSetTimerResolution(hnsDesiredResolution,
                                           (BOOLEAN)!!setResolution,
                                           (hnsCurrentResolution) ? hnsCurrentResolution : &_);
    return (LONG)status;
}

HANDLE 
win32_create_high_resolution_timer(
    LPSECURITY_ATTRIBUTES lpTimerAttributes, 
    PTCHAR                lpTimerName, 
    DWORD                 dwDesiredAccess)
{
    return CreateWaitableTimerEx(lpTimerAttributes,
                                 lpTimerName,
                                 CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                 dwDesiredAccess);
}

BOOL
win32_yield_on_high_resolution_timer(
    HANDLE               hTimer,
    CONST PLARGE_INTEGER dueTime)
{
    BOOL set = SetWaitableTimerEx(hTimer, dueTime, 0, 0, 0, NULL, 0);
    if (!set) return FALSE;

    DWORD result = WaitForSingleObjectEx(hTimer, INFINITE, TRUE);
    return (result == WAIT_OBJECT_0);
}

BOOL 
win32_hectonano_sleep(
    LONGLONG hns)
{
    LARGE_INTEGER due_time;
    due_time.QuadPart = hns;

    HANDLE timer = win32_create_high_resolution_timer(NULL, NULL, TIMER_MODIFY_STATE);
    if (timer == NULL) return FALSE;

    BOOL result = win32_yield_on_high_resolution_timer(timer, &due_time);
    (VOID)CloseHandle(timer);

    return result;
}
