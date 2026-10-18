#include <string.h>

#include "input_mapping.h"

#define FHW_AXIS_CENTER 32767.5f
#define FHW_HAT_CENTERED_MASK 0xFFFFul
#define FHW_HAT_FULL_TURN 36000ul
#define FHW_HAT_QUARTER 9000ul
#define FHW_HAT_EIGHTH 4500ul
#define FHW_BUTTON_PRESSED 0x80
#define FHW_PEDAL_WAKE_DELTA 0.05f
#define FHW_HANDBRAKE_PULLED 0.3f

void fhw_raw_clear(fhw_raw_state *state)
{
    memset(state, 0, sizeof(*state));
    state->hat = FHW_HAT_CENTERED;
}

static unsigned long fhw_hat_distance(unsigned long hat, unsigned long direction)
{
    unsigned long difference = hat > direction ? hat - direction : direction - hat;

    return difference > FHW_HAT_FULL_TURN / 2 ? FHW_HAT_FULL_TURN - difference : difference;
}

int fhw_hat_matches(unsigned long hat, int binding)
{
    unsigned long direction;

    if ((hat & FHW_HAT_CENTERED_MASK) == FHW_HAT_CENTERED_MASK || hat >= FHW_HAT_FULL_TURN)
        return 0;
    switch (binding) {
    case FHW_BINDING_HAT_UP:
        direction = 0;
        break;
    case FHW_BINDING_HAT_RIGHT:
        direction = FHW_HAT_QUARTER;
        break;
    case FHW_BINDING_HAT_DOWN:
        direction = 2 * FHW_HAT_QUARTER;
        break;
    case FHW_BINDING_HAT_LEFT:
        direction = 3 * FHW_HAT_QUARTER;
        break;
    default:
        return 0;
    }
    return fhw_hat_distance(hat, direction) <= FHW_HAT_EIGHTH;
}

int fhw_binding_pressed(const fhw_raw_state *state, int binding)
{
    if (binding > 0 && binding <= FHW_RAW_BUTTON_COUNT)
        return (state->buttons[binding - 1] & FHW_BUTTON_PRESSED) != 0;
    if (binding < 0)
        return fhw_hat_matches(state->hat, binding);
    return 0;
}

static float fhw_clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

int fhw_has_pattern_shifter(const fhw_config *config)
{
    int index;

    for (index = FHW_SHIFTER_GEAR_1; index <= FHW_SHIFTER_REVERSE; index++) {
        if (config->shifter[index] != FHW_BINDING_NONE)
            return 1;
    }
    return 0;
}

unsigned char fhw_highest_gear(const fhw_config *config)
{
    int index;

    for (index = FHW_SHIFTER_GEAR_COUNT - 1; index >= 0; index--) {
        if (config->shifter[FHW_SHIFTER_GEAR_1 + index] != FHW_BINDING_NONE)
            return (unsigned char)(index + 1);
    }
    return FHW_SHIFTER_GEAR_COUNT;
}

static void fhw_map_shifter(const fhw_config *config, const fhw_raw_state *shifter, fhw_wheel_input *input)
{
    int index;

    input->shifter = 0;
    input->gear = FHW_GEAR_NEUTRAL;
    if (shifter == NULL)
        return;
    for (index = 0; index < FHW_SHIFTER_COUNT; index++) {
        if (fhw_binding_pressed(shifter, config->shifter[index]))
            input->shifter |= 1u << index;
    }
    if ((input->shifter >> FHW_SHIFTER_REVERSE) & 1u) {
        input->gear = FHW_GEAR_REVERSE;
    } else {
        for (index = 0; index < FHW_SHIFTER_GEAR_COUNT && input->gear == FHW_GEAR_NEUTRAL; index++) {
            if ((input->shifter >> (FHW_SHIFTER_GEAR_1 + index)) & 1u)
                input->gear = (unsigned char)(index + 1);
        }
    }
    if ((input->shifter >> FHW_SHIFTER_UP) & 1u)
        input->buttons |= 1u << FHW_BUTTON_PADDLE_RIGHT;
    if ((input->shifter >> FHW_SHIFTER_DOWN) & 1u)
        input->buttons |= 1u << FHW_BUTTON_PADDLE_LEFT;
}

