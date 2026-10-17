#define DIRECTINPUT_VERSION 0x0800
#define COBJMACROS
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_AXIS_RANGE_MAX 65535
#define PROBE_DEFAULT_SECONDS 20
#define PROBE_POLL_MS 20
#define PROBE_AXIS_REPORT_STEP 2000
#define PROBE_BUTTON_PRESSED 0x80
#define PROBE_AXIS_COUNT 8
#define PROBE_MILLISECONDS 1000

typedef struct {
    unsigned int vid;
    unsigned int pid;
    GUID instance;
    int found;
} probe_search;

static const char *const probe_axis_names[PROBE_AXIS_COUNT] = {"X", "Y", "Z", "RX", "RY", "RZ", "Slider0",
                                                               "Slider1"};

static BOOL CALLBACK probe_list(LPCDIDEVICEINSTANCEW instance, LPVOID context)
{
    LPDIRECTINPUT8W input = (LPDIRECTINPUT8W)context;
    LPDIRECTINPUTDEVICE8W device = NULL;
    DIDEVCAPS caps;

    ZeroMemory(&caps, sizeof(caps));
    caps.dwSize = sizeof(caps);
    if (SUCCEEDED(IDirectInput8_CreateDevice(input, &instance->guidInstance, &device, NULL))) {
        IDirectInputDevice8_GetCapabilities(device, &caps);
        IDirectInputDevice8_Release(device);
    }
    printf("%04X:%04X  type %02lX  axes %lu  buttons %lu  hats %lu  force feedback %s  '%ls'\n",
           (unsigned)LOWORD(instance->guidProduct.Data1), (unsigned)HIWORD(instance->guidProduct.Data1),
           (unsigned long)(instance->dwDevType & 0xFF), (unsigned long)caps.dwAxes, (unsigned long)caps.dwButtons,
           (unsigned long)caps.dwPOVs, (caps.dwFlags & DIDC_FORCEFEEDBACK) ? "yes" : "no", instance->tszProductName);
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK probe_find(LPCDIDEVICEINSTANCEW instance, LPVOID context)
{
    probe_search *search = (probe_search *)context;

    if (LOWORD(instance->guidProduct.Data1) == search->vid && HIWORD(instance->guidProduct.Data1) == search->pid) {
        search->instance = instance->guidInstance;
        search->found = 1;
        return DIENUM_STOP;
    }
    return DIENUM_CONTINUE;
}

static void probe_axes(const DIJOYSTATE2 *state, LONG *axes)
{
    axes[0] = state->lX;
    axes[1] = state->lY;
    axes[2] = state->lZ;
    axes[3] = state->lRx;
    axes[4] = state->lRy;
    axes[5] = state->lRz;
    axes[6] = state->rglSlider[0];
    axes[7] = state->rglSlider[1];
}

static int probe_watch(LPDIRECTINPUT8W input, unsigned int vid, unsigned int pid, int seconds)
{
    probe_search search;
    LPDIRECTINPUTDEVICE8W device = NULL;
    DIPROPRANGE range;
    DIJOYSTATE2 state;
    DIJOYSTATE2 previous;
    LONG axes[PROBE_AXIS_COUNT];
    LONG reported[PROBE_AXIS_COUNT];
    DWORD end;
    int index;

    ZeroMemory(&search, sizeof(search));
    search.vid = vid;
    search.pid = pid;
    IDirectInput8_EnumDevices(input, DI8DEVCLASS_GAMECTRL, probe_find, &search, DIEDFL_ATTACHEDONLY);
    if (!search.found) {
        printf("device %04X:%04X not found\n", vid, pid);
        return 1;
    }
    if (FAILED(IDirectInput8_CreateDevice(input, &search.instance, &device, NULL)))
        return 1;
    IDirectInputDevice8_SetDataFormat(device, &c_dfDIJoystick2);
    IDirectInputDevice8_SetCooperativeLevel(device, GetConsoleWindow(), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    ZeroMemory(&range, sizeof(range));
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = DIPH_DEVICE;
    range.lMax = PROBE_AXIS_RANGE_MAX;
    IDirectInputDevice8_SetProperty(device, DIPROP_RANGE, &range.diph);
    IDirectInputDevice8_Acquire(device);

    ZeroMemory(&previous, sizeof(previous));
    IDirectInputDevice8_Poll(device);
    IDirectInputDevice8_GetDeviceState(device, sizeof(previous), &previous);
    probe_axes(&previous, reported);
    printf("initial state:");
    for (index = 0; index < PROBE_AXIS_COUNT; index++)
        printf(" %s=%ld", probe_axis_names[index], reported[index]);
    printf("  hat=%ld\n", (long)previous.rgdwPOV[0]);
    printf("for %d s: turn the wheel, press the pedals and every button\n", seconds);
    fflush(stdout);

    end = GetTickCount() + (DWORD)seconds * PROBE_MILLISECONDS;
    while ((LONG)(end - GetTickCount()) > 0) {
        IDirectInputDevice8_Poll(device);
        if (FAILED(IDirectInputDevice8_GetDeviceState(device, sizeof(state), &state))) {
            IDirectInputDevice8_Acquire(device);
            Sleep(PROBE_POLL_MS);
            continue;
        }
        for (index = 0; index < (int)sizeof(state.rgbButtons); index++) {
            int now = (state.rgbButtons[index] & PROBE_BUTTON_PRESSED) != 0;
            int before = (previous.rgbButtons[index] & PROBE_BUTTON_PRESSED) != 0;

            if (now != before)
                printf("button %d %s\n", index + 1, now ? "pressed" : "released");
        }
        if (state.rgdwPOV[0] != previous.rgdwPOV[0])
            printf("hat %ld\n", (long)state.rgdwPOV[0]);
        probe_axes(&state, axes);
        for (index = 0; index < PROBE_AXIS_COUNT; index++) {
            if (labs(axes[index] - reported[index]) >= PROBE_AXIS_REPORT_STEP) {
                printf("axis %s = %ld\n", probe_axis_names[index], axes[index]);
                reported[index] = axes[index];
            }
        }
        fflush(stdout);
        previous = state;
        Sleep(PROBE_POLL_MS);
    }
    IDirectInputDevice8_Unacquire(device);
    IDirectInputDevice8_Release(device);
    return 0;
}

typedef struct {
    const char *label;
    const GUID *type;
    DWORD flags;
    LONG direction;
    LONG magnitude;
} probe_force_variant;

static void probe_print_force_state(LPDIRECTINPUTDEVICE8W device, const char *moment)
{
    DWORD state = 0;
    HRESULT result = IDirectInputDevice8_GetForceFeedbackState(device, &state);

    printf("force feedback state (%s): %08lX, result %08lX%s%s%s%s%s\n", moment, (unsigned long)state,
           (unsigned long)result, (state & DIGFFS_ACTUATORSON) ? " actuators-on" : "",
           (state & DIGFFS_ACTUATORSOFF) ? " ACTUATORS-OFF" : "", (state & DIGFFS_POWERON) ? " powered" : "",
           (state & DIGFFS_POWEROFF) ? " NOT-POWERED" : "", (state & DIGFFS_PAUSED) ? " PAUSED" : "");
}

static void probe_run_variant(LPDIRECTINPUTDEVICE8W device, const probe_force_variant *variant, int index)
{
    DWORD axes[1] = {DIJOFS_X};
    LONG direction[1];
    DICONSTANTFORCE constant;
    DICONDITION condition;
    DIPERIODIC periodic;
    DIEFFECT effect;
    LPDIRECTINPUTEFFECT handle = NULL;
    HRESULT created;
    HRESULT started;

    direction[0] = variant->direction;
    ZeroMemory(&effect, sizeof(effect));
    effect.dwSize = sizeof(effect);
    effect.dwFlags = variant->flags | DIEFF_OBJECTOFFSETS;
    effect.dwDuration = 1500000;
    effect.dwGain = DI_FFNOMINALMAX;
    effect.dwTriggerButton = DIEB_NOTRIGGER;
    effect.cAxes = 1;
    effect.rgdwAxes = axes;
    effect.rglDirection = direction;
    if (IsEqualGUID(variant->type, &GUID_ConstantForce)) {
        constant.lMagnitude = variant->magnitude;
        effect.cbTypeSpecificParams = sizeof(constant);
        effect.lpvTypeSpecificParams = &constant;
    } else if (IsEqualGUID(variant->type, &GUID_Spring)) {
        ZeroMemory(&condition, sizeof(condition));
        condition.lPositiveCoefficient = variant->magnitude;
        condition.lNegativeCoefficient = variant->magnitude;
        condition.dwPositiveSaturation = DI_FFNOMINALMAX;
        condition.dwNegativeSaturation = DI_FFNOMINALMAX;
        effect.cbTypeSpecificParams = sizeof(condition);
        effect.lpvTypeSpecificParams = &condition;
    } else {
        ZeroMemory(&periodic, sizeof(periodic));
        periodic.dwMagnitude = (DWORD)variant->magnitude;
        periodic.dwPeriod = 100000;
        effect.cbTypeSpecificParams = sizeof(periodic);
        effect.lpvTypeSpecificParams = &periodic;
    }
    created = IDirectInputDevice8_CreateEffect(device, variant->type, &effect, &handle, NULL);
    started = handle != NULL ? IDirectInputEffect_Start(handle, 1, 0) : E_FAIL;
    printf("test %d: %-45s create %08lX start %08lX\n", index, variant->label, (unsigned long)created,
           (unsigned long)started);
    fflush(stdout);
    Sleep(1500);
    if (handle != NULL) {
        IDirectInputEffect_Stop(handle);
        IDirectInputEffect_Release(handle);
    }
    Sleep(1500);
}

#define PROBE_ACTUATOR_CAPACITY 8

typedef struct {
    DWORD offsets[PROBE_ACTUATOR_CAPACITY];
    DWORD types[PROBE_ACTUATOR_CAPACITY];
    int count;
} probe_actuators;

static BOOL CALLBACK probe_object(LPCDIDEVICEOBJECTINSTANCEW object, LPVOID context)
{
    probe_actuators *actuators = (probe_actuators *)context;
    int actuator = (object->dwFlags & DIDOI_FFACTUATOR) != 0;

    printf("  axis '%ls': offset %lu, type %08lX%s\n", object->tszName, (unsigned long)object->dwOfs,
           (unsigned long)object->dwType, actuator ? "  <-- MOTOR (actuator)" : "");
    if (actuator && actuators->count < PROBE_ACTUATOR_CAPACITY) {
        actuators->offsets[actuators->count] = object->dwOfs;
        actuators->types[actuators->count] = object->dwType;
        actuators->count++;
    }
    return DIENUM_CONTINUE;
}

static void probe_constant_on(LPDIRECTINPUTDEVICE8W device, DWORD axis, DWORD flags, const char *label)
{
    DWORD axes[1];
    LONG direction[1] = {1};
    DICONSTANTFORCE constant = {6000};
    DIEFFECT effect;
    LPDIRECTINPUTEFFECT handle = NULL;
    HRESULT created;
    HRESULT started;

    axes[0] = axis;
    ZeroMemory(&effect, sizeof(effect));
    effect.dwSize = sizeof(effect);
    effect.dwFlags = DIEFF_CARTESIAN | flags;
    effect.dwDuration = 1500000;
    effect.dwGain = DI_FFNOMINALMAX;
    effect.dwTriggerButton = DIEB_NOTRIGGER;
    effect.cAxes = 1;
    effect.rgdwAxes = axes;
    effect.rglDirection = direction;
    effect.cbTypeSpecificParams = sizeof(constant);
    effect.lpvTypeSpecificParams = &constant;
    created = IDirectInputDevice8_CreateEffect(device, &GUID_ConstantForce, &effect, &handle, NULL);
    started = handle != NULL ? IDirectInputEffect_Start(handle, 1, 0) : E_FAIL;
    printf("test: %-50s create %08lX start %08lX\n", label, (unsigned long)created, (unsigned long)started);
    fflush(stdout);
    Sleep(1500);
    if (handle != NULL) {
        IDirectInputEffect_Stop(handle);
        IDirectInputEffect_Release(handle);
    }
    Sleep(1500);
}

static void probe_constant_two_axes(LPDIRECTINPUTDEVICE8W device, const probe_actuators *actuators, LONG sign,
                                    const char *label)
{
    DWORD axes[2];
    LONG direction[2];
    DICONSTANTFORCE constant = {6000};
    DIEFFECT effect;
    LPDIRECTINPUTEFFECT handle = NULL;
    HRESULT created;
    HRESULT started;

    if (actuators->count < 2)
        return;
    axes[0] = actuators->types[0];
    axes[1] = actuators->types[1];
    direction[0] = sign;
    direction[1] = 0;
    ZeroMemory(&effect, sizeof(effect));
    effect.dwSize = sizeof(effect);
    effect.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTIDS;
    effect.dwDuration = 1500000;
    effect.dwGain = DI_FFNOMINALMAX;
    effect.dwTriggerButton = DIEB_NOTRIGGER;
    effect.cAxes = 2;
    effect.rgdwAxes = axes;
    effect.rglDirection = direction;
    effect.cbTypeSpecificParams = sizeof(constant);
    effect.lpvTypeSpecificParams = &constant;
    created = IDirectInputDevice8_CreateEffect(device, &GUID_ConstantForce, &effect, &handle, NULL);
    started = handle != NULL ? IDirectInputEffect_Start(handle, 1, 0) : E_FAIL;
    printf("test: %-50s create %08lX start %08lX\n", label, (unsigned long)created, (unsigned long)started);
    fflush(stdout);
    Sleep(1500);
    if (handle != NULL) {
        IDirectInputEffect_Stop(handle);
        IDirectInputEffect_Release(handle);
    }
    Sleep(1500);
}

static void probe_gain(LPDIRECTINPUTDEVICE8W device)
{
    DIPROPDWORD gain;
    HRESULT result;

    ZeroMemory(&gain, sizeof(gain));
    gain.diph.dwSize = sizeof(gain);
    gain.diph.dwHeaderSize = sizeof(gain.diph);
    gain.diph.dwHow = DIPH_DEVICE;
    result = IDirectInputDevice8_GetProperty(device, DIPROP_FFGAIN, &gain.diph);
    printf("current device gain: %lu (result %08lX)\n", (unsigned long)gain.dwData, (unsigned long)result);
    gain.dwData = DI_FFNOMINALMAX;
    result = IDirectInputDevice8_SetProperty(device, DIPROP_FFGAIN, &gain.diph);
    printf("device gain set to 10000: %08lX\n", (unsigned long)result);
}

static int probe_actuator_test(LPDIRECTINPUT8W input, unsigned int vid, unsigned int pid)
{
    probe_search search;
    probe_actuators actuators;
    LPDIRECTINPUTDEVICE8W device = NULL;
    HWND window = CreateWindowExW(0, L"STATIC", L"fh2wheel-probe", WS_POPUP, 0, 0, 1, 1, NULL, NULL,
                                  GetModuleHandleW(NULL), NULL);
    char label[96];
    int index;

    ZeroMemory(&search, sizeof(search));
    ZeroMemory(&actuators, sizeof(actuators));
    search.vid = vid;
    search.pid = pid;
    IDirectInput8_EnumDevices(input, DI8DEVCLASS_GAMECTRL, probe_find, &search, DIEDFL_ATTACHEDONLY);
    if (!search.found || FAILED(IDirectInput8_CreateDevice(input, &search.instance, &device, NULL))) {
        printf("device %04X:%04X not found\n", vid, pid);
        return 1;
    }
    IDirectInputDevice8_SetDataFormat(device, &c_dfDIJoystick2);
    printf("exclusive level: %08lX\n",
           (unsigned long)IDirectInputDevice8_SetCooperativeLevel(device, window, DISCL_EXCLUSIVE | DISCL_BACKGROUND));
    printf("device axes:\n");
    IDirectInputDevice8_EnumObjects(device, probe_object, &actuators, DIDFT_AXIS);
    printf("acquire: %08lX\n", (unsigned long)IDirectInputDevice8_Acquire(device));
    probe_gain(device);
    IDirectInputDevice8_SendForceFeedbackCommand(device, DISFFC_RESET);
    IDirectInputDevice8_SendForceFeedbackCommand(device, DISFFC_SETACTUATORSON);
    printf("%d motor axis/axes; each test lasts 1.5 s, then a 1.5 s pause\n", actuators.count);
    fflush(stdout);
    probe_constant_two_axes(device, &actuators, 1, "both motor axes, push right");
    probe_constant_two_axes(device, &actuators, -1, "both motor axes, push left");
    probe_constant_on(device, DIJOFS_X, DIEFF_OBJECTOFFSETS, "X axis with device gain set");
    for (index = 0; index < actuators.count; index++) {
        snprintf(label, sizeof(label), "motor %d by offset %lu", index + 1,
                 (unsigned long)actuators.offsets[index]);
        probe_constant_on(device, actuators.offsets[index], DIEFF_OBJECTOFFSETS, label);
        snprintf(label, sizeof(label), "motor %d by id %08lX", index + 1,
                 (unsigned long)actuators.types[index]);
        probe_constant_on(device, actuators.types[index], DIEFF_OBJECTIDS, label);
    }
    IDirectInputDevice8_Unacquire(device);
    IDirectInputDevice8_Release(device);
    return 0;
}

static void probe_pump_for(DWORD milliseconds)
{
    DWORD end = GetTickCount() + milliseconds;
    MSG message;

    while ((LONG)(end - GetTickCount()) > 0) {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

static void probe_foreground_constant(LPDIRECTINPUTDEVICE8W device, LONG sign, const char *label)
{
    DWORD axes[1] = {DIJOFS_X};
    LONG direction[1] = {0};
    DICONSTANTFORCE constant;
    DIEFFECT effect;
    LPDIRECTINPUTEFFECT handle = NULL;
    HRESULT created;
    HRESULT started;

    constant.lMagnitude = sign * 6000;
    ZeroMemory(&effect, sizeof(effect));
    effect.dwSize = sizeof(effect);
    effect.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    effect.dwDuration = INFINITE;
    effect.dwGain = DI_FFNOMINALMAX;
    effect.dwTriggerButton = DIEB_NOTRIGGER;
    effect.cAxes = 1;
    effect.rgdwAxes = axes;
    effect.rglDirection = direction;
    effect.cbTypeSpecificParams = sizeof(constant);
    effect.lpvTypeSpecificParams = &constant;
    created = IDirectInputDevice8_CreateEffect(device, &GUID_ConstantForce, &effect, &handle, NULL);
    started = handle != NULL ? IDirectInputEffect_Start(handle, 1, 0) : E_FAIL;
    printf("test: %-50s create %08lX start %08lX\n", label, (unsigned long)created, (unsigned long)started);
    fflush(stdout);
    probe_pump_for(1500);
    if (handle != NULL) {
        IDirectInputEffect_Stop(handle);
        IDirectInputEffect_Release(handle);
    }
    probe_pump_for(1500);
}

static int probe_foreground_test(LPDIRECTINPUT8W input, unsigned int vid, unsigned int pid)
{
    WNDCLASSW window_class;
    probe_search search;
    LPDIRECTINPUTDEVICE8W device = NULL;
    HWND window;
    HRESULT result = E_FAIL;
    int attempt;

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(NULL);
    window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    window_class.lpszClassName = L"fh2wheel-probe-premier-plan";
    RegisterClassW(&window_class);
    window = CreateWindowExW(WS_EX_TOPMOST, window_class.lpszClassName,
                             L"fh2wheel: force feedback test (do not click elsewhere)",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 200, 520, 160, NULL, NULL,
                             GetModuleHandleW(NULL), NULL);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    probe_pump_for(300);

    ZeroMemory(&search, sizeof(search));
    search.vid = vid;
    search.pid = pid;
    IDirectInput8_EnumDevices(input, DI8DEVCLASS_GAMECTRL, probe_find, &search, DIEDFL_ATTACHEDONLY);
    if (!search.found || FAILED(IDirectInput8_CreateDevice(input, &search.instance, &device, NULL))) {
        printf("device %04X:%04X not found\n", vid, pid);
        return 1;
    }
    IDirectInputDevice8_SetDataFormat(device, &c_dfDIJoystick);
    printf("exclusive foreground level: %08lX\n",
           (unsigned long)IDirectInputDevice8_SetCooperativeLevel(device, window, DISCL_EXCLUSIVE | DISCL_FOREGROUND));
    for (attempt = 0; attempt < 20 && FAILED(result); attempt++) {
        SetForegroundWindow(window);
        result = IDirectInputDevice8_Acquire(device);
        if (FAILED(result))
            probe_pump_for(100);
    }
    printf("acquire: %08lX (window in foreground: %s)\n", (unsigned long)result,
           GetForegroundWindow() == window ? "yes" : "no");
    probe_gain(device);
    printf("reset command: %08lX\n", (unsigned long)IDirectInputDevice8_SendForceFeedbackCommand(
                                                  device, DISFFC_RESET));
    printf("actuators on: %08lX\n",
           (unsigned long)IDirectInputDevice8_SendForceFeedbackCommand(device, DISFFC_SETACTUATORSON));
    fflush(stdout);
    probe_foreground_constant(device, 1, "foreground, constant force towards +");
    probe_foreground_constant(device, -1, "foreground, constant force towards -");
    IDirectInputDevice8_Unacquire(device);
    IDirectInputDevice8_Release(device);
    DestroyWindow(window);
    return 0;
}

static int probe_force(LPDIRECTINPUT8W input, unsigned int vid, unsigned int pid)
{
    static const probe_force_variant variants[] = {
        {"constant force, sign in direction (+)", &GUID_ConstantForce, DIEFF_CARTESIAN, 1, 6000},
        {"constant force, sign in direction (-)", &GUID_ConstantForce, DIEFF_CARTESIAN, -1, 6000},
        {"constant force, sign in magnitude (+)", &GUID_ConstantForce, DIEFF_CARTESIAN, 0, 6000},
        {"constant force, polar 90 degrees", &GUID_ConstantForce, DIEFF_POLAR, 9000, 6000},
        {"centering spring (turn the wheel)", &GUID_Spring, DIEFF_CARTESIAN, 0, 8000},
        {"sine vibration", &GUID_Sine, DIEFF_CARTESIAN, 1, 6000},
    };
    probe_search search;
    LPDIRECTINPUTDEVICE8W device = NULL;
    HWND window = CreateWindowExW(0, L"STATIC", L"fh2wheel-probe", WS_POPUP, 0, 0, 1, 1, NULL, NULL,
                                  GetModuleHandleW(NULL), NULL);
    HRESULT result;
    size_t index;

    ZeroMemory(&search, sizeof(search));
    search.vid = vid;
    search.pid = pid;
    IDirectInput8_EnumDevices(input, DI8DEVCLASS_GAMECTRL, probe_find, &search, DIEDFL_ATTACHEDONLY);
    if (!search.found || FAILED(IDirectInput8_CreateDevice(input, &search.instance, &device, NULL))) {
        printf("device %04X:%04X not found\n", vid, pid);
        return 1;
    }
    IDirectInputDevice8_SetDataFormat(device, &c_dfDIJoystick2);
    result = IDirectInputDevice8_SetCooperativeLevel(device, window, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    printf("exclusive level: %08lX\n", (unsigned long)result);
    result = IDirectInputDevice8_Acquire(device);
    printf("acquire: %08lX\n", (unsigned long)result);
    probe_print_force_state(device, "on open");
    result = IDirectInputDevice8_SendForceFeedbackCommand(device, DISFFC_RESET);
    printf("reset command: %08lX\n", (unsigned long)result);
    result = IDirectInputDevice8_SendForceFeedbackCommand(device, DISFFC_SETACTUATORSON);
    printf("actuators on command: %08lX\n", (unsigned long)result);
    probe_print_force_state(device, "after enabling");
    printf("each test lasts 1.5 s, then a 1.5 s pause; note which ones move the wheel\n");
    fflush(stdout);
    for (index = 0; index < sizeof(variants) / sizeof(variants[0]); index++)
        probe_run_variant(device, &variants[index], (int)index + 1);
    probe_print_force_state(device, "at the end");
    IDirectInputDevice8_Unacquire(device);
    IDirectInputDevice8_Release(device);
    return 0;
}

int main(int argc, char **argv)
{
    LPDIRECTINPUT8W input = NULL;
    unsigned int vid = 0;
    unsigned int pid = 0;
    int seconds = PROBE_DEFAULT_SECONDS;
    int result = 0;

    if (FAILED(DirectInput8Create(GetModuleHandleW(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8W,
                                  (void **)&input, NULL))) {
        printf("DirectInput8Create failed\n");
        return 1;
    }
    printf("connected DirectInput devices:\n");
    IDirectInput8_EnumDevices(input, DI8DEVCLASS_GAMECTRL, probe_list, input, DIEDFL_ATTACHEDONLY);
    if (argc >= 3 && strcmp(argv[1], "--foreground") == 0 && sscanf(argv[2], "%x:%x", &vid, &pid) == 2) {
        result = probe_foreground_test(input, vid, pid);
    } else if (argc >= 3 && strcmp(argv[1], "--motors") == 0 && sscanf(argv[2], "%x:%x", &vid, &pid) == 2) {
        result = probe_actuator_test(input, vid, pid);
    } else if (argc >= 3 && strcmp(argv[1], "--force") == 0 && sscanf(argv[2], "%x:%x", &vid, &pid) == 2) {
        result = probe_force(input, vid, pid);
    } else if (argc >= 2 && sscanf(argv[1], "%x:%x", &vid, &pid) == 2) {
        if (argc >= 3)
            seconds = atoi(argv[2]);
        result = probe_watch(input, vid, pid, seconds);
    }
    IDirectInput8_Release(input);
    return result;
}
