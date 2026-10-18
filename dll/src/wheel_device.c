#define COBJMACROS
#include "dinput_state.h"
#include "log.h"
#include "wheel_device.h"

#define FHW_FORCE_EPSILON 0.0025f
#define FHW_FORCE_ERROR_LOG_LIMIT 5
#define FHW_FORCE_STATE_LOG_LIMIT 20
#define FHW_WINDOW_SEARCH_INTERVAL_MS 1000u
#define FHW_ACQUIRE_INTERVAL_MS 250u
#define FHW_FORCE_REFRESH_MS 200u
#define FHW_WINDOW_CLASS L"fh2wheel-directinput"

typedef struct {
    unsigned int vid;
    unsigned int pid;
    GUID instance;
    int found;
} fhw_device_search;

typedef struct {
    LPDIRECTINPUTDEVICE8W device;
    fhw_raw_state state;
    int acquired;
} fhw_input_device;

typedef struct {
    HWND best;
    LONG best_area;
} fhw_window_search;

typedef struct {
    LPDIRECTINPUTDEVICE8W device;
    HWND game_window;
    int acquired;
    DWORD next_window_search;
    DWORD next_acquire;
} fhw_force_device;

static LPDIRECTINPUT8W fhw_input = NULL;
static fhw_input_device fhw_wheel = {0};
static fhw_input_device fhw_pedals = {0};
static fhw_input_device fhw_shifter = {0};
static fhw_input_device fhw_handbrake = {0};
static fhw_pedal_rest fhw_pedals_at_rest;
static fhw_force_device fhw_force_owner = {0};
static fhw_force_effect fhw_force;
static float fhw_force_level = 0.0f;
static DWORD fhw_force_refreshed = 0;
static HWND fhw_window = NULL;
static fhw_config fhw_device_config;
static volatile LONG fhw_force_errors = 0;
static volatile LONG fhw_force_state_logs = 0;

static BOOL CALLBACK fhw_enumerate_device(LPCDIDEVICEINSTANCEW instance, LPVOID context)
{
    fhw_device_search *search = (fhw_device_search *)context;
    unsigned int vid = fhw_device_vid(instance);
    unsigned int pid = fhw_device_pid(instance);

    fhw_log_format("directinput: device '%ls' %04X:%04X", instance->tszProductName, vid, pid);
    if (!search->found && vid == search->vid && pid == search->pid) {
        search->instance = instance->guidInstance;
        search->found = 1;
    }
    return DIENUM_CONTINUE;
}

static HWND fhw_create_window(void *module)
{
    WNDCLASSW window_class;

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = (HINSTANCE)module;
    window_class.lpszClassName = FHW_WINDOW_CLASS;
    RegisterClassW(&window_class);
    return CreateWindowExW(0, FHW_WINDOW_CLASS, FHW_WINDOW_CLASS, WS_POPUP, 0, 0, 1, 1, NULL, NULL,
                           (HINSTANCE)module, NULL);
}

static void fhw_force_log(const char *format, unsigned long value)
{
    if (InterlockedIncrement(&fhw_force_state_logs) <= FHW_FORCE_STATE_LOG_LIMIT)
        fhw_log_format(format, value);
}

static int fhw_find_device(unsigned int vid, unsigned int pid, GUID *instance)
{
    fhw_device_search search;

    ZeroMemory(&search, sizeof(search));
    search.vid = vid;
    search.pid = pid;
    IDirectInput8_EnumDevices(fhw_input, DI8DEVCLASS_GAMECTRL, fhw_enumerate_device, &search, DIEDFL_ATTACHEDONLY);
    if (search.found)
        *instance = search.instance;
    return search.found;
}

