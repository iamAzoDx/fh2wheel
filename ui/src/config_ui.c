#define COBJMACROS
#include "dinput_state.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <stdio.h>

#include "config.h"
#include "device_lists.h"
#include "input_mapping.h"
#include "preview.h"
#include "ui_text.h"
#include "ui_theme.h"

#define UI_WINDOW_CLASS L"fh2wheel-configuration"
#define UI_DEVICE_CAPACITY UI_DEVICE_LIST_CAPACITY
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
#define UI_SHIFTER_ROWS 5
#define UI_CONTENT_TOP 112
#define UI_FORCE_TOP (UI_CONTENT_TOP + 618)
#define UI_PRESSED_MARK L"  ●"
#define UI_FORM_WIDTH 760
#define UI_PREVIEW_WIDTH 336
#define UI_MARGIN 12
#define UI_WIDTH (UI_FORM_WIDTH + UI_PREVIEW_WIDTH + UI_MARGIN)
#define UI_HEIGHT 900
#define UI_HEADER_COUNT 4
#define UI_BUTTON_CORNER 8
#define UI_DARK_TITLE_ATTRIBUTE 20

#define ID_WHEEL 100
#define ID_PEDALS 101
#define ID_REFRESH 102
#define ID_SHIFTER 103
#define ID_HANDBRAKE 104
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
#define ID_SHIFTER_ASSIGN 500
#define ID_SHIFTER_CLEAR 540

typedef enum {
    UI_MODE_IDLE,
    UI_MODE_AXIS,
    UI_MODE_BUTTON,
    UI_MODE_SHIFTER
} ui_mode;

typedef struct {
    wchar_t name[UI_NAME_CAPACITY];
    GUID instance;
} ui_device;

typedef struct {
    LPDIRECTINPUTDEVICE8W device;
    fhw_raw_state state;
    int valid;
} ui_input;

static HINSTANCE ui_instance;
static HWND ui_window;
static HWND ui_preview;
static HFONT ui_font;
static HFONT ui_header_font;
static HBRUSH ui_background_brush;
static HBRUSH ui_card_brush;
static HWND ui_headers[UI_HEADER_COUNT];
static int ui_header_count = 0;
static const int ui_role_ids[UI_ROLE_COUNT] = {ID_WHEEL, ID_PEDALS, ID_SHIFTER, ID_HANDBRAKE};
static const ui_text_id ui_role_none_texts[UI_ROLE_COUNT] = {T_COUNT, T_PEDALS_NONE, T_SHIFTER_SAME,
                                                             T_HANDBRAKE_NONE};
static const ui_text_id ui_source_texts[FHW_SOURCE_COUNT] = {T_SOURCE_WHEEL, T_SOURCE_PEDALS, T_SOURCE_HANDBRAKE};
static HWND ui_device_combo[UI_ROLE_COUNT];
static int ui_combo_device[UI_ROLE_COUNT][UI_DEVICE_CAPACITY + 1];
static int ui_selected[UI_ROLE_COUNT];
static HWND ui_axis_mapping[UI_AXIS_ROWS];
static HWND ui_axis_bar[UI_AXIS_ROWS];
static HWND ui_axis_invert[UI_AXIS_ROWS];
static HWND ui_button_mapping[FHW_BUTTON_COUNT];
static HWND ui_shifter_mapping[FHW_SHIFTER_COUNT];
static HWND ui_status;
static fhw_config ui_config;
static ui_device ui_devices[UI_DEVICE_CAPACITY];
static ui_device_info ui_device_infos[UI_DEVICE_CAPACITY];
static int ui_device_count = 0;
static LPDIRECTINPUT8W ui_directinput = NULL;
static ui_input ui_wheel;
static ui_input ui_pedals;
static ui_input ui_shifter;
static ui_input ui_handbrake;
static fhw_force_effect ui_force;
static LPDIRECTINPUTDEVICE8W ui_force_device = NULL;
static ui_mode ui_current_mode = UI_MODE_IDLE;
static int ui_target = 0;
static DWORD ui_mode_started = 0;
static fhw_raw_state ui_baseline[FHW_SOURCE_COUNT];
static fhw_raw_state ui_baseline_shifter;
static wchar_t ui_shown_mapping[FHW_BUTTON_COUNT][UI_TEXT_CAPACITY];
static wchar_t ui_shown_shifter[FHW_SHIFTER_COUNT][UI_TEXT_CAPACITY];

