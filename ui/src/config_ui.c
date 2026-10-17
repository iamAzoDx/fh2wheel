#define COBJMACROS
#include "dinput_state.h"

#include <commctrl.h>
#include <stdio.h>

#include "config.h"
#include "input_mapping.h"
#include "ui_text.h"

#define UI_WINDOW_CLASS L"fh2wheel-configuration"
#define UI_DEVICE_CAPACITY 16
#define UI_NAME_CAPACITY 128
#define UI_TEXT_CAPACITY 160
#define UI_POLL_TIMER 1
#define UI_FORCE_TIMER 2
#define UI_POLL_MS 30
#define UI_FORCE_TEST_MS 700
#define UI_DETECT_TIMEOUT_MS 10000
#define UI_AXIS_DETECT_THRESHOLD 25000L
#define UI_PROGRESS_MAX 1000
#define UI_FORCE_TEST_LEVEL 0.3f
#define UI_PERCENT 100.0f
#define UI_BUTTON_ROWS 6
#define UI_PRESSED_MARK L"  ●"
#define UI_WIDTH 760
#define UI_HEIGHT 690

#define ID_WHEEL 100
#define ID_PEDALS 101
#define ID_REFRESH 102
#define ID_AXIS_DETECT 200
#define ID_AXIS_INVERT 220
#define ID_BUTTON_ASSIGN 300
#define ID_BUTTON_CLEAR 340
#define ID_DPAD_HAT 400
#define ID_GAIN 401
#define ID_FREQUENCY 402
#define ID_ROTATION 403
#define ID_FORCE_INVERT 404
#define ID_FORCE_ENABLED 405
#define ID_TEST_LEFT 406
#define ID_TEST_RIGHT 407
#define ID_TRACE 408
#define ID_SAVE 409

typedef enum {
    UI_MODE_IDLE,
    UI_MODE_AXIS,
    UI_MODE_BUTTON
} ui_mode;

typedef struct {
    wchar_t name[UI_NAME_CAPACITY];
    unsigned int vid;
    unsigned int pid;
    GUID instance;
} ui_device;

typedef struct {
    LPDIRECTINPUTDEVICE8W device;
    fhw_raw_state state;
    int valid;
} ui_input;

static HINSTANCE ui_instance;
static HWND ui_window;
static HFONT ui_font;
static HWND ui_wheel_combo;
static HWND ui_pedals_combo;
static HWND ui_axis_mapping[UI_AXIS_ROWS];
static HWND ui_axis_bar[UI_AXIS_ROWS];
static HWND ui_axis_invert[UI_AXIS_ROWS];
static HWND ui_button_mapping[FHW_BUTTON_COUNT];
static HWND ui_status;
static fhw_config ui_config;
static ui_device ui_devices[UI_DEVICE_CAPACITY];
static int ui_device_count = 0;
static LPDIRECTINPUT8W ui_directinput = NULL;
static ui_input ui_wheel;
static ui_input ui_pedals;
static fhw_force_effect ui_force;
static LPDIRECTINPUTDEVICE8W ui_force_device = NULL;
static ui_mode ui_current_mode = UI_MODE_IDLE;
static int ui_target = 0;
static DWORD ui_mode_started = 0;
static fhw_raw_state ui_baseline_wheel;
static fhw_raw_state ui_baseline_pedals;
static wchar_t ui_shown_mapping[FHW_BUTTON_COUNT][UI_TEXT_CAPACITY];

static void ui_set_status(const wchar_t *text)
{
    SetWindowTextW(ui_status, text);
}

static HWND ui_control(const wchar_t *type, const wchar_t *text, DWORD style, int x, int y, int width, int height,
                       int id)
{
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, ui_window,
                                   (HMENU)(INT_PTR)id, ui_instance, NULL);

    SendMessageW(control, WM_SETFONT, (WPARAM)ui_font, TRUE);
    return control;
}

