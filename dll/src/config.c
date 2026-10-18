#include <windows.h>
#include <stdio.h>

#include "config.h"
#include "log.h"
#include "targets.h"

#define FHW_VALUE_CAPACITY 32
#define FHW_DEFAULT_WHEEL_VID 0x346Eu
#define FHW_DEFAULT_WHEEL_PID 0x0002u
#define FHW_DEFAULT_ROTATION 900u
#define FHW_DEFAULT_GAIN_PERCENT 100
#define FHW_DEFAULT_FREQUENCY_HZ 500u
#define FHW_HAT_DIRECTION_COUNT 4

#define FHW_SECTION_GENERAL L"General"
#define FHW_SECTION_DEVICES L"Devices"
#define FHW_SECTION_AXES L"Axes"
#define FHW_SECTION_BUTTONS L"Buttons"
#define FHW_SECTION_FORCE L"ForceFeedback"
#define FHW_SECTION_SHIFTER L"Shifter"
#define FHW_SOURCE_PEDALS_VALUE L"Pedals"
#define FHW_SOURCE_WHEEL_VALUE L"Wheel"

typedef struct {
    const wchar_t *section;
    const wchar_t *key;
} fhw_setting;

static const fhw_setting fhw_enabled_setting = {FHW_SECTION_GENERAL, L"Enabled"};
static const fhw_setting fhw_rotation_setting = {FHW_SECTION_GENERAL, L"RotationDegrees"};
static const fhw_setting fhw_trace_setting = {FHW_SECTION_GENERAL, L"Trace"};
static const fhw_setting fhw_reported_id_setting = {FHW_SECTION_GENERAL, L"ReportedId"};
static const fhw_setting fhw_wheel_vid_setting = {FHW_SECTION_DEVICES, L"WheelVid"};
static const fhw_setting fhw_wheel_pid_setting = {FHW_SECTION_DEVICES, L"WheelPid"};
static const fhw_setting fhw_pedals_vid_setting = {FHW_SECTION_DEVICES, L"PedalsVid"};
static const fhw_setting fhw_pedals_pid_setting = {FHW_SECTION_DEVICES, L"PedalsPid"};
static const fhw_setting fhw_shifter_vid_setting = {FHW_SECTION_DEVICES, L"ShifterVid"};
static const fhw_setting fhw_shifter_pid_setting = {FHW_SECTION_DEVICES, L"ShifterPid"};
static const fhw_setting fhw_steering_setting = {FHW_SECTION_AXES, L"Steering"};
static const fhw_setting fhw_steering_inverted_setting = {FHW_SECTION_AXES, L"SteeringInverted"};
static const fhw_setting fhw_pedal_settings[FHW_PEDAL_COUNT] = {
    {FHW_SECTION_AXES, L"Throttle"}, {FHW_SECTION_AXES, L"Brake"}, {FHW_SECTION_AXES, L"Clutch"},
    {FHW_SECTION_AXES, L"Handbrake"}};
static const fhw_setting fhw_pedal_source_settings[FHW_PEDAL_COUNT] = {
    {FHW_SECTION_AXES, L"ThrottleSource"}, {FHW_SECTION_AXES, L"BrakeSource"}, {FHW_SECTION_AXES, L"ClutchSource"},
    {FHW_SECTION_AXES, L"HandbrakeSource"}};
static const fhw_setting fhw_pedal_inverted_settings[FHW_PEDAL_COUNT] = {
    {FHW_SECTION_AXES, L"ThrottleInverted"}, {FHW_SECTION_AXES, L"BrakeInverted"},
    {FHW_SECTION_AXES, L"ClutchInverted"}, {FHW_SECTION_AXES, L"HandbrakeInverted"}};
