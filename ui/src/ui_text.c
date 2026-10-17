#include "ui_text.h"

static const wchar_t *const ui_texts[T_COUNT] = {
    [T_TITLE] = L"fh2wheel — wheel setup",
    [T_WHEEL] = L"Wheel:",
    [T_PEDALS] = L"Pedals:",
    [T_REFRESH] = L"Refresh list",
    [T_PEDALS_NONE] = L"None (pedals on the wheel)",
    [T_AXES_HEADER] = L"AXES — click “Detect”, then move the axis all the way. The bar shows what the game gets.",
    [T_DETECT] = L"Detect",
    [T_INVERT] = L"Invert",
    [T_BUTTONS_HEADER] = L"BUTTONS — click “Assign”, then press the wheel button. ● = currently pressed.",
    [T_ASSIGN] = L"Assign",
    [T_CLEAR] = L"Clear",
    [T_DPAD_HAT] = L"D-pad also follows the wheel's hat switch (POV)",
    [T_FORCE_HEADER] = L"FORCE FEEDBACK — test with the game closed.",
    [T_GAIN] = L"Gain %",
    [T_FREQUENCY] = L"Rate Hz",
    [T_ROTATION] = L"Rotation °",
    [T_FORCE_INVERT] = L"Invert force",
    [T_FORCE_ENABLED] = L"Force feedback on",
    [T_TEST_LEFT] = L"Test: push left",
    [T_TEST_RIGHT] = L"Test: push right",
    [T_TRACE] = L"Detailed log (trace)",
    [T_SAVE] = L"Save",
    [T_WHEEL_OPENED] = L"Wheel opened. Pick a control to set up.",
    [T_NO_WHEEL] = L"No wheel selected, or the wheel cannot be opened.",
    [T_AXIS_FORMAT] = L"axis %ls",
    [T_NONE] = L"none",
    [T_AXIS_SOURCE_FORMAT] = L"axis %ls (%ls)",
    [T_SOURCE_PEDALS] = L"pedals",
    [T_SOURCE_WHEEL] = L"wheel",
    [T_BUTTON_FORMAT] = L"button %d%ls",
    [T_AXIS_PROMPT_FORMAT] = L"%ls: %ls…",
    [T_BUTTON_PROMPT_FORMAT] = L"%ls: press the wheel button to use…",
    [T_STEERING_FROM_PEDALS] = L"Steering must come from the wheel, not the pedals. Try again.",
    [T_AXIS_DONE_FORMAT] = L"%ls set to axis %ls%ls. Let go, then check the bar.",
    [T_INVERTED_SUFFIX] = L" (inverted)",
    [T_BUTTON_DONE_FORMAT] = L"%ls set to %ls.",
    [T_TIMEOUT] = L"Nothing detected in 10 seconds, cancelled.",
    [T_FORCE_UNAVAILABLE_FORMAT] = L"Force feedback unavailable (%08lX): is the game running?",
    [T_FORCE_SENT_FORMAT] = L"Force %+.2f sent (%08lX): the wheel should turn %ls.",
    [T_RIGHT] = L"RIGHT",
    [T_LEFT] = L"LEFT",
    [T_SAVED] = L"Saved to data\\fh2wheel.ini. Restart the game to apply.",
    [T_SAVE_FAILED] = L"Could not save data\\fh2wheel.ini.",
    [T_NO_DIRECTINPUT] = L"DirectInput is not available.",
};
static const wchar_t *const ui_axis_labels[UI_AXIS_ROWS] = {L"Steering", L"Throttle", L"Brake", L"Clutch",
                                                            L"Handbrake"};
static const wchar_t *const ui_axis_hints[UI_AXIS_ROWS] = {
    L"Turn the wheel to the RIGHT (fairly far)", L"Press the throttle all the way", L"Press the brake all the way",
    L"Press the clutch all the way", L"Pull the handbrake all the way"};
static const wchar_t *const ui_button_labels[FHW_BUTTON_COUNT] = {
    L"A", L"B", L"X", L"Y", L"View", L"Menu", L"D-pad up", L"D-pad down", L"D-pad left", L"D-pad right",
    L"Left paddle", L"Right paddle"};
static const wchar_t *const ui_axis_names[FHW_AXIS_COUNT] = {L"none", L"X",  L"Y",       L"Z",      L"RX",
                                                             L"RY",   L"RZ", L"Slider0", L"Slider1"};
static const wchar_t *const ui_hat_names[UI_HAT_DIRECTIONS] = {L"hat up", L"hat down", L"hat left", L"hat right"};

const wchar_t *ui_text(ui_text_id id)
{
    return (unsigned int)id < T_COUNT && ui_texts[id] != NULL ? ui_texts[id] : L"";
}

const wchar_t *ui_axis_label(int row)
{
    return row >= 0 && row < UI_AXIS_ROWS ? ui_axis_labels[row] : L"";
}

const wchar_t *ui_axis_hint(int row)
{
    return row >= 0 && row < UI_AXIS_ROWS ? ui_axis_hints[row] : L"";
}

const wchar_t *ui_button_label(fhw_button button)
{
    return (unsigned int)button < FHW_BUTTON_COUNT ? ui_button_labels[button] : L"";
}

const wchar_t *ui_axis_name(fhw_axis axis)
{
    return (unsigned int)axis < FHW_AXIS_COUNT ? ui_axis_names[axis] : L"?";
}

const wchar_t *ui_hat_name(int direction)
{
    return direction >= 0 && direction < UI_HAT_DIRECTIONS ? ui_hat_names[direction] : L"?";
}

int ui_text_complete(void)
{
    int index;

    for (index = 0; index < T_COUNT; index++) {
        if (ui_texts[index] == NULL)
            return 0;
    }
    return 1;
}
