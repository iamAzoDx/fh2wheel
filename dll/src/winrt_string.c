#include <windows.h>

#include "memory_region.h"
#include "targets.h"
#include "winrt_string.h"

#define FHW_ASCII_LIMIT 0x7Fu
#define FHW_ASCII_REPLACEMENT '?'

typedef const wchar_t *(*fhw_string_buffer_function)(void *string, unsigned int *length);
typedef HRESULT (*fhw_create_string_function)(const wchar_t *text, unsigned int length, void **string);

static void *fhw_game_import(unsigned long long slot_address)
{
    void *const *slot = (void *const *)(UINT_PTR)slot_address;

    if (!fhw_range_is_readable(slot, sizeof(*slot)) || !fhw_region_is_executable(*slot))
        return NULL;
    return *slot;
}

int fhw_game_string_ascii(void *string, char *out, size_t capacity)
{
    fhw_string_buffer_function buffer = (fhw_string_buffer_function)fhw_game_import(FHW_GAME_STRING_BUFFER_IAT_SLOT);
    const wchar_t *characters;
    unsigned int length = 0;
    size_t index;

    if (capacity == 0)
        return 0;
    out[0] = '\0';
    if (string == NULL)
        return 1;
    if (buffer == NULL)
        return 0;
    characters = buffer(string, &length);
    if (characters == NULL)
        return 1;
    if (length >= capacity)
        return 0;
    for (index = 0; index < length; index++)
        out[index] = characters[index] <= FHW_ASCII_LIMIT ? (char)characters[index] : FHW_ASCII_REPLACEMENT;
    out[length] = '\0';
    return 1;
}

HRESULT fhw_create_game_string(const wchar_t *text, void **string)
{
    fhw_create_string_function create = (fhw_create_string_function)fhw_game_import(FHW_GAME_CREATE_STRING_IAT_SLOT);

    *string = NULL;
    if (create == NULL)
        return E_FAIL;
    return create(text, (unsigned int)lstrlenW(text), string);
}
