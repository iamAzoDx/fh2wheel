#include "iat_hook.h"

static const IMAGE_NT_HEADERS64 *fhw_nt_headers(HMODULE module)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)module;
    const IMAGE_NT_HEADERS64 *nt;

    if (module == NULL || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;
    nt = (const IMAGE_NT_HEADERS64 *)((const unsigned char *)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return NULL;
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return NULL;
    return nt;
}

void **fhw_find_iat_slot(HMODULE module, const char *dll_name, const char *function_name)
{
    const IMAGE_NT_HEADERS64 *nt = fhw_nt_headers(module);
    unsigned char *base = (unsigned char *)module;
    const IMAGE_DATA_DIRECTORY *directory;
    const IMAGE_IMPORT_DESCRIPTOR *descriptor;

    if (nt == NULL)
        return NULL;

    directory = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory->VirtualAddress == 0 || directory->Size == 0)
        return NULL;

    descriptor = (const IMAGE_IMPORT_DESCRIPTOR *)(base + directory->VirtualAddress);
    for (; descriptor->Name != 0; descriptor++) {
        const IMAGE_THUNK_DATA64 *names;
        IMAGE_THUNK_DATA64 *addresses;
        SIZE_T index;

        if (lstrcmpiA((const char *)(base + descriptor->Name), dll_name) != 0)
            continue;
        if (descriptor->OriginalFirstThunk == 0 || descriptor->FirstThunk == 0)
            continue;

        names = (const IMAGE_THUNK_DATA64 *)(base + descriptor->OriginalFirstThunk);
        addresses = (IMAGE_THUNK_DATA64 *)(base + descriptor->FirstThunk);
        for (index = 0; names[index].u1.AddressOfData != 0; index++) {
            const IMAGE_IMPORT_BY_NAME *imported;

            if (names[index].u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                continue;
            imported = (const IMAGE_IMPORT_BY_NAME *)(base + names[index].u1.AddressOfData);
            if (lstrcmpA((const char *)imported->Name, function_name) == 0)
                return (void **)&addresses[index].u1.Function;
        }
    }
    return NULL;
}

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
