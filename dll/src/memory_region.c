#include <windows.h>
#include <stdio.h>

#include "memory_region.h"

#define FHW_UNREADABLE_PROTECTION (PAGE_NOACCESS | PAGE_GUARD)
#define FHW_EXECUTABLE_PROTECTION (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)
#define FHW_MODULE_LOOKUP_FLAGS (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT)

static int fhw_query_committed(const void *address, MEMORY_BASIC_INFORMATION *information)
{
    if (VirtualQuery(address, information, sizeof(*information)) != sizeof(*information))
        return 0;
    if (information->State != MEM_COMMIT)
        return 0;
    return (information->Protect & FHW_UNREADABLE_PROTECTION) == 0;
}

int fhw_region_is_readable(const void *address)
{
    MEMORY_BASIC_INFORMATION information;

    return fhw_query_committed(address, &information);
}

int fhw_range_is_readable(const void *address, size_t length)
{
    const char *first = (const char *)address;

    if (address == NULL || length == 0)
        return 0;
    return fhw_region_is_readable(first) && fhw_region_is_readable(first + length - 1);
}

int fhw_region_is_executable(const void *address)
{
    MEMORY_BASIC_INFORMATION information;

    if (!fhw_query_committed(address, &information))
        return 0;
    return (information.Protect & FHW_EXECUTABLE_PROTECTION) != 0;
}

void fhw_describe_address(const void *address, char *buffer, size_t capacity)
{
    HMODULE module = NULL;
    char path[MAX_PATH];
    const char *leaf;
    DWORD length;

    if (!GetModuleHandleExA(FHW_MODULE_LOOKUP_FLAGS, (LPCSTR)address, &module)) {
        snprintf(buffer, capacity, "outside any module");
        return;
    }
    length = GetModuleFileNameA(module, path, sizeof(path));
    if (length == 0 || length >= sizeof(path)) {
        snprintf(buffer, capacity, "unnamed module+0x%llX",
                 (unsigned long long)((const char *)address - (const char *)module));
        return;
    }
    leaf = path + length;
    while (leaf > path && leaf[-1] != '\\')
        leaf--;
    snprintf(buffer, capacity, "%s+0x%llX", leaf, (unsigned long long)((const char *)address - (const char *)module));
}