static HWND ui_label(const wchar_t *text, int x, int y, int width)
{
    return ui_control(L"STATIC", text, 0, x, y + 3, width, 18, 0);
}

static BOOL CALLBACK ui_collect_device(LPCDIDEVICEINSTANCEW instance, LPVOID context)
{
    ui_device *device;

    (void)context;
    if (ui_device_count >= UI_DEVICE_CAPACITY)
        return DIENUM_STOP;
    device = &ui_devices[ui_device_count++];
    lstrcpynW(device->name, instance->tszProductName, UI_NAME_CAPACITY);
    device->vid = fhw_device_vid(instance);
    device->pid = fhw_device_pid(instance);
    device->instance = instance->guidInstance;
    return DIENUM_CONTINUE;
}

static void ui_close_force(void)
{
    fhw_force_effect_release(&ui_force);
    if (ui_force_device != NULL) {
        IDirectInputDevice8_Unacquire(ui_force_device);
        IDirectInputDevice8_Release(ui_force_device);
        ui_force_device = NULL;
    }
}

static void ui_close_input(ui_input *input)
{
    if (input == &ui_wheel)
        ui_close_force();
    if (input->device != NULL) {
        IDirectInputDevice8_Unacquire(input->device);
        IDirectInputDevice8_Release(input->device);
        input->device = NULL;
    }
    fhw_raw_clear(&input->state);
    input->valid = 0;
}

