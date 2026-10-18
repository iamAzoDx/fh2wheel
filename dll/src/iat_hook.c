#include "iat_hook.h"

int fhw_chain_iat_slot(void **slot, void *replacement, void *volatile *next)
{
    DWORD previous_protection;
    void *current;

    if (slot == NULL || replacement == NULL || next == NULL)
        return 0;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &previous_protection))
        return 0;

    do {
        current = *(void *volatile *)slot;
        if (current == NULL)
            break;
        *next = current;
    } while (InterlockedCompareExchangePointer(slot, replacement, current) != current);

    VirtualProtect(slot, sizeof(void *), previous_protection, &previous_protection);
    return current != NULL;
}
