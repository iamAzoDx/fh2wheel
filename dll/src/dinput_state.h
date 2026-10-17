#ifndef FHW_DINPUT_STATE_H
#define FHW_DINPUT_STATE_H

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include "input_mapping.h"

typedef struct {
    LPDIRECTINPUTEFFECT effect;
    DIEFFECT parameters;
    DICONSTANTFORCE constant_force;
    DWORD axes[1];
    LONG direction[1];
    HRESULT last_result;
} fhw_force_effect;

void fhw_raw_from_dinput(const DIJOYSTATE2 *joystick, fhw_raw_state *state);
void fhw_set_full_axis_range(LPDIRECTINPUTDEVICE8W device);
unsigned int fhw_device_vid(const DIDEVICEINSTANCEW *instance);
unsigned int fhw_device_pid(const DIDEVICEINSTANCEW *instance);
HRESULT fhw_force_effect_create(LPDIRECTINPUTDEVICE8W device, fhw_force_effect *force);
HRESULT fhw_force_effect_set(fhw_force_effect *force, float level, int restart);
void fhw_force_effect_release(fhw_force_effect *force);

#endif