static int fhw_open_input(fhw_input_device *target, const char *role, unsigned int vid, unsigned int pid)
{
    GUID instance;
    HRESULT result;

    fhw_raw_clear(&target->state);
    if (!fhw_find_device(vid, pid, &instance)) {
        fhw_log_format("directinput: %s %04X:%04X -- SKIPPED -- not plugged in", role, vid, pid);
        return 0;
    }
    result = IDirectInput8_CreateDevice(fhw_input, &instance, &target->device, NULL);
    if (FAILED(result)) {
        fhw_log_format("directinput: %s -- SKIPPED -- CreateDevice %08lX", role, (unsigned long)result);
        return 0;
    }
    IDirectInputDevice8_SetDataFormat(target->device, &c_dfDIJoystick2);
    IDirectInputDevice8_SetCooperativeLevel(target->device, fhw_window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    fhw_set_full_axis_range(target->device);
    target->acquired = SUCCEEDED(IDirectInputDevice8_Acquire(target->device));
    fhw_log_format("directinput: %s %04X:%04X -- OK (shared read, acquired %d)", role, vid, pid,
                   target->acquired);
    return 1;
}

static void fhw_open_force_device(unsigned int vid, unsigned int pid)
{
    DIDEVCAPS capabilities;
    GUID instance;

    if (!fhw_find_device(vid, pid, &instance) ||
        FAILED(IDirectInput8_CreateDevice(fhw_input, &instance, &fhw_force_owner.device, NULL))) {
        fhw_log_step_skipped("force feedback", "cannot open the wheel a second time");
        return;
    }
    ZeroMemory(&capabilities, sizeof(capabilities));
    capabilities.dwSize = sizeof(capabilities);
    IDirectInputDevice8_GetCapabilities(fhw_force_owner.device, &capabilities);
    if ((capabilities.dwFlags & DIDC_FORCEFEEDBACK) == 0) {
        fhw_log_step_skipped("force feedback", "device has no DirectInput force feedback");
        IDirectInputDevice8_Release(fhw_force_owner.device);
        fhw_force_owner.device = NULL;
        return;
    }
    IDirectInputDevice8_SetDataFormat(fhw_force_owner.device, &c_dfDIJoystick);
    fhw_log_step_ok("force feedback: waiting for the game window (exclusive foreground)");
}

static BOOL CALLBACK fhw_consider_window(HWND window, LPARAM context)
{
    fhw_window_search *search = (fhw_window_search *)context;
    DWORD process = 0;
    RECT bounds;
    LONG area;

    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || window == fhw_window || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != NULL || !GetWindowRect(window, &bounds))
        return TRUE;
    area = (bounds.right - bounds.left) * (bounds.bottom - bounds.top);
    if (area > search->best_area) {
        search->best_area = area;
        search->best = window;
    }
    return TRUE;
}

static void fhw_attach_game_window(DWORD now)
{
    fhw_window_search search = {NULL, 0};
    HRESULT result;

    if (now < fhw_force_owner.next_window_search)
        return;
    fhw_force_owner.next_window_search = now + FHW_WINDOW_SEARCH_INTERVAL_MS;
    EnumWindows(fhw_consider_window, (LPARAM)&search);
    if (search.best == NULL)
        return;
    result = IDirectInputDevice8_SetCooperativeLevel(fhw_force_owner.device, search.best,
                                                     DISCL_EXCLUSIVE | DISCL_FOREGROUND);
    fhw_force_log("force feedback: game window found, exclusive foreground level %08lX",
                  (unsigned long)result);
    if (SUCCEEDED(result))
        fhw_force_owner.game_window = search.best;
}

static void fhw_acquire_force(DWORD now)
{
    HRESULT result;

    if (now < fhw_force_owner.next_acquire)
        return;
    fhw_force_owner.next_acquire = now + FHW_ACQUIRE_INTERVAL_MS;
    if (FAILED(IDirectInputDevice8_Acquire(fhw_force_owner.device)))
        return;
    fhw_force_owner.acquired = 1;
    if (fhw_force.effect == NULL) {
        result = fhw_force_effect_create(fhw_force_owner.device, &fhw_force);
        fhw_force_log(fhw_force.effect != NULL ? "force feedback: effect created, wheel taken in the foreground (%08lX)"
                                               : "force feedback: effect creation refused (%08lX)",
                      (unsigned long)result);
        return;
    }
    result = fhw_force_effect_set(&fhw_force, fhw_force_level, 1);
    if (FAILED(result)) {
        fhw_force_effect_release(&fhw_force);
        result = fhw_force_effect_create(fhw_force_owner.device, &fhw_force);
        fhw_force_log("force feedback: effect recreated after reacquire (%08lX)", (unsigned long)result);
        return;
    }
    fhw_force_log("force feedback: wheel reacquired in the foreground (%08lX)", (unsigned long)result);
}

static int fhw_game_has_focus(void)
{
    HWND foreground = GetForegroundWindow();
    DWORD process = 0;

    if (foreground == NULL)
        return 0;
    GetWindowThreadProcessId(foreground, &process);
    return process == GetCurrentProcessId();
}

static void fhw_service_force(void)
{
    DWORD now = GetTickCount();

    if (fhw_force_owner.device == NULL)
        return;
    if (fhw_force_owner.acquired) {
        if (fhw_game_has_focus())
            return;
        fhw_force_owner.acquired = 0;
        if (fhw_force.effect != NULL)
            fhw_force_effect_set(&fhw_force, 0.0f, 0);
        IDirectInputDevice8_Unacquire(fhw_force_owner.device);
        fhw_force_log("force feedback: game in the background, wheel released (%lu)", 0ul);
        return;
    }
    if (fhw_force_owner.game_window == NULL)
        fhw_attach_game_window(now);
    else if (fhw_game_has_focus())
        fhw_acquire_force(now);
}

