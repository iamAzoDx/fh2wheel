#ifndef FHW_IAT_HOOK_H
#define FHW_IAT_HOOK_H

#include <windows.h>

void **fhw_find_iat_slot(HMODULE module, const char *dll_name, const char *function_name);
int fhw_chain_iat_slot(void **slot, void *replacement, void *volatile *next);

#endif
