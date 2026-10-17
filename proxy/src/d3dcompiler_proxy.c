#include <windows.h>

#include "log.h"

#define FHW_SYSTEM_LIBRARY L"\\D3DCompiler_47.dll"
#define FHW_PAYLOAD_NAME L"fh2wheel.dll"

void fhw_proxy_bind(HMODULE real);

static HMODULE fhw_system_library = NULL;
static HMODULE fhw_payload = NULL;

static HMODULE fhw_load_system_library(void)
{
    wchar_t path[MAX_PATH];
    UINT length = GetSystemDirectoryW(path, MAX_PATH);

    if (length == 0 || length >= MAX_PATH)
        return NULL;
    if (lstrlenW(path) + lstrlenW(FHW_SYSTEM_LIBRARY) >= MAX_PATH)
        return NULL;
    lstrcatW(path, FHW_SYSTEM_LIBRARY);
    return LoadLibraryW(path);
}

static int fhw_load_payload(HINSTANCE instance)
{
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(instance, path, MAX_PATH);

    if (length == 0 || length >= MAX_PATH)
        return 0;
    while (length > 0 && path[length - 1] != L'\\')
        length--;
    if (length == 0)
        return 0;
    path[length] = L'\0';
    if (lstrlenW(path) + lstrlenW(FHW_PAYLOAD_NAME) >= MAX_PATH)
        return 0;
    lstrcatW(path, FHW_PAYLOAD_NAME);
    fhw_payload = LoadLibraryW(path);
    return fhw_payload != NULL;
}

static void fhw_attach(HINSTANCE instance)
{
    fhw_system_library = fhw_load_system_library();
    fhw_proxy_bind(fhw_system_library);
    if (!fhw_log_open())
        return;
    if (fhw_system_library != NULL)
        fhw_log_step_ok("D3DCompiler_47 proxy: forwarding to the system compiler");
    else
        fhw_log_step_skipped("D3DCompiler_47 proxy: forwarding to the system compiler", "system DLL not found");
    if (!fhw_load_payload(instance))
        fhw_log_format("D3DCompiler_47 proxy: loading %ls -- SKIPPED -- error %lu", FHW_PAYLOAD_NAME,
                       GetLastError());
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        fhw_attach(instance);
    }
    return TRUE;
}