static void ui_refresh_axis_texts(void);

static void ui_set_status(const wchar_t *text)
{
    SetWindowTextW(ui_status, text);
}

static int ui_is_push_button(const wchar_t *type, DWORD style)
{
    return lstrcmpW(type, L"BUTTON") == 0 && (style & BS_TYPEMASK) <= BS_DEFPUSHBUTTON;
}

static HWND ui_control(const wchar_t *type, const wchar_t *text, DWORD style, int x, int y, int width, int height,
                       int id)
{
    HWND control;

    if (ui_is_push_button(type, style))
        style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, ui_window,
                              (HMENU)(INT_PTR)id, ui_instance, NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)ui_font, TRUE);
    return control;
}

static HWND ui_label(const wchar_t *text, int x, int y, int width)
{
    return ui_control(L"STATIC", text, 0, x, y + 3, width, 18, 0);
}

static void ui_header(const wchar_t *text, int x, int y, int width)
{
    HWND header = ui_label(text, x, y, width);

    SendMessageW(header, WM_SETFONT, (WPARAM)ui_header_font, TRUE);
    if (ui_header_count < UI_HEADER_COUNT)
        ui_headers[ui_header_count++] = header;
}

static int ui_is_header(HWND control)
{
    int index;

    for (index = 0; index < ui_header_count; index++) {
        if (ui_headers[index] == control)
            return 1;
    }
    return 0;
}

static void ui_draw_button(const DRAWITEMSTRUCT *item)
{
    wchar_t text[UI_TEXT_CAPACITY];
    int pressed = (item->itemState & ODS_SELECTED) != 0;
    int primary = item->CtlID == ID_SAVE;
    COLORREF fill = primary ? (pressed ? UI_COLOR_ACCENT_PRESSED : UI_COLOR_ACCENT)
                            : (pressed ? UI_COLOR_CARD_HOVER : UI_COLOR_CARD);
    HBRUSH background = ui_background_brush;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, primary ? fill : UI_COLOR_BORDER);
    HGDIOBJ old_brush;
    HGDIOBJ old_pen;
    RECT box = item->rcItem;

    FillRect(item->hDC, &box, background);
    old_brush = SelectObject(item->hDC, brush);
    old_pen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, box.left, box.top, box.right, box.bottom, UI_BUTTON_CORNER, UI_BUTTON_CORNER);
    SelectObject(item->hDC, old_brush);
    SelectObject(item->hDC, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);

    GetWindowTextW(item->hwndItem, text, UI_TEXT_CAPACITY);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, UI_COLOR_TEXT);
    DrawTextW(item->hDC, text, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (item->itemState & ODS_FOCUS) {
        InflateRect(&box, -3, -3);
        DrawFocusRect(item->hDC, &box);
    }
}

static LRESULT ui_color_static(HDC dc, HWND control)
{
    SetTextColor(dc, ui_is_header(control) ? UI_COLOR_ACCENT : (control == ui_status ? UI_COLOR_MUTED : UI_COLOR_TEXT));
    SetBkColor(dc, UI_COLOR_BACKGROUND);
    return (LRESULT)ui_background_brush;
}

static LRESULT ui_color_field(HDC dc)
{
    SetTextColor(dc, UI_COLOR_TEXT);
    SetBkColor(dc, UI_COLOR_CARD);
    return (LRESULT)ui_card_brush;
}

