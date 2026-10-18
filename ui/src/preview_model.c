#include "preview_model.h"

#define UI_GEAR_NEUTRAL 0u
#define UI_GEAR_REVERSE 0xFFu
#define UI_PERCENT_SCALE 100.0f

static float ui_clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

float ui_wheel_angle(float steering, unsigned int rotation_degrees)
{
    return ui_clamp(steering, -1.0f, 1.0f) * (float)rotation_degrees / 2.0f;
}

int ui_percent(float value)
{
    return (int)(ui_clamp(value, 0.0f, 1.0f) * UI_PERCENT_SCALE + 0.5f);
}

void ui_gear_label(unsigned char gear, int has_pattern_shifter, wchar_t *text)
{
    if (!has_pattern_shifter) {
        text[0] = L'–';
        text[1] = L'\0';
    } else if (gear == UI_GEAR_REVERSE) {
        text[0] = L'R';
        text[1] = L'\0';
    } else if (gear == UI_GEAR_NEUTRAL) {
        text[0] = L'N';
        text[1] = L'\0';
    } else {
        text[0] = (wchar_t)(L'0' + gear % 10);
        text[1] = L'\0';
    }
}