static int ui_open_input(ui_input *input, int device_index)
{
    ui_close_input(input);
    if (device_index < 0 || device_index >= ui_device_count)
        return 0;
    if (FAILED(IDirectInput8_CreateDevice(ui_directinput, &ui_devices[device_index].instance, &input->device, NULL)))
        return 0;
    IDirectInputDevice8_SetDataFormat(input->device, &c_dfDIJoystick2);
    IDirectInputDevice8_SetCooperativeLevel(input->device, ui_window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    fhw_set_full_axis_range(input->device);
    IDirectInputDevice8_Acquire(input->device);
    return 1;
}

static HRESULT ui_open_force(void)
{
    int wheel = (int)SendMessageW(ui_wheel_combo, CB_GETCURSEL, 0, 0);
    HRESULT result;

    if (ui_force.effect != NULL)
        return S_OK;
    ui_close_force();
    if (wheel < 0 || wheel >= ui_device_count)
        return E_FAIL;
    result = IDirectInput8_CreateDevice(ui_directinput, &ui_devices[wheel].instance, &ui_force_device, NULL);
    if (FAILED(result))
        return result;
    IDirectInputDevice8_SetDataFormat(ui_force_device, &c_dfDIJoystick);
    result = IDirectInputDevice8_SetCooperativeLevel(ui_force_device, ui_window, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
    if (SUCCEEDED(result))
        result = IDirectInputDevice8_Acquire(ui_force_device);
    if (SUCCEEDED(result))
        fhw_force_effect_create(ui_force_device, &ui_force);
    if (ui_force.effect == NULL) {
        ui_close_force();
        return FAILED(result) ? result : E_FAIL;
    }
    return S_OK;
}

static int ui_find_device(unsigned int vid, unsigned int pid)
{
    int index;

    for (index = 0; index < ui_device_count; index++) {
        if (ui_devices[index].vid == vid && ui_devices[index].pid == pid)
            return index;
    }
    return -1;
}

static void ui_open_selected(void)
{
    int wheel = (int)SendMessageW(ui_wheel_combo, CB_GETCURSEL, 0, 0);
    int pedals = (int)SendMessageW(ui_pedals_combo, CB_GETCURSEL, 0, 0) - 1;
    int opened;

    if (wheel >= 0 && wheel < ui_device_count) {
        ui_config.wheel_vid = ui_devices[wheel].vid;
        ui_config.wheel_pid = ui_devices[wheel].pid;
    }
    if (pedals >= 0 && pedals < ui_device_count) {
        ui_config.pedals_vid = ui_devices[pedals].vid;
        ui_config.pedals_pid = ui_devices[pedals].pid;
    } else {
        ui_config.pedals_vid = 0;
        ui_config.pedals_pid = 0;
    }
    opened = ui_open_input(&ui_wheel, wheel);
    ui_open_input(&ui_pedals, pedals);
    ui_set_status(ui_text(opened ? T_WHEEL_OPENED : T_NO_WHEEL));
}

static void ui_fill_devices(void)
{
    wchar_t text[UI_TEXT_CAPACITY];
    int index;
    int wheel;
    int pedals;

    ui_close_input(&ui_wheel);
    ui_close_input(&ui_pedals);
    ui_device_count = 0;
    IDirectInput8_EnumDevices(ui_directinput, DI8DEVCLASS_GAMECTRL, ui_collect_device, NULL, DIEDFL_ATTACHEDONLY);
    SendMessageW(ui_wheel_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(ui_pedals_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(ui_pedals_combo, CB_ADDSTRING, 0, (LPARAM)ui_text(T_PEDALS_NONE));
    for (index = 0; index < ui_device_count; index++) {
        _snwprintf(text, UI_TEXT_CAPACITY, L"%ls (%04X:%04X)", ui_devices[index].name, ui_devices[index].vid,
                   ui_devices[index].pid);
        text[UI_TEXT_CAPACITY - 1] = L'\0';
        SendMessageW(ui_wheel_combo, CB_ADDSTRING, 0, (LPARAM)text);
        SendMessageW(ui_pedals_combo, CB_ADDSTRING, 0, (LPARAM)text);
    }
    wheel = ui_find_device(ui_config.wheel_vid, ui_config.wheel_pid);
    pedals = ui_config.pedals_vid != 0 ? ui_find_device(ui_config.pedals_vid, ui_config.pedals_pid) : -1;
    SendMessageW(ui_wheel_combo, CB_SETCURSEL, (WPARAM)(wheel >= 0 ? wheel : 0), 0);
    SendMessageW(ui_pedals_combo, CB_SETCURSEL, (WPARAM)(pedals + 1), 0);
    ui_open_selected();
}

static fhw_pedal_mapping *ui_pedal_row(int row)
{
    return &ui_config.pedals[row - 1];
}

static void ui_refresh_axis_texts(void)
{
    wchar_t text[UI_TEXT_CAPACITY];
    int row;

    for (row = 0; row < UI_AXIS_ROWS; row++) {
        if (row == 0) {
            _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_AXIS_FORMAT), ui_axis_name(ui_config.steering_axis));
            SendMessageW(ui_axis_invert[row], BM_SETCHECK, ui_config.steering_inverted ? BST_CHECKED : BST_UNCHECKED,
                         0);
        } else {
            fhw_pedal_mapping *mapping = ui_pedal_row(row);

            if (mapping->axis == FHW_AXIS_NONE)
                lstrcpynW(text, ui_text(T_NONE), UI_TEXT_CAPACITY);
            else
                _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_AXIS_SOURCE_FORMAT), ui_axis_name(mapping->axis),
                           ui_text(mapping->source == FHW_SOURCE_PEDALS ? T_SOURCE_PEDALS : T_SOURCE_WHEEL));
            SendMessageW(ui_axis_invert[row], BM_SETCHECK, mapping->inverted ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        text[UI_TEXT_CAPACITY - 1] = L'\0';
        SetWindowTextW(ui_axis_mapping[row], text);
    }
}

static void ui_binding_label(int binding, int pressed, wchar_t *text)
{
    if (binding > 0)
        _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_BUTTON_FORMAT), binding, pressed ? UI_PRESSED_MARK : L"");
    else if (binding < 0 && -binding <= UI_HAT_DIRECTIONS)
        _snwprintf(text, UI_TEXT_CAPACITY, L"%ls%ls", ui_hat_name(-binding - 1), pressed ? UI_PRESSED_MARK : L"");
    else
        _snwprintf(text, UI_TEXT_CAPACITY, L"—");
    text[UI_TEXT_CAPACITY - 1] = L'\0';
}