static BOOL CALLBACK ui_collect_device(LPCDIDEVICEINSTANCEW instance, LPVOID context)
{
    ui_device *device;

    (void)context;
    if (ui_device_count >= UI_DEVICE_CAPACITY)
        return DIENUM_STOP;
    device = &ui_devices[ui_device_count];
    lstrcpynW(device->name, instance->tszProductName, UI_NAME_CAPACITY);
    device->instance = instance->guidInstance;
    ui_device_infos[ui_device_count].vid = fhw_device_vid(instance);
    ui_device_infos[ui_device_count].pid = fhw_device_pid(instance);
    ui_device_infos[ui_device_count].force_feedback = 0;
    ui_device_count++;
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK ui_mark_force_feedback(LPCDIDEVICEINSTANCEW instance, LPVOID context)
{
    int index;

    (void)context;
    for (index = 0; index < ui_device_count; index++) {
        if (IsEqualGUID(&ui_devices[index].instance, &instance->guidInstance))
            ui_device_infos[index].force_feedback = 1;
    }
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
    int wheel = ui_selected[UI_ROLE_WHEEL];
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

    if (vid == 0)
        return UI_NO_DEVICE;
    for (index = 0; index < ui_device_count; index++) {
        if (ui_device_infos[index].vid == vid && ui_device_infos[index].pid == pid)
            return index;
    }
    return UI_NO_DEVICE;
}

static void ui_store_device(int device, unsigned int *vid, unsigned int *pid)
{
    *vid = device != UI_NO_DEVICE ? ui_device_infos[device].vid : 0;
    *pid = device != UI_NO_DEVICE ? ui_device_infos[device].pid : 0;
}

static void ui_open_selected(void)
{
    int opened;

    if (ui_selected[UI_ROLE_WHEEL] != UI_NO_DEVICE)
        ui_store_device(ui_selected[UI_ROLE_WHEEL], &ui_config.wheel_vid, &ui_config.wheel_pid);
    ui_store_device(ui_selected[UI_ROLE_PEDALS], &ui_config.pedals_vid, &ui_config.pedals_pid);
    ui_store_device(ui_selected[UI_ROLE_SHIFTER], &ui_config.shifter_vid, &ui_config.shifter_pid);
    ui_store_device(ui_selected[UI_ROLE_HANDBRAKE], &ui_config.handbrake_vid, &ui_config.handbrake_pid);
    opened = ui_open_input(&ui_wheel, ui_selected[UI_ROLE_WHEEL]);
    ui_open_input(&ui_pedals, ui_selected[UI_ROLE_PEDALS]);
    ui_open_input(&ui_shifter, ui_selected[UI_ROLE_SHIFTER]);
    ui_open_input(&ui_handbrake, ui_selected[UI_ROLE_HANDBRAKE]);
    ui_set_status(ui_text(opened ? T_WHEEL_OPENED : T_NO_WHEEL));
}

static void ui_fill_combos(void)
{
    wchar_t text[UI_TEXT_CAPACITY];
    int choices[UI_DEVICE_CAPACITY];
    int role;

    for (role = 0; role < UI_ROLE_COUNT; role++) {
        HWND combo = ui_device_combo[role];
        int count = ui_role_choices(ui_device_infos, ui_device_count, ui_selected, (ui_role)role, choices);
        int item = 0;
        int index;

        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        SendMessageW(combo, CB_SETCURSEL, (WPARAM)-1, 0);
        if (role != UI_ROLE_WHEEL) {
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)ui_text(ui_role_none_texts[role]));
            ui_combo_device[role][item++] = UI_NO_DEVICE;
        }
        for (index = 0; index < count; index++) {
            const ui_device_info *info = &ui_device_infos[choices[index]];

            _snwprintf(text, UI_TEXT_CAPACITY, L"%ls (%04X:%04X)", ui_devices[choices[index]].name, info->vid,
                       info->pid);
            text[UI_TEXT_CAPACITY - 1] = L'\0';
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
            ui_combo_device[role][item++] = choices[index];
        }
        for (index = 0; index < item; index++) {
            if (ui_combo_device[role][index] == ui_selected[role])
                SendMessageW(combo, CB_SETCURSEL, (WPARAM)index, 0);
        }
    }
}

