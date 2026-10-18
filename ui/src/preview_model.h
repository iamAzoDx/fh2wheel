#ifndef FHW_PREVIEW_MODEL_H
#define FHW_PREVIEW_MODEL_H

#include <wchar.h>

#define UI_GEAR_LABEL_CAPACITY 4

float ui_wheel_angle(float steering, unsigned int rotation_degrees);
int ui_percent(float value);
void ui_gear_label(unsigned char gear, int has_pattern_shifter, wchar_t *text);

#endif
