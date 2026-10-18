#ifndef FHW_IAT_HOOK_H
#define FHW_IAT_HOOK_H

#include <windows.h>

int fhw_chain_iat_slot(void **slot, void *replacement, void *volatile *next);

#endif