int fhw_device_open(const fhw_config *config, void *module)
{
    HRESULT result;

    fhw_device_config = *config;
    fhw_window = fhw_create_window(module);
    result = DirectInput8Create((HINSTANCE)module, DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void **)&fhw_input,
                                NULL);
    if (FAILED(result)) {
        fhw_log_format("directinput -- SKIPPED -- DirectInput8Create %08lX", (unsigned long)result);
        return 0;
    }
    fhw_raw_clear(&fhw_pedals.state);
    fhw_raw_clear(&fhw_shifter.state);
    fhw_raw_clear(&fhw_handbrake.state);
    fhw_pedal_rest_reset(&fhw_pedals_at_rest);
    if (!fhw_open_input(&fhw_wheel, "wheel", config->wheel_vid, config->wheel_pid))
        return 0;
    if (config->pedals_vid != 0)
        fhw_open_input(&fhw_pedals, "pedals", config->pedals_vid, config->pedals_pid);
    if (config->shifter_vid != 0)
        fhw_open_input(&fhw_shifter, "shifter", config->shifter_vid, config->shifter_pid);
    if (config->handbrake_vid != 0)
        fhw_open_input(&fhw_handbrake, "handbrake", config->handbrake_vid, config->handbrake_pid);
    if (config->force_enabled)
        fhw_open_force_device(config->wheel_vid, config->wheel_pid);
    return 1;
}

static int fhw_read_state(fhw_input_device *source)
{
    DIJOYSTATE2 joystick;
    HRESULT result;

    if (source->device == NULL)
        return 0;
    IDirectInputDevice8_Poll(source->device);
    result = IDirectInputDevice8_GetDeviceState(source->device, sizeof(joystick), &joystick);
    if (result == DIERR_INPUTLOST || result == DIERR_NOTACQUIRED) {
        source->acquired = SUCCEEDED(IDirectInputDevice8_Acquire(source->device));
        result = IDirectInputDevice8_GetDeviceState(source->device, sizeof(joystick), &joystick);
    }
    if (FAILED(result))
        return 0;
    fhw_raw_from_dinput(&joystick, &source->state);
    return 1;
}

int fhw_device_poll(fhw_wheel_input *input)
{
    fhw_raw_sources sources;

    fhw_service_force();
    if (!fhw_read_state(&fhw_wheel))
        return 0;
    sources.wheel = &fhw_wheel.state;
    sources.pedals = fhw_read_state(&fhw_pedals) ? &fhw_pedals.state : NULL;
    sources.shifter = fhw_read_state(&fhw_shifter) ? &fhw_shifter.state : NULL;
    sources.handbrake = fhw_read_state(&fhw_handbrake) ? &fhw_handbrake.state : NULL;
    fhw_map_input(&fhw_device_config, &sources, input);
    fhw_pedal_rest_apply(&fhw_pedals_at_rest, input);
    fhw_map_handbrake(&fhw_device_config, input);
    return 1;
}

int fhw_device_has_force(void)
{
    return fhw_force.effect != NULL && fhw_force_owner.acquired;
}

void fhw_device_set_force(float force)
{
    float delta = force - fhw_force_level;
    DWORD now = GetTickCount();
    HRESULT result;

    if (fhw_force.effect == NULL || !fhw_force_owner.acquired)
        return;
    if (delta < 0.0f)
        delta = -delta;
    int refresh = now - fhw_force_refreshed >= FHW_FORCE_REFRESH_MS;

    if (!refresh && (delta == 0.0f || (delta < FHW_FORCE_EPSILON && force != 0.0f)))
        return;
    fhw_force_level = force;
    if (refresh)
        fhw_force_refreshed = now;
    result = fhw_force_effect_set(&fhw_force, force, refresh);
    if (FAILED(result)) {
        fhw_force_owner.acquired = 0;
        IDirectInputDevice8_Unacquire(fhw_force_owner.device);
        if (InterlockedIncrement(&fhw_force_errors) <= FHW_FORCE_ERROR_LOG_LIMIT)
            fhw_log_format("force feedback: wheel lost (game in the background?) %08lX", (unsigned long)result);
    }
}

static void fhw_release_input(fhw_input_device *source)
{
    if (source->device == NULL)
        return;
    IDirectInputDevice8_Unacquire(source->device);
    IDirectInputDevice8_Release(source->device);
    source->device = NULL;
}

void fhw_device_close(void)
{
    fhw_force_effect_release(&fhw_force);
    if (fhw_force_owner.device != NULL) {
        IDirectInputDevice8_Unacquire(fhw_force_owner.device);
        IDirectInputDevice8_Release(fhw_force_owner.device);
        fhw_force_owner.device = NULL;
    }
    fhw_release_input(&fhw_handbrake);
    fhw_release_input(&fhw_shifter);
    fhw_release_input(&fhw_pedals);
    fhw_release_input(&fhw_wheel);
    if (fhw_input != NULL) {
        IDirectInput8_Release(fhw_input);
        fhw_input = NULL;
    }
}