static void ui_refresh_button_texts(unsigned int pressed)
{
    wchar_t text[UI_TEXT_CAPACITY];
    int index;

    for (index = 0; index < FHW_BUTTON_COUNT; index++) {
        ui_binding_label(ui_config.buttons[index], (pressed >> index) & 1u, text);
        if (lstrcmpW(text, ui_shown_mapping[index]) != 0) {
            lstrcpyW(ui_shown_mapping[index], text);
            SetWindowTextW(ui_button_mapping[index], text);
        }
    }
}

static int ui_read_number(int id, int fallback)
{
    BOOL ok = FALSE;
    UINT value = GetDlgItemInt(ui_window, id, &ok, FALSE);

    return ok ? (int)value : fallback;
}

static void ui_read_force_settings(void)
{
    int gain = ui_read_number(ID_GAIN, ui_config.force_gain_percent);
    int frequency = ui_read_number(ID_FREQUENCY, (int)ui_config.force_frequency_hz);
    int rotation = ui_read_number(ID_ROTATION, (int)ui_config.rotation_degrees);

    ui_config.force_gain_percent = gain > FHW_MAX_GAIN_PERCENT ? FHW_MAX_GAIN_PERCENT : gain;
    ui_config.force_frequency_hz = (unsigned int)(frequency < (int)FHW_MIN_FREQUENCY_HZ   ? (int)FHW_MIN_FREQUENCY_HZ
                                                  : frequency > (int)FHW_MAX_FREQUENCY_HZ ? (int)FHW_MAX_FREQUENCY_HZ
                                                                                          : frequency);
    ui_config.rotation_degrees = (unsigned int)rotation;
    ui_config.force_inverted = SendMessageW(GetDlgItem(ui_window, ID_FORCE_INVERT), BM_GETCHECK, 0, 0) == BST_CHECKED;
    ui_config.force_enabled = SendMessageW(GetDlgItem(ui_window, ID_FORCE_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED;
    ui_config.trace = SendMessageW(GetDlgItem(ui_window, ID_TRACE), BM_GETCHECK, 0, 0) == BST_CHECKED;
    ui_config.dpad_from_hat = SendMessageW(GetDlgItem(ui_window, ID_DPAD_HAT), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static int ui_poll(ui_input *input)
{
    DIJOYSTATE2 joystick;
    HRESULT result;

    if (input->device == NULL)
        return 0;
    IDirectInputDevice8_Poll(input->device);
    result = IDirectInputDevice8_GetDeviceState(input->device, sizeof(joystick), &joystick);
    if (FAILED(result)) {
        IDirectInputDevice8_Acquire(input->device);
        return 0;
    }
    fhw_raw_from_dinput(&joystick, &input->state);
    input->valid = 1;
    return 1;
}

static void ui_start_mode(ui_mode mode, int target)
{
    wchar_t text[UI_TEXT_CAPACITY];

    ui_current_mode = mode;
    ui_target = target;
    ui_mode_started = GetTickCount();
    ui_baseline_wheel = ui_wheel.state;
    ui_baseline_pedals = ui_pedals.state;
    if (mode == UI_MODE_AXIS)
        _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_AXIS_PROMPT_FORMAT), ui_axis_label(target), ui_axis_hint(target));
    else
        _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_BUTTON_PROMPT_FORMAT), ui_button_label((fhw_button)target));
    text[UI_TEXT_CAPACITY - 1] = L'\0';
    ui_set_status(text);
}