static void ui_read_from_wheel_when_same_device(ui_role role, fhw_source source)
{
    int index;

    if (ui_selected[role] == UI_NO_DEVICE || ui_selected[role] != ui_selected[UI_ROLE_WHEEL])
        return;
    for (index = 0; index < FHW_PEDAL_COUNT; index++) {
        if (ui_config.pedals[index].source == source)
            ui_config.pedals[index].source = FHW_SOURCE_WHEEL;
    }
}

static void ui_fill_devices(void)
{
    ui_close_input(&ui_wheel);
    ui_close_input(&ui_pedals);
    ui_close_input(&ui_shifter);
    ui_close_input(&ui_handbrake);
    ui_device_count = 0;
    IDirectInput8_EnumDevices(ui_directinput, DI8DEVCLASS_GAMECTRL, ui_collect_device, NULL, DIEDFL_ATTACHEDONLY);
    IDirectInput8_EnumDevices(ui_directinput, DI8DEVCLASS_GAMECTRL, ui_mark_force_feedback, NULL,
                              DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    ui_selected[UI_ROLE_WHEEL] = ui_find_device(ui_config.wheel_vid, ui_config.wheel_pid);
    ui_selected[UI_ROLE_PEDALS] = ui_find_device(ui_config.pedals_vid, ui_config.pedals_pid);
    ui_selected[UI_ROLE_SHIFTER] = ui_find_device(ui_config.shifter_vid, ui_config.shifter_pid);
    ui_selected[UI_ROLE_HANDBRAKE] = ui_find_device(ui_config.handbrake_vid, ui_config.handbrake_pid);
    ui_read_from_wheel_when_same_device(UI_ROLE_PEDALS, FHW_SOURCE_PEDALS);
    ui_read_from_wheel_when_same_device(UI_ROLE_HANDBRAKE, FHW_SOURCE_HANDBRAKE);
    ui_drop_conflicts(ui_selected);
    ui_fill_combos();
    ui_open_selected();
    ui_refresh_axis_texts();
}

static void ui_select_device(int id)
{
    int role;
    int item;

    for (role = 0; role < UI_ROLE_COUNT; role++) {
        if (ui_role_ids[role] != id)
            continue;
        item = (int)SendMessageW(ui_device_combo[role], CB_GETCURSEL, 0, 0);
        if (item < 0)
            return;
        ui_selected[role] = ui_combo_device[role][item];
        ui_fill_combos();
        ui_open_selected();
        return;
    }
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
                           ui_text(ui_source_texts[mapping->source]));
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

static void ui_refresh_shifter_texts(unsigned int pressed)
{
    wchar_t text[UI_TEXT_CAPACITY];
    int index;

    for (index = 0; index < FHW_SHIFTER_COUNT; index++) {
        ui_binding_label(ui_config.shifter[index], (pressed >> index) & 1u, text);
        if (lstrcmpW(text, ui_shown_shifter[index]) != 0) {
            lstrcpyW(ui_shown_shifter[index], text);
            SetWindowTextW(ui_shifter_mapping[index], text);
        }
    }
}

static ui_input *ui_source_input(fhw_source source)
{
    switch (source) {
    case FHW_SOURCE_PEDALS:
        return &ui_pedals;
    case FHW_SOURCE_HANDBRAKE:
        return &ui_handbrake;
    default:
        return &ui_wheel;
    }
}

static ui_input *ui_shifter_source(void)
{
    return ui_config.shifter_vid != 0 ? &ui_shifter : &ui_wheel;
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
    int source;

    ui_current_mode = mode;
    ui_target = target;
    ui_mode_started = GetTickCount();
    for (source = 0; source < FHW_SOURCE_COUNT; source++)
        ui_baseline[source] = ui_source_input((fhw_source)source)->state;
    ui_baseline_shifter = ui_shifter_source()->state;
    if (mode == UI_MODE_AXIS)
        _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_AXIS_PROMPT_FORMAT), ui_axis_label(target), ui_axis_hint(target));
    else if (mode == UI_MODE_SHIFTER)
        _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_SHIFTER_PROMPT_FORMAT),
                   ui_shifter_label((fhw_shifter_control)target));
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
    fhw_source source = FHW_SOURCE_WHEEL;
    int axis = FHW_AXIS_NONE;
    long delta = 0;
    wchar_t text[UI_TEXT_CAPACITY];
    int candidate;

    for (candidate = 0; candidate < FHW_SOURCE_COUNT; candidate++) {
        ui_input *input = ui_source_input((fhw_source)candidate);
        long candidate_delta = 0;
        int candidate_axis;

        if (!input->valid)
            continue;
        candidate_axis = ui_strongest_axis(&input->state, &ui_baseline[candidate], &candidate_delta);
        if (candidate_axis != FHW_AXIS_NONE && (axis == FHW_AXIS_NONE || labs(candidate_delta) > labs(delta))) {
            source = (fhw_source)candidate;
            axis = candidate_axis;
            delta = candidate_delta;
        }
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

static int ui_new_binding(const fhw_raw_state *now, const fhw_raw_state *base)
{
    int binding = FHW_BINDING_NONE;
    int number;

    for (number = 1; number <= FHW_RAW_BUTTON_COUNT && binding == FHW_BINDING_NONE; number++) {
        if (fhw_binding_pressed(now, number) && !fhw_binding_pressed(base, number))
            binding = number;
    }
    if (binding == FHW_BINDING_NONE && ui_hat_binding(base->hat) == FHW_BINDING_NONE)
        binding = ui_hat_binding(now->hat);
    return binding;
}

static void ui_detect_button(void)
{
    wchar_t text[UI_TEXT_CAPACITY];
    wchar_t binding_text[UI_TEXT_CAPACITY];
    int shifter = ui_current_mode == UI_MODE_SHIFTER;
    int binding = shifter ? ui_new_binding(&ui_shifter_source()->state, &ui_baseline_shifter)
                          : ui_new_binding(&ui_wheel.state, &ui_baseline[FHW_SOURCE_WHEEL]);

    if (binding == FHW_BINDING_NONE)
        return;
    if (shifter)
        ui_config.shifter[ui_target] = binding;
    else
        ui_config.buttons[ui_target] = binding;
    ui_current_mode = UI_MODE_IDLE;
    ui_binding_label(binding, 0, binding_text);
    _snwprintf(text, UI_TEXT_CAPACITY, ui_text(T_BUTTON_DONE_FORMAT),
               shifter ? ui_shifter_label((fhw_shifter_control)ui_target) : ui_button_label((fhw_button)ui_target),
               binding_text);
    text[UI_TEXT_CAPACITY - 1] = L'\0';
    ui_set_status(text);
}

static void ui_show_preview(const fhw_wheel_input *mapped, int connected)
{
    ui_preview_state state;
    int wheel = ui_selected[UI_ROLE_WHEEL];

    ZeroMemory(&state, sizeof(state));
    state.input = *mapped;
    state.rotation_degrees = (unsigned int)ui_read_number(ID_ROTATION, (int)ui_config.rotation_degrees);
    state.has_pattern_shifter = fhw_has_pattern_shifter(&ui_config);
    state.connected = connected && wheel != UI_NO_DEVICE;
    if (state.connected)
        lstrcpynW(state.device_name, ui_devices[wheel].name, UI_PREVIEW_NAME_CAPACITY);
    ui_preview_update(ui_preview, &state);
}

static void ui_tick(void)
{
    fhw_wheel_input mapped;
    fhw_raw_sources sources;
    int row;

    int wheel_ok = ui_poll(&ui_wheel);

    ui_poll(&ui_pedals);
    ui_poll(&ui_handbrake);
    if (!ui_poll(&ui_shifter) && !wheel_ok)
        return;
    if (ui_current_mode != UI_MODE_IDLE && GetTickCount() - ui_mode_started > UI_DETECT_TIMEOUT_MS) {
        ui_current_mode = UI_MODE_IDLE;
        ui_set_status(ui_text(T_TIMEOUT));
    }
    if (ui_current_mode == UI_MODE_AXIS)
        ui_detect_axis();
    else if (ui_current_mode == UI_MODE_BUTTON || ui_current_mode == UI_MODE_SHIFTER)
        ui_detect_button();

    ui_config.dpad_from_hat = SendMessageW(GetDlgItem(ui_window, ID_DPAD_HAT), BM_GETCHECK, 0, 0) == BST_CHECKED;
    sources.wheel = &ui_wheel.state;
    sources.pedals = ui_pedals.valid ? &ui_pedals.state : NULL;
    sources.shifter = ui_shifter.valid ? &ui_shifter.state : NULL;
    sources.handbrake = ui_handbrake.valid ? &ui_handbrake.state : NULL;
    fhw_map_input(&ui_config, &sources, &mapped);
    fhw_map_handbrake(&ui_config, &mapped);
    SendMessageW(ui_axis_bar[0], PBM_SETPOS, (WPARAM)((mapped.steering + 1.0f) * UI_PROGRESS_MAX / 2.0f), 0);
    for (row = 1; row < UI_AXIS_ROWS; row++)
        SendMessageW(ui_axis_bar[row], PBM_SETPOS, (WPARAM)(mapped.pedals[row - 1] * UI_PROGRESS_MAX), 0);
    ui_refresh_button_texts(mapped.buttons);
    ui_refresh_shifter_texts(mapped.shifter);
    ui_show_preview(&mapped, wheel_ok);
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
    ui_device_combo[UI_ROLE_WHEEL] =
        ui_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 75, 12, 300, 300, ID_WHEEL);
    ui_label(ui_text(T_PEDALS), 380, 12, 75);
    ui_device_combo[UI_ROLE_PEDALS] =
        ui_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 458, 12, 280, 300, ID_PEDALS);
    ui_control(L"BUTTON", ui_text(T_REFRESH), BS_PUSHBUTTON, 12, 44, 140, 26, ID_REFRESH);
    ui_label(ui_text(T_SHIFTER), 380, 44, 75);
    ui_device_combo[UI_ROLE_SHIFTER] =
        ui_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 458, 44, 280, 300, ID_SHIFTER);
    ui_label(ui_text(T_HANDBRAKE), 380, 76, 75);
    ui_device_combo[UI_ROLE_HANDBRAKE] =
        ui_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 458, 76, 280, 300, ID_HANDBRAKE);

    ui_header(ui_text(T_AXES_HEADER), 12, UI_CONTENT_TOP, 720);
    for (row = 0; row < UI_AXIS_ROWS; row++) {
        y = UI_CONTENT_TOP + 26 + row * 32;
        ui_label(ui_axis_label(row), 12, y, 110);
        ui_axis_mapping[row] = ui_label(L"", 125, y, 150);
        ui_axis_bar[row] = ui_control(PROGRESS_CLASSW, L"", PBS_SMOOTH, 280, y + 2, 250, 20, 0);
        SendMessageW(ui_axis_bar[row], PBM_SETRANGE32, 0, UI_PROGRESS_MAX);
        SendMessageW(ui_axis_bar[row], PBM_SETBARCOLOR, 0, (LPARAM)UI_COLOR_ACCENT);
        SendMessageW(ui_axis_bar[row], PBM_SETBKCOLOR, 0, (LPARAM)UI_COLOR_TRACK);
        ui_control(L"BUTTON", ui_text(T_DETECT), BS_PUSHBUTTON, 545, y, 85, 26, ID_AXIS_DETECT + row);
        ui_axis_invert[row] = ui_control(L"BUTTON", ui_text(T_INVERT), BS_AUTOCHECKBOX, 645, y + 3, 90, 20,
                                         ID_AXIS_INVERT + row);
    }

    ui_header(ui_text(T_BUTTONS_HEADER), 12, UI_CONTENT_TOP + 196, 720);
    for (row = 0; row < FHW_BUTTON_COUNT; row++) {
        int column = row / UI_BUTTON_ROWS;
        int x = 12 + column * 375;

        y = UI_CONTENT_TOP + 222 + (row % UI_BUTTON_ROWS) * 30;
        ui_label(ui_button_label((fhw_button)row), x, y, 100);
        ui_button_mapping[row] = ui_label(L"", x + 100, y, 120);
        ui_control(L"BUTTON", ui_text(T_ASSIGN), BS_PUSHBUTTON, x + 222, y, 75, 26, ID_BUTTON_ASSIGN + row);
        ui_control(L"BUTTON", ui_text(T_CLEAR), BS_PUSHBUTTON, x + 300, y, 60, 26, ID_BUTTON_CLEAR + row);
    }
    ui_control(L"BUTTON", ui_text(T_DPAD_HAT), BS_AUTOCHECKBOX, 12, UI_CONTENT_TOP + 404, 400, 20, ID_DPAD_HAT);

    ui_header(ui_text(T_SHIFTER_HEADER), 12, UI_CONTENT_TOP + 438, 730);
    for (row = 0; row < FHW_SHIFTER_COUNT; row++) {
        int column = row / UI_SHIFTER_ROWS;
        int x = 12 + column * 375;

        y = UI_CONTENT_TOP + 464 + (row % UI_SHIFTER_ROWS) * 30;
        ui_label(ui_shifter_label((fhw_shifter_control)row), x, y, 100);
        ui_shifter_mapping[row] = ui_label(L"", x + 100, y, 120);
        ui_control(L"BUTTON", ui_text(T_ASSIGN), BS_PUSHBUTTON, x + 222, y, 75, 26, ID_SHIFTER_ASSIGN + row);
        ui_control(L"BUTTON", ui_text(T_CLEAR), BS_PUSHBUTTON, x + 300, y, 60, 26, ID_SHIFTER_CLEAR + row);
    }

    y = UI_FORCE_TOP;
    ui_header(ui_text(T_FORCE_HEADER), 12, y, 720);
    ui_label(ui_text(T_GAIN), 12, y + 26, 50);
    _snwprintf(number, UI_TEXT_CAPACITY, L"%d", ui_config.force_gain_percent);
    ui_control(L"EDIT", number, WS_BORDER | ES_NUMBER, 65, y + 26, 50, 24, ID_GAIN);
    ui_label(ui_text(T_FREQUENCY), 130, y + 26, 85);
    _snwprintf(number, UI_TEXT_CAPACITY, L"%u", ui_config.force_frequency_hz);
    ui_control(L"EDIT", number, WS_BORDER | ES_NUMBER, 218, y + 26, 50, 24, ID_FREQUENCY);
    ui_label(ui_text(T_ROTATION), 283, y + 26, 65);
    _snwprintf(number, UI_TEXT_CAPACITY, L"%u", ui_config.rotation_degrees);
    ui_control(L"EDIT", number, WS_BORDER | ES_NUMBER, 350, y + 26, 50, 24, ID_ROTATION);
    ui_control(L"BUTTON", ui_text(T_FORCE_INVERT), BS_AUTOCHECKBOX, 420, y + 28, 140, 20, ID_FORCE_INVERT);
    ui_control(L"BUTTON", ui_text(T_FORCE_ENABLED), BS_AUTOCHECKBOX, 570, y + 28, 170, 20, ID_FORCE_ENABLED);
    ui_control(L"BUTTON", ui_text(T_TEST_LEFT), BS_PUSHBUTTON, 12, y + 60, 190, 28, ID_TEST_LEFT);
    ui_control(L"BUTTON", ui_text(T_TEST_RIGHT), BS_PUSHBUTTON, 210, y + 60, 190, 28, ID_TEST_RIGHT);
    ui_control(L"BUTTON", ui_text(T_TRACE), BS_AUTOCHECKBOX, 420, y + 64, 200, 20, ID_TRACE);

    ui_status = ui_label(L"", 12, y + 105, 600);
    ui_control(L"BUTTON", ui_text(T_SAVE), BS_DEFPUSHBUTTON, 630, y + 100, 110, 30, ID_SAVE);

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
    } else if (notification == CBN_SELCHANGE &&
               (id == ID_WHEEL || id == ID_PEDALS || id == ID_SHIFTER || id == ID_HANDBRAKE)) {
        ui_select_device(id);
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
    } else if (id >= ID_SHIFTER_ASSIGN && id < ID_SHIFTER_ASSIGN + FHW_SHIFTER_COUNT) {
        ui_start_mode(UI_MODE_SHIFTER, id - ID_SHIFTER_ASSIGN);
    } else if (id >= ID_SHIFTER_CLEAR && id < ID_SHIFTER_CLEAR + FHW_SHIFTER_COUNT) {
        ui_config.shifter[id - ID_SHIFTER_CLEAR] = FHW_BINDING_NONE;
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
            ui_close_force();
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        return ui_color_static((HDC)wparam, (HWND)lparam);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return ui_color_field((HDC)wparam);
    case WM_DRAWITEM:
        if (((const DRAWITEMSTRUCT *)lparam)->CtlType != ODT_BUTTON)
            break;
        ui_draw_button((const DRAWITEMSTRUCT *)lparam);
        return TRUE;
    case WM_DESTROY:
        ui_close_input(&ui_wheel);
        ui_close_input(&ui_pedals);
        ui_close_input(&ui_shifter);
        ui_close_input(&ui_handbrake);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void ui_use_dark_title_bar(HWND window)
{
    BOOL dark = TRUE;

    DwmSetWindowAttribute(window, UI_DARK_TITLE_ATTRIBUTE, &dark, sizeof(dark));
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
    metrics.lfMessageFont.lfWeight = FW_SEMIBOLD;
    lstrcpynW(metrics.lfMessageFont.lfFaceName, UI_FONT_FACE_STRONG, LF_FACESIZE);
    ui_header_font = CreateFontIndirectW(&metrics.lfMessageFont);
    ui_background_brush = CreateSolidBrush(UI_COLOR_BACKGROUND);
    ui_card_brush = CreateSolidBrush(UI_COLOR_CARD);
    fhw_config_load(&ui_config);
    fhw_raw_clear(&ui_wheel.state);
    fhw_raw_clear(&ui_pedals.state);
    fhw_raw_clear(&ui_shifter.state);
    fhw_raw_clear(&ui_handbrake.state);

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = ui_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    window_class.hbrBackground = ui_background_brush;
    window_class.lpszClassName = UI_WINDOW_CLASS;
    RegisterClassW(&window_class);
    AdjustWindowRect(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    ui_window = CreateWindowExW(0, UI_WINDOW_CLASS, ui_text(T_TITLE),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
                                NULL, NULL, instance, NULL);
    ui_use_dark_title_bar(ui_window);
    ui_build();
    ui_preview = ui_preview_create(ui_window, instance, UI_FORM_WIDTH, UI_MARGIN, UI_PREVIEW_WIDTH,
                                   UI_HEIGHT - 2 * UI_MARGIN);
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