static const fhw_setting fhw_button_settings[FHW_BUTTON_COUNT] = {
    {FHW_SECTION_BUTTONS, L"A"},          {FHW_SECTION_BUTTONS, L"B"},          {FHW_SECTION_BUTTONS, L"X"},
    {FHW_SECTION_BUTTONS, L"Y"},          {FHW_SECTION_BUTTONS, L"View"},       {FHW_SECTION_BUTTONS, L"Menu"},
    {FHW_SECTION_BUTTONS, L"DpadUp"},     {FHW_SECTION_BUTTONS, L"DpadDown"},   {FHW_SECTION_BUTTONS, L"DpadLeft"},
    {FHW_SECTION_BUTTONS, L"DpadRight"},  {FHW_SECTION_BUTTONS, L"LeftPaddle"}, {FHW_SECTION_BUTTONS, L"RightPaddle"}};
static const fhw_setting fhw_dpad_hat_setting = {FHW_SECTION_BUTTONS, L"DpadFromHat"};
static const fhw_setting fhw_shifter_settings[FHW_SHIFTER_COUNT] = {
    {FHW_SECTION_SHIFTER, L"Gear1"},   {FHW_SECTION_SHIFTER, L"Gear2"},   {FHW_SECTION_SHIFTER, L"Gear3"},
    {FHW_SECTION_SHIFTER, L"Gear4"},   {FHW_SECTION_SHIFTER, L"Gear5"},   {FHW_SECTION_SHIFTER, L"Gear6"},
    {FHW_SECTION_SHIFTER, L"Gear7"},   {FHW_SECTION_SHIFTER, L"Reverse"}, {FHW_SECTION_SHIFTER, L"ShiftUp"},
    {FHW_SECTION_SHIFTER, L"ShiftDown"}};
static const fhw_setting fhw_force_enabled_setting = {FHW_SECTION_FORCE, L"Enabled"};
static const fhw_setting fhw_gain_setting = {FHW_SECTION_FORCE, L"GainPercent"};
static const fhw_setting fhw_force_inverted_setting = {FHW_SECTION_FORCE, L"Inverted"};
static const fhw_setting fhw_frequency_setting = {FHW_SECTION_FORCE, L"RateHz"};

static const wchar_t *const fhw_axis_values[FHW_AXIS_COUNT] = {L"None", L"X",  L"Y",       L"Z",      L"RX",
                                                               L"RY",   L"RZ", L"Slider0", L"Slider1"};
static const wchar_t *const fhw_hat_values[FHW_HAT_DIRECTION_COUNT] = {L"HatUp", L"HatDown", L"HatLeft",
                                                                       L"HatRight"};
static const char *const fhw_axis_names[FHW_AXIS_COUNT] = {"None", "X", "Y", "Z", "RX", "RY", "RZ", "Slider0",
                                                           "Slider1"};
static const char *const fhw_button_names[FHW_BUTTON_COUNT] = {
    "A", "B", "X", "Y", "View", "Menu", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "LeftPaddle", "RightPaddle"};
static const char *const fhw_pedal_names[FHW_PEDAL_COUNT] = {"Throttle", "Brake", "Clutch", "Handbrake"};
static const int fhw_default_buttons[FHW_BUTTON_COUNT] = {1, 2, 3, 4, 24, 37, 6, 5, 8, 7, 14, 13};
static const fhw_axis fhw_default_pedal_axes[FHW_PEDAL_COUNT] = {FHW_AXIS_Z, FHW_AXIS_RZ, FHW_AXIS_SLIDER0,
                                                                FHW_AXIS_NONE};

const char *fhw_axis_name(fhw_axis axis)
{
    return (unsigned int)axis < FHW_AXIS_COUNT ? fhw_axis_names[axis] : "?";
}

const char *fhw_button_name(fhw_button button)
{
    return (unsigned int)button < FHW_BUTTON_COUNT ? fhw_button_names[button] : "?";
}

const char *fhw_pedal_name(fhw_pedal pedal)
{
    return (unsigned int)pedal < FHW_PEDAL_COUNT ? fhw_pedal_names[pedal] : "?";
}