static int ui_strongest_axis(const fhw_raw_state *now, const fhw_raw_state *base, long *delta)
{
    int best = FHW_AXIS_NONE;
    long best_size = 0;
    int axis;

    for (axis = FHW_AXIS_X; axis < FHW_AXIS_COUNT; axis++) {
        long change = now->axes[axis] - base->axes[axis];
        long size = change < 0 ? -change : change;

        if (size > best_size) {
            best_size = size;
            best = axis;
            *delta = change;
        }
    }
    return best_size >= UI_AXIS_DETECT_THRESHOLD ? best : FHW_AXIS_NONE;
}

static void ui_detect_axis(void)
{
    long wheel_delta = 0;
    long pedals_delta = 0;
    int wheel_axis = ui_strongest_axis(&ui_wheel.state, &ui_baseline_wheel, &wheel_delta);
    int pedals_axis = ui_pedals.valid ? ui_strongest_axis(&ui_pedals.state, &ui_baseline_pedals, &pedals_delta)
                                      : FHW_AXIS_NONE;
    fhw_source source = FHW_SOURCE_WHEEL;
    int axis = wheel_axis;
    long delta = wheel_delta;
    wchar_t text[UI_TEXT_CAPACITY];

    if (pedals_axis != FHW_AXIS_NONE && (axis == FHW_AXIS_NONE || labs(pedals_delta) > labs(wheel_delta))) {
        source = FHW_SOURCE_PEDALS;
        axis = pedals_axis;
        delta = pedals_delta;
    }
    if (axis == FHW_AXIS_NONE)
        return;
    if (ui_target == 0) {
        if (source != FHW_SOURCE_WHEEL) {
            ui_set_status(ui_text(T_STEERING_FROM_PEDALS));
            ui_current_mode = UI_MODE_IDLE;
            return;
        }
        ui_config.steering_axis = (fhw_axis)axis;
        ui_config.steering_inverted = delta < 0;
    } else {
        fhw_pedal_mapping *mapping = ui_pedal_row(ui_target);

        mapping->axis = (fhw_axis)axis;
        mapping->source = source;
        mapping->inverted = delta < 0;
    }
    ui_current_mode = UI_MODE_IDLE;
    ui_refresh_axis_texts();
    _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_AXIS_DONE_FORMAT), ui_axis_label(ui_target),
               ui_axis_name((fhw_axis)axis), delta < 0 ? ui_text(T_INVERTED_SUFFIX) : L"");
    text[UI_TEXT_CAPACITY - 1] = L'\0';
    ui_set_status(text);
}

static int ui_hat_binding(unsigned long hat)
{
    static const int bindings[] = {FHW_BINDING_HAT_UP, FHW_BINDING_HAT_DOWN, FHW_BINDING_HAT_LEFT,
                                   FHW_BINDING_HAT_RIGHT};
    unsigned int index;

    for (index = 0; index < sizeof(bindings) / sizeof(bindings[0]); index++) {
        if (fhw_hat_matches(hat, bindings[index]))
            return bindings[index];
    }
    return FHW_BINDING_NONE;
}

static void ui_detect_button(void)
{
    wchar_t text[UI_TEXT_CAPACITY];
    wchar_t binding_text[UI_TEXT_CAPACITY];
    int binding = FHW_BINDING_NONE;
    int number;

    for (number = 1; number <= FHW_RAW_BUTTON_COUNT && binding == FHW_BINDING_NONE; number++) {
        if (fhw_binding_pressed(&ui_wheel.state, number) && !fhw_binding_pressed(&ui_baseline_wheel, number))
            binding = number;
    }
    if (binding == FHW_BINDING_NONE && ui_hat_binding(ui_baseline_wheel.hat) == FHW_BINDING_NONE)
        binding = ui_hat_binding(ui_wheel.state.hat);
    if (binding == FHW_BINDING_NONE)
        return;
    ui_config.buttons[ui_target] = binding;
    ui_current_mode = UI_MODE_IDLE;
    ui_binding_label(binding, 0, binding_text);
    _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_BUTTON_DONE_FORMAT), ui_button_label((fhw_button)ui_target),
               binding_text);
    text[UI_TEXT_CAPACITY - 1] = L'\0';
    ui_set_status(text);
}

