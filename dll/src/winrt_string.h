#ifndef FHW_WINRT_STRING_H
#define FHW_WINRT_STRING_H

#include <windows.h>

int fhw_game_string_ascii(void *string, char *out, size_t capacity);
HRESULT fhw_create_game_string(const wchar_t *text, void **string);

#endif
