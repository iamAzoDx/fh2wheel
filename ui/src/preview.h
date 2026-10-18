#ifndef FHW_PREVIEW_H
#define FHW_PREVIEW_H

#include <windows.h>

#include "input_mapping.h"

#define UI_PREVIEW_NAME_CAPACITY 128

typedef struct {
    fhw_wheel_input input;
    unsigned int rotation_degrees;
    int has_pattern_shifter;
    int connected;
    wchar_t device_name[UI_PREVIEW_NAME_CAPACITY];
} ui_preview_state;

HWND ui_preview_create(HWND parent, HINSTANCE instance, int x, int y, int width, int height);
void ui_preview_update(HWND preview, const ui_preview_state *state);

#endif
