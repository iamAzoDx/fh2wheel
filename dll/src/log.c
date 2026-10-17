#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#include "log.h"
#include "targets.h"

#define FHW_LOG_LINE_CAPACITY 1024

static HANDLE fhw_log_handle = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION fhw_log_lock;
static LONG fhw_log_lock_ready = 0;

static int fhw_host_directory(wchar_t *buffer, DWORD capacity)
{
    DWORD length = GetModuleFileNameW(NULL, buffer, capacity);

    if (length == 0 || length >= capacity)
        return 0;
    while (length > 0 && buffer[length - 1] != L'\\')
        length--;
    if (length == 0)
        return 0;
    buffer[length - 1] = L'\0';
    return 1;
}

static void fhw_join_path(wchar_t *destination, DWORD capacity, const wchar_t *directory, const wchar_t *leaf)
{
    lstrcpynW(destination, directory, (int)capacity);
    lstrcatW(destination, L"\\");
    lstrcatW(destination, leaf);
}

int fhw_log_open(void)
{
    wchar_t host[MAX_PATH];
    wchar_t path[MAX_PATH];

    if (InterlockedCompareExchange(&fhw_log_lock_ready, 1, 0) == 0)
        InitializeCriticalSection(&fhw_log_lock);
    if (fhw_log_handle != INVALID_HANDLE_VALUE)
        return 1;
    if (!fhw_host_directory(host, MAX_PATH))
        return 0;

    fhw_join_path(path, MAX_PATH, host, FHW_LOG_DIRECTORY_DATA);
    CreateDirectoryW(path, NULL);
    fhw_join_path(path, MAX_PATH, host, FHW_LOG_DIRECTORY_LOGS);
    CreateDirectoryW(path, NULL);
    fhw_join_path(path, MAX_PATH, host, FHW_LOG_FILE);

    fhw_log_handle = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
    return fhw_log_handle != INVALID_HANDLE_VALUE;
}

int fhw_host_path(const wchar_t *relative, wchar_t *out, DWORD capacity)
{
    wchar_t host[MAX_PATH];

    if (!fhw_host_directory(host, MAX_PATH))
        return 0;
    if ((DWORD)(lstrlenW(host) + 1 + lstrlenW(relative)) >= capacity)
        return 0;
    fhw_join_path(out, capacity, host, relative);
    return 1;
}

void fhw_log_close(void)
{
    if (fhw_log_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(fhw_log_handle);
        fhw_log_handle = INVALID_HANDLE_VALUE;
    }
}

void fhw_log_format(const char *format, ...)
{
    char line[FHW_LOG_LINE_CAPACITY];
    SYSTEMTIME now;
    DWORD written = 0;
    int prefix;
    int body;
    va_list arguments;

    if (fhw_log_handle == INVALID_HANDLE_VALUE)
        return;
    GetLocalTime(&now);
    prefix = snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u ", now.wHour, now.wMinute, now.wSecond,
                      now.wMilliseconds);
    if (prefix < 0 || prefix >= (int)sizeof(line))
        return;

    va_start(arguments, format);
    body = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix - 2, format, arguments);
    va_end(arguments);
    if (body < 0)
        return;
    if (body > (int)(sizeof(line) - (size_t)prefix - 2))
        body = (int)(sizeof(line) - (size_t)prefix - 2);

    line[prefix + body] = '\r';
    line[prefix + body + 1] = '\n';
    EnterCriticalSection(&fhw_log_lock);
    WriteFile(fhw_log_handle, line, (DWORD)(prefix + body + 2), &written, NULL);
    LeaveCriticalSection(&fhw_log_lock);
}

void fhw_log_step_ok(const char *step)
{
    fhw_log_format("%s -- OK", step);
}

void fhw_log_step_skipped(const char *step, const char *reason)
{
    fhw_log_format("%s -- SKIPPED -- %s", step, reason);
}