static void ui_tick(void)
{
    fhw_wheel_input mapped;
    int row;

    if (!ui_poll(&ui_wheel))
        return;
    ui_poll(&ui_pedals);
    if (ui_current_mode != UI_MODE_IDLE && GetTickCount() - ui_mode_started > UI_DETECT_TIMEOUT_MS) {
        ui_current_mode = UI_MODE_IDLE;
        ui_set_status(ui_text(T_TIMEOUT));
    }
    if (ui_current_mode == UI_MODE_AXIS)
        ui_detect_axis();
    else if (ui_current_mode == UI_MODE_BUTTON)
        ui_detect_button();

    ui_config.dpad_from_hat = SendMessageW(GetDlgItem(ui_window, ID_DPAD_HAT), BM_GETCHECK, 0, 0) == BST_CHECKED;
    fhw_map_input(&ui_config, &ui_wheel.state, ui_pedals.valid ? &ui_pedals.state : NULL, &mapped);
    SendMessageW(ui_axis_bar[0], PBM_SETPOS, (WPARAM)((mapped.steering + 1.0f) * UI_PROGRESS_MAX / 2.0f), 0);
    for (row = 1; row < UI_AXIS_ROWS; row++)
        SendMessageW(ui_axis_bar[row], PBM_SETPOS, (WPARAM)(mapped.pedals[row - 1] * UI_PROGRESS_MAX), 0);
    ui_refresh_button_texts(mapped.buttons);
}

static HRESULT ui_set_force(float level)
{
    return fhw_force_effect_set(&ui_force, level, 1);
}

static void ui_test_force(float direction)
{
    wchar_t text[UI_TEXT_CAPACITY];
    HRESULT result;
    float level;

    result = ui_open_force();
    if (FAILED(result)) {
        _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_FORCE_UNAVAILABLE_FORMAT), (unsigned long)result);
        text[UI_TEXT_CAPACITY - 1] = L'\0';
        ui_set_status(text);
        return;
    }
    ui_read_force_settings();
    level = direction * UI_FORCE_TEST_LEVEL * (float)ui_config.force_gain_percent / UI_PERCENT *
            (ui_config.force_inverted ? -1.0f : 1.0f);
    result = ui_set_force(level);
    SetTimer(ui_window, UI_FORCE_TIMER, UI_FORCE_TEST_MS, NULL);
    _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_FORCE_SENT_FORMAT), level, (unsigned long)result,
               ui_text(direction > 0 ? T_RIGHT : T_LEFT));
    text[UI_TEXT_CAPACITY - 1] = L'\0';
    ui_set_status(text);
}

static void ui_save(void)
{
    ui_read_force_settings();
    ui_set_status(ui_text(fhw_config_save(&ui_config) ? T_SAVED : T_SAVE_FAILED));
}