void fhw_binding_text(int binding, wchar_t *out, size_t capacity)
{
    if (binding > 0)
        _snwprintf(out, capacity, L"%d", binding);
    else if (binding < 0 && -binding <= FHW_HAT_DIRECTION_COUNT)
        _snwprintf(out, capacity, L"%ls", fhw_hat_values[-binding - 1]);
    else
        _snwprintf(out, capacity, L"0");
    out[capacity - 1] = L'\0';
}

void fhw_config_defaults(fhw_config *config)
{
    int index;

    ZeroMemory(config, sizeof(*config));
    config->enabled = 1;
    config->wheel_vid = FHW_DEFAULT_WHEEL_VID;
    config->wheel_pid = FHW_DEFAULT_WHEEL_PID;
    config->steering_axis = FHW_AXIS_X;
    for (index = 0; index < FHW_PEDAL_COUNT; index++) {
        config->pedals[index].axis = fhw_default_pedal_axes[index];
        config->pedals[index].source = FHW_SOURCE_WHEEL;
    }
    for (index = 0; index < FHW_BUTTON_COUNT; index++)
        config->buttons[index] = fhw_default_buttons[index];
    config->dpad_from_hat = 1;
    config->rotation_degrees = FHW_DEFAULT_ROTATION;
    config->reported_device_id = FHW_DEFAULT_DEVICE_ID;
    config->force_enabled = 1;
    config->force_gain_percent = FHW_DEFAULT_GAIN_PERCENT;
    config->force_frequency_hz = FHW_DEFAULT_FREQUENCY_HZ;
}