static const fhw_raw_state *fhw_axis_source(const fhw_raw_sources *sources, fhw_source source)
{
    switch (source) {
    case FHW_SOURCE_PEDALS:
        return sources->pedals;
    case FHW_SOURCE_HANDBRAKE:
        return sources->handbrake;
    default:
        return sources->wheel;
    }
}

void fhw_map_handbrake(const fhw_config *config, fhw_wheel_input *input)
{
    if ((unsigned int)config->handbrake_button < FHW_BUTTON_COUNT &&
        input->pedals[FHW_PEDAL_HANDBRAKE] > FHW_HANDBRAKE_PULLED)
        input->buttons |= 1u << config->handbrake_button;
}

void fhw_map_input(const fhw_config *config, const fhw_raw_sources *sources, fhw_wheel_input *input)
{
    static const int fhw_hat_dpad[] = {FHW_BINDING_HAT_UP, FHW_BINDING_HAT_DOWN, FHW_BINDING_HAT_LEFT,
                                       FHW_BINDING_HAT_RIGHT};
    static const fhw_button fhw_dpad_buttons[] = {FHW_BUTTON_UP, FHW_BUTTON_DOWN, FHW_BUTTON_LEFT, FHW_BUTTON_RIGHT};
    const fhw_raw_state *wheel = sources->wheel;
    unsigned int index;

    input->steering = 0.0f;
    if (config->steering_axis != FHW_AXIS_NONE)
        input->steering = ((float)wheel->axes[config->steering_axis] - FHW_AXIS_CENTER) / FHW_AXIS_CENTER;
    if (config->steering_inverted)
        input->steering = -input->steering;
    input->steering = fhw_clamp(input->steering, -1.0f, 1.0f);

    for (index = 0; index < FHW_PEDAL_COUNT; index++) {
        const fhw_pedal_mapping *mapping = &config->pedals[index];
        const fhw_raw_state *source = fhw_axis_source(sources, mapping->source);
        float value;

        if (mapping->axis == FHW_AXIS_NONE || source == NULL) {
            input->pedals[index] = 0.0f;
            continue;
        }
        value = (float)source->axes[mapping->axis] / (float)FHW_RAW_AXIS_MAX;
        if (mapping->inverted)
            value = 1.0f - value;
        input->pedals[index] = fhw_clamp(value, 0.0f, 1.0f);
    }

    input->buttons = 0;
    for (index = 0; index < FHW_BUTTON_COUNT; index++) {
        if (fhw_binding_pressed(wheel, config->buttons[index]))
            input->buttons |= 1u << index;
    }
    if (config->dpad_from_hat) {
        for (index = 0; index < sizeof(fhw_hat_dpad) / sizeof(fhw_hat_dpad[0]); index++) {
            if (fhw_hat_matches(wheel->hat, fhw_hat_dpad[index]))
                input->buttons |= 1u << fhw_dpad_buttons[index];
        }
    }
    fhw_map_shifter(config, config->shifter_vid != 0 ? sources->shifter : wheel, input);
}

void fhw_pedal_rest_reset(fhw_pedal_rest *rest)
{
    memset(rest, 0, sizeof(*rest));
}

void fhw_pedal_rest_apply(fhw_pedal_rest *rest, fhw_wheel_input *input)
{
    unsigned int index;

    if (!rest->primed) {
        memcpy(rest->initial, input->pedals, sizeof(rest->initial));
        rest->primed = 1;
    }
    for (index = 0; index < FHW_PEDAL_COUNT; index++) {
        float change = input->pedals[index] - rest->initial[index];

        if (change > FHW_PEDAL_WAKE_DELTA || change < -FHW_PEDAL_WAKE_DELTA)
            rest->moved[index] = 1;
        if (!rest->moved[index])
            input->pedals[index] = 0.0f;
    }
}