static void ui_build(void)
{
    wchar_t number[UI_TEXT_CAPACITY];
    int row;
    int y;

    ui_label(ui_text(T_WHEEL), 12, 12, 60);
    ui_wheel_combo = ui_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 75, 12, 300, 300, ID_WHEEL);
    ui_label(ui_text(T_PEDALS), 390, 12, 65);
    ui_pedals_combo = ui_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 458, 12, 280, 300, ID_PEDALS);
    ui_control(L"BUTTON", ui_text(T_REFRESH), BS_PUSHBUTTON, 12, 44, 140, 26, ID_REFRESH);

    ui_label(ui_text(T_AXES_HEADER), 12, 82, 720);
    for (row = 0; row < UI_AXIS_ROWS; row++) {
        y = 108 + row * 32;
        ui_label(ui_axis_label(row), 12, y, 110);
        ui_axis_mapping[row] = ui_label(L"", 125, y, 150);
        ui_axis_bar[row] = ui_control(PROGRESS_CLASSW, L"", PBS_SMOOTH, 280, y + 2, 250, 20, 0);
        SendMessageW(ui_axis_bar[row], PBM_SETRANGE32, 0, UI_PROGRESS_MAX);
        ui_control(L"BUTTON", ui_text(T_DETECT), BS_PUSHBUTTON, 545, y, 85, 26, ID_AXIS_DETECT + row);
        ui_axis_invert[row] = ui_control(L"BUTTON", ui_text(T_INVERT), BS_AUTOCHECKBOX, 645, y + 3, 90, 20,
                                         ID_AXIS_INVERT + row);
    }

    ui_label(ui_text(T_BUTTONS_HEADER), 12, 278, 720);
    for (row = 0; row < FHW_BUTTON_COUNT; row++) {
        int column = row / UI_BUTTON_ROWS;
        int x = 12 + column * 375;

        y = 304 + (row % UI_BUTTON_ROWS) * 30;
        ui_label(ui_button_label((fhw_button)row), x, y, 100);
        ui_button_mapping[row] = ui_label(L"", x + 100, y, 120);
        ui_control(L"BUTTON", ui_text(T_ASSIGN), BS_PUSHBUTTON, x + 222, y, 75, 26, ID_BUTTON_ASSIGN + row);
        ui_control(L"BUTTON", ui_text(T_CLEAR), BS_PUSHBUTTON, x + 300, y, 60, 26, ID_BUTTON_CLEAR + row);
    }
    ui_control(L"BUTTON", ui_text(T_DPAD_HAT), BS_AUTOCHECKBOX, 12, 486, 400, 20, ID_DPAD_HAT);

    ui_label(ui_text(T_FORCE_HEADER), 12, 520, 720);
    ui_label(ui_text(T_GAIN), 12, 546, 50);
    _snwprintf(number, UI_TEXT_CAPACITY, L"%d", ui_config.force_gain_percent);
    ui_control(L"EDIT", number, WS_BORDER | ES_NUMBER, 65, 546, 50, 24, ID_GAIN);
    ui_label(ui_text(T_FREQUENCY), 130, 546, 85);
    _snwprintf(number, UI_TEXT_CAPACITY, L"%u", ui_config.force_frequency_hz);
    ui_control(L"EDIT", number, WS_BORDER | ES_NUMBER, 218, 546, 50, 24, ID_FREQUENCY);
    ui_label(ui_text(T_ROTATION), 283, 546, 65);
    _snwprintf(number, UI_TEXT_CAPACITY, L"%u", ui_config.rotation_degrees);
    ui_control(L"EDIT", number, WS_BORDER | ES_NUMBER, 350, 546, 50, 24, ID_ROTATION);
    ui_control(L"BUTTON", ui_text(T_FORCE_INVERT), BS_AUTOCHECKBOX, 420, 548, 140, 20, ID_FORCE_INVERT);
    ui_control(L"BUTTON", ui_text(T_FORCE_ENABLED), BS_AUTOCHECKBOX, 570, 548, 170, 20, ID_FORCE_ENABLED);
    ui_control(L"BUTTON", ui_text(T_TEST_LEFT), BS_PUSHBUTTON, 12, 580, 190, 28, ID_TEST_LEFT);
    ui_control(L"BUTTON", ui_text(T_TEST_RIGHT), BS_PUSHBUTTON, 210, 580, 190, 28, ID_TEST_RIGHT);
    ui_control(L"BUTTON", ui_text(T_TRACE), BS_AUTOCHECKBOX, 420, 584, 200, 20, ID_TRACE);

    ui_status = ui_label(L"", 12, 625, 600);
    ui_control(L"BUTTON", ui_text(T_SAVE), BS_DEFPUSHBUTTON, 630, 620, 110, 30, ID_SAVE);

    SendMessageW(GetDlgItem(ui_window, ID_DPAD_HAT), BM_SETCHECK, ui_config.dpad_from_hat ? BST_CHECKED : BST_UNCHECKED,
                 0);
    SendMessageW(GetDlgItem(ui_window, ID_FORCE_INVERT), BM_SETCHECK,
                 ui_config.force_inverted ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(ui_window, ID_FORCE_ENABLED), BM_SETCHECK,
                 ui_config.force_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(ui_window, ID_TRACE), BM_SETCHECK, ui_config.trace ? BST_CHECKED : BST_UNCHECKED, 0);
    ui_refresh_axis_texts();
}