static int fhw_clamp(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

static int fhw_read_text(const wchar_t *path, const fhw_setting *setting, wchar_t *value)
{
    return GetPrivateProfileStringW(setting->section, setting->key, L"", value, FHW_VALUE_CAPACITY, path) > 0;
}

static unsigned int fhw_read_number(const wchar_t *path, const fhw_setting *setting, unsigned int fallback)
{
    wchar_t value[FHW_VALUE_CAPACITY];
    wchar_t *end = NULL;
    unsigned long parsed;

    if (!fhw_read_text(path, setting, value))
        return fallback;
    parsed = wcstoul(value, &end, 0);
    return end == value ? fallback : (unsigned int)parsed;
}

static fhw_axis fhw_read_axis(const wchar_t *path, const fhw_setting *setting, fhw_axis fallback)
{
    wchar_t value[FHW_VALUE_CAPACITY];
    int index;

    if (!fhw_read_text(path, setting, value))
        return fallback;
    for (index = 0; index < FHW_AXIS_COUNT; index++) {
        if (lstrcmpiW(value, fhw_axis_values[index]) == 0)
            return (fhw_axis)index;
    }
    fhw_log_format("configuration: unknown axis '%ls' for %ls, using %s", value, setting->key,
                   fhw_axis_name(fallback));
    return fallback;
}

static int fhw_read_binding(const wchar_t *path, const fhw_setting *setting, int fallback)
{
    wchar_t value[FHW_VALUE_CAPACITY];
    wchar_t *end = NULL;
    long parsed;
    int index;

    if (!fhw_read_text(path, setting, value))
        return fallback;
    for (index = 0; index < FHW_HAT_DIRECTION_COUNT; index++) {
        if (lstrcmpiW(value, fhw_hat_values[index]) == 0)
            return -(index + 1);
    }
    parsed = wcstol(value, &end, 10);
    if (end == value || parsed < 0 || parsed > FHW_MAX_BUTTON_NUMBER)
        return fallback;
    return (int)parsed;
}

static int fhw_write_text(const wchar_t *path, const fhw_setting *setting, const wchar_t *value)
{
    return WritePrivateProfileStringW(setting->section, setting->key, value, path) != 0;
}

static int fhw_write_number(const wchar_t *path, const fhw_setting *setting, unsigned int value, int hexadecimal)
{
    wchar_t text[FHW_VALUE_CAPACITY];

    _snwprintf(text, FHW_VALUE_CAPACITY, hexadecimal ? L"0x%04X" : L"%u", value);
    text[FHW_VALUE_CAPACITY - 1] = L'\0';
    return fhw_write_text(path, setting, text);
}

int fhw_config_save(const fhw_config *config)
{
    wchar_t path[MAX_PATH];
    wchar_t text[FHW_VALUE_CAPACITY];
    int ok = 1;
    int index;

    if (!fhw_host_path(FHW_CONFIG_FILE, path, MAX_PATH))
        return 0;
    ok &= fhw_write_number(path, &fhw_enabled_setting, (unsigned int)config->enabled, 0);
    ok &= fhw_write_number(path, &fhw_rotation_setting, config->rotation_degrees, 0);
    ok &= fhw_write_number(path, &fhw_trace_setting, (unsigned int)config->trace, 0);
    ok &= fhw_write_number(path, &fhw_wheel_vid_setting, config->wheel_vid, 1);
    ok &= fhw_write_number(path, &fhw_wheel_pid_setting, config->wheel_pid, 1);
    ok &= fhw_write_number(path, &fhw_pedals_vid_setting, config->pedals_vid, 1);
    ok &= fhw_write_number(path, &fhw_pedals_pid_setting, config->pedals_pid, 1);
    ok &= fhw_write_number(path, &fhw_shifter_vid_setting, config->shifter_vid, 1);
    ok &= fhw_write_number(path, &fhw_shifter_pid_setting, config->shifter_pid, 1);
    ok &= fhw_write_text(path, &fhw_steering_setting, fhw_axis_values[config->steering_axis]);
    ok &= fhw_write_number(path, &fhw_steering_inverted_setting, (unsigned int)config->steering_inverted, 0);
    for (index = 0; index < FHW_PEDAL_COUNT; index++) {
        const fhw_pedal_mapping *mapping = &config->pedals[index];

        ok &= fhw_write_text(path, &fhw_pedal_settings[index], fhw_axis_values[mapping->axis]);
        ok &= fhw_write_text(path, &fhw_pedal_source_settings[index],
                             mapping->source == FHW_SOURCE_PEDALS ? FHW_SOURCE_PEDALS_VALUE : FHW_SOURCE_WHEEL_VALUE);
        ok &= fhw_write_number(path, &fhw_pedal_inverted_settings[index], (unsigned int)mapping->inverted, 0);
    }
    for (index = 0; index < FHW_BUTTON_COUNT; index++) {
        fhw_binding_text(config->buttons[index], text, FHW_VALUE_CAPACITY);
        ok &= fhw_write_text(path, &fhw_button_settings[index], text);
    }
    ok &= fhw_write_number(path, &fhw_dpad_hat_setting, (unsigned int)config->dpad_from_hat, 0);
    for (index = 0; index < FHW_SHIFTER_COUNT; index++) {
        fhw_binding_text(config->shifter[index], text, FHW_VALUE_CAPACITY);
        ok &= fhw_write_text(path, &fhw_shifter_settings[index], text);
    }
    ok &= fhw_write_number(path, &fhw_force_enabled_setting, (unsigned int)config->force_enabled, 0);
    ok &= fhw_write_number(path, &fhw_gain_setting, (unsigned int)config->force_gain_percent, 0);
    ok &= fhw_write_number(path, &fhw_force_inverted_setting, (unsigned int)config->force_inverted, 0);
    ok &= fhw_write_number(path, &fhw_frequency_setting, config->force_frequency_hz, 0);
    return ok;
}

void fhw_config_load(fhw_config *config)
{
    wchar_t path[MAX_PATH];
    wchar_t value[FHW_VALUE_CAPACITY];
    int index;

    fhw_config_defaults(config);
    if (!fhw_host_path(FHW_CONFIG_FILE, path, MAX_PATH)) {
        fhw_log_step_skipped("configuration", "file path not found, using defaults");
        return;
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        fhw_config_save(config);
        fhw_log_format("configuration: %ls created with default values", FHW_CONFIG_FILE);
    }

    config->enabled = fhw_read_number(path, &fhw_enabled_setting, 1) != 0;
    config->rotation_degrees =
        (unsigned int)fhw_clamp((int)fhw_read_number(path, &fhw_rotation_setting, FHW_DEFAULT_ROTATION),
                                FHW_MIN_REPORTED_ROTATION, FHW_MAX_REPORTED_ROTATION);
    config->trace = fhw_read_number(path, &fhw_trace_setting, 0) != 0;
    config->reported_device_id = fhw_read_number(path, &fhw_reported_id_setting, FHW_DEFAULT_DEVICE_ID);

    config->wheel_vid = fhw_read_number(path, &fhw_wheel_vid_setting, FHW_DEFAULT_WHEEL_VID);
    config->wheel_pid = fhw_read_number(path, &fhw_wheel_pid_setting, FHW_DEFAULT_WHEEL_PID);
    config->pedals_vid = fhw_read_number(path, &fhw_pedals_vid_setting, 0);
    config->pedals_pid = fhw_read_number(path, &fhw_pedals_pid_setting, 0);
    config->shifter_vid = fhw_read_number(path, &fhw_shifter_vid_setting, 0);
    config->shifter_pid = fhw_read_number(path, &fhw_shifter_pid_setting, 0);

    config->steering_axis = fhw_read_axis(path, &fhw_steering_setting, FHW_AXIS_X);
    config->steering_inverted = fhw_read_number(path, &fhw_steering_inverted_setting, 0) != 0;
    for (index = 0; index < FHW_PEDAL_COUNT; index++) {
        fhw_pedal_mapping *mapping = &config->pedals[index];

        mapping->axis = fhw_read_axis(path, &fhw_pedal_settings[index], fhw_default_pedal_axes[index]);
        mapping->source = fhw_read_text(path, &fhw_pedal_source_settings[index], value) &&
                                  lstrcmpiW(value, FHW_SOURCE_PEDALS_VALUE) == 0
                              ? FHW_SOURCE_PEDALS
                              : FHW_SOURCE_WHEEL;
        mapping->inverted = fhw_read_number(path, &fhw_pedal_inverted_settings[index], 0) != 0;
    }

    for (index = 0; index < FHW_BUTTON_COUNT; index++)
        config->buttons[index] = fhw_read_binding(path, &fhw_button_settings[index], fhw_default_buttons[index]);
    config->dpad_from_hat = fhw_read_number(path, &fhw_dpad_hat_setting, 1) != 0;
    for (index = 0; index < FHW_SHIFTER_COUNT; index++)
        config->shifter[index] = fhw_read_binding(path, &fhw_shifter_settings[index], FHW_BINDING_NONE);

    config->force_enabled = fhw_read_number(path, &fhw_force_enabled_setting, 1) != 0;
    config->force_gain_percent =
        fhw_clamp((int)fhw_read_number(path, &fhw_gain_setting, FHW_DEFAULT_GAIN_PERCENT), FHW_MIN_GAIN_PERCENT,
                  FHW_MAX_GAIN_PERCENT);
    config->force_inverted = fhw_read_number(path, &fhw_force_inverted_setting, 0) != 0;
    config->force_frequency_hz =
        (unsigned int)fhw_clamp((int)fhw_read_number(path, &fhw_frequency_setting, FHW_DEFAULT_FREQUENCY_HZ),
                                (int)FHW_MIN_FREQUENCY_HZ, (int)FHW_MAX_FREQUENCY_HZ);

    fhw_log_format("configuration -- OK (enabled %d, wheel %04X:%04X, pedals %04X:%04X, shifter %04X:%04X, "
                   "rotation %u, steering %s%s, force %d gain %d%% inverted %d %u Hz, trace %d)",
                   config->enabled, config->wheel_vid, config->wheel_pid, config->pedals_vid, config->pedals_pid,
                   config->shifter_vid, config->shifter_pid, config->rotation_degrees, fhw_axis_name(config->steering_axis),
                   config->steering_inverted ? " inverted" : "", config->force_enabled, config->force_gain_percent,
                   config->force_inverted, config->force_frequency_hz, config->trace);
}
