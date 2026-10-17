#define COBJMACROS
#include "dinput_state.h"

#define FHW_AXIS_RANGE_MIN 0
#define FHW_FORCE_SCALE 10000.0f

void fhw_raw_from_dinput(const DIJOYSTATE2 *joystick, fhw_raw_state *state)
{
    state->axes[FHW_AXIS_NONE] = 0;
    state->axes[FHW_AXIS_X] = joystick->lX;
    state->axes[FHW_AXIS_Y] = joystick->lY;
    state->axes[FHW_AXIS_Z] = joystick->lZ;
    state->axes[FHW_AXIS_RX] = joystick->lRx;
    state->axes[FHW_AXIS_RY] = joystick->lRy;
    state->axes[FHW_AXIS_RZ] = joystick->lRz;
    state->axes[FHW_AXIS_SLIDER0] = joystick->rglSlider[0];
    state->axes[FHW_AXIS_SLIDER1] = joystick->rglSlider[1];
    CopyMemory(state->buttons, joystick->rgbButtons, sizeof(state->buttons));
    state->hat = joystick->rgdwPOV[0];
}

void fhw_set_full_axis_range(LPDIRECTINPUTDEVICE8W device)
{
    DIPROPRANGE range;

    ZeroMemory(&range, sizeof(range));
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = DIPH_DEVICE;
    range.lMin = FHW_AXIS_RANGE_MIN;
    range.lMax = FHW_RAW_AXIS_MAX;
    IDirectInputDevice8_SetProperty(device, DIPROP_RANGE, &range.diph);
}

unsigned int fhw_device_vid(const DIDEVICEINSTANCEW *instance)
{
    return LOWORD(instance->guidProduct.Data1);
}

unsigned int fhw_device_pid(const DIDEVICEINSTANCEW *instance)
{
    return HIWORD(instance->guidProduct.Data1);
}

HRESULT fhw_force_effect_create(LPDIRECTINPUTDEVICE8W device, fhw_force_effect *force)
{
    DIPROPDWORD autocenter;
    HRESULT result;

    ZeroMemory(force, sizeof(*force));
    ZeroMemory(&autocenter, sizeof(autocenter));
    autocenter.diph.dwSize = sizeof(autocenter);
    autocenter.diph.dwHeaderSize = sizeof(autocenter.diph);
    autocenter.diph.dwHow = DIPH_DEVICE;
    autocenter.dwData = DIPROPAUTOCENTER_OFF;
    IDirectInputDevice8_SetProperty(device, DIPROP_AUTOCENTER, &autocenter.diph);

    force->axes[0] = DIJOFS_X;
    force->direction[0] = 0;
    force->parameters.dwSize = sizeof(force->parameters);
    force->parameters.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    force->parameters.dwDuration = INFINITE;
    force->parameters.dwSamplePeriod = 0;
    force->parameters.dwGain = DI_FFNOMINALMAX;
    force->parameters.dwTriggerButton = DIEB_NOTRIGGER;
    force->parameters.cAxes = 1;
    force->parameters.rgdwAxes = force->axes;
    force->parameters.rglDirection = force->direction;
    force->parameters.cbTypeSpecificParams = sizeof(force->constant_force);
    force->parameters.lpvTypeSpecificParams = &force->constant_force;

    result = IDirectInputDevice8_CreateEffect(device, &GUID_ConstantForce, &force->parameters, &force->effect, NULL);
    if (FAILED(result)) {
        force->effect = NULL;
        force->last_result = result;
        return result;
    }
    force->last_result = IDirectInputEffect_Start(force->effect, 1, 0);
    return force->last_result;
}

HRESULT fhw_force_effect_set(fhw_force_effect *force, float level, int restart)
{
    DWORD flags = DIEP_TYPESPECIFICPARAMS | (restart ? DIEP_START : 0);

    if (force->effect == NULL)
        return E_POINTER;
    if (level > 1.0f)
        level = 1.0f;
    if (level < -1.0f)
        level = -1.0f;
    force->constant_force.lMagnitude = (LONG)(level * FHW_FORCE_SCALE);
    force->last_result = IDirectInputEffect_SetParameters(force->effect, &force->parameters, flags);
    return force->last_result;
}

void fhw_force_effect_release(fhw_force_effect *force)
{
    if (force->effect == NULL)
        return;
    IDirectInputEffect_Stop(force->effect);
    IDirectInputEffect_Release(force->effect);
    force->effect = NULL;
}