static void ui_command(int id, int notification)
{
    if (id == ID_REFRESH) {
        ui_fill_devices();
    } else if ((id == ID_WHEEL || id == ID_PEDALS) && notification == CBN_SELCHANGE) {
        ui_open_selected();
    } else if (id >= ID_AXIS_DETECT && id < ID_AXIS_DETECT + UI_AXIS_ROWS) {
        ui_start_mode(UI_MODE_AXIS, id - ID_AXIS_DETECT);
    } else if (id >= ID_AXIS_INVERT && id < ID_AXIS_INVERT + UI_AXIS_ROWS) {
        int row = id - ID_AXIS_INVERT;
        int checked = SendMessageW(ui_axis_invert[row], BM_GETCHECK, 0, 0) == BST_CHECKED;

        if (row == 0)
            ui_config.steering_inverted = checked;
        else
            ui_pedal_row(row)->inverted = checked;
    } else if (id >= ID_BUTTON_ASSIGN && id < ID_BUTTON_ASSIGN + FHW_BUTTON_COUNT) {
        ui_start_mode(UI_MODE_BUTTON, id - ID_BUTTON_ASSIGN);
    } else if (id >= ID_BUTTON_CLEAR && id < ID_BUTTON_CLEAR + FHW_BUTTON_COUNT) {
        ui_config.buttons[id - ID_BUTTON_CLEAR] = FHW_BINDING_NONE;
    } else if (id == ID_TEST_LEFT) {
        ui_test_force(-1.0f);
    } else if (id == ID_TEST_RIGHT) {
        ui_test_force(1.0f);
    } else if (id == ID_SAVE) {
        ui_save();
    }
}

static LRESULT CALLBACK ui_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_COMMAND:
        ui_command(LOWORD(wparam), HIWORD(wparam));
        return 0;
    case WM_TIMER:
        if (wparam == UI_POLL_TIMER) {
            ui_tick();
        } else if (wparam == UI_FORCE_TIMER) {
            KillTimer(window, UI_FORCE_TIMER);
            ui_set_force(0.0f);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wparam, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY:
        ui_close_input(&ui_wheel);
        ui_close_input(&ui_pedals);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show)
{
    INITCOMMONCONTROLSEX controls;
    WNDCLASSW window_class;
    NONCLIENTMETRICSW metrics;
    RECT bounds = {0, 0, UI_WIDTH, UI_HEIGHT};
    MSG message;

    (void)previous;
    (void)command_line;
    ui_instance = instance;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    ui_font = CreateFontIndirectW(&metrics.lfMessageFont);
    fhw_config_load(&ui_config);
    fhw_raw_clear(&ui_wheel.state);
    fhw_raw_clear(&ui_pedals.state);

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = ui_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    window_class.lpszClassName = UI_WINDOW_CLASS;
    RegisterClassW(&window_class);
    AdjustWindowRect(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    ui_window = CreateWindowExW(0, UI_WINDOW_CLASS, ui_text(T_TITLE),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
                                NULL, NULL, instance, NULL);
    ui_build();
    if (FAILED(DirectInput8Create(instance, DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void **)&ui_directinput,
                                  NULL))) {
        MessageBoxW(ui_window, ui_text(T_NO_DIRECTINPUT), ui_text(T_TITLE), MB_ICONERROR);
        return 1;
    }
    ui_fill_devices();
    SetTimer(ui_window, UI_POLL_TIMER, UI_POLL_MS, NULL);
    ShowWindow(ui_window, show);
    UpdateWindow(ui_window);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
