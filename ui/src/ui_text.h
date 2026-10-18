#ifndef FHW_UI_TEXT_H
#define FHW_UI_TEXT_H

#include <wchar.h>

#include "config.h"

#define UI_AXIS_ROWS 5
#define UI_HAT_DIRECTIONS 4

typedef enum {
    T_TITLE,
    T_WHEEL,
    T_PEDALS,
    T_REFRESH,
    T_PEDALS_NONE,
    T_AXES_HEADER,
    T_DETECT,
    T_INVERT,
    T_BUTTONS_HEADER,
    T_ASSIGN,
    T_CLEAR,
    T_DPAD_HAT,
    T_FORCE_HEADER,
    T_GAIN,
    T_FREQUENCY,
    T_ROTATION,
    T_FORCE_INVERT,
    T_FORCE_ENABLED,
    T_TEST_LEFT,
    T_TEST_RIGHT,
    T_TRACE,
    T_SAVE,
    T_WHEEL_OPENED,
    T_NO_WHEEL,
    T_AXIS_FORMAT,
    T_NONE,
    T_AXIS_SOURCE_FORMAT,
    T_SOURCE_PEDALS,
    T_SOURCE_WHEEL,
    T_BUTTON_FORMAT,
    T_AXIS_PROMPT_FORMAT,
    T_BUTTON_PROMPT_FORMAT,
    T_STEERING_FROM_PEDALS,
    T_AXIS_DONE_FORMAT,
    T_INVERTED_SUFFIX,
    T_BUTTON_DONE_FORMAT,
    T_TIMEOUT,
    T_FORCE_UNAVAILABLE_FORMAT,
    T_FORCE_SENT_FORMAT,
    T_RIGHT,
    T_LEFT,
    T_SAVED,
    T_SAVE_FAILED,
    T_NO_DIRECTINPUT,
    T_SHIFTER,
    T_SHIFTER_SAME,
    T_SHIFTER_HEADER,
    T_SHIFTER_PROMPT_FORMAT,
    T_HANDBRAKE,
    T_HANDBRAKE_NONE,
    T_SOURCE_HANDBRAKE,
    T_COUNT
} ui_text_id;

const wchar_t *ui_text(ui_text_id id);
const wchar_t *ui_axis_label(int row);
const wchar_t *ui_axis_hint(int row);
const wchar_t *ui_button_label(fhw_button button);
const wchar_t *ui_shifter_label(fhw_shifter_control control);
const wchar_t *ui_axis_name(fhw_axis axis);
const wchar_t *ui_hat_name(int direction);
int ui_text_complete(void);

#endif
