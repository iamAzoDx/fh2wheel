#ifndef FHW_LOG_H
#define FHW_LOG_H

#include <windows.h>

int fhw_log_open(void);
void fhw_log_close(void);
int fhw_host_path(const wchar_t *relative, wchar_t *out, DWORD capacity);
void fhw_log_format(const char *format, ...);
void fhw_log_step_ok(const char *step);
void fhw_log_step_skipped(const char *step, const char *reason);

#endif
