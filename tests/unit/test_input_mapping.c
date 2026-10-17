#include <math.h>
#include <stdio.h>
#include <string.h>

#include "input_mapping.h"

#define PRESSED 0x80

static int failures = 0;

static void expect_near(const char *what, float actual, float expected)
{
    if (fabsf(actual - expected) > 1e-3f) {
        printf("FAIL %s: got %f, expected %f\n", what, actual, expected);
        failures++;
    }
}

static void expect_true(const char *what, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static void base_config(fhw_config *config)
{
    memset(config, 0, sizeof(*config));
    config->steering_axis = FHW_AXIS_X;
    config->pedals[FHW_PEDAL_THROTTLE].axis = FHW_AXIS_Z;
    config->pedals[FHW_PEDAL_BRAKE].axis = FHW_AXIS_RZ;
    config->pedals[FHW_PEDAL_BRAKE].inverted = 1;
    config->pedals[FHW_PEDAL_CLUTCH].axis = FHW_AXIS_SLIDER0;
    config->pedals[FHW_PEDAL_CLUTCH].source = FHW_SOURCE_PEDALS;
    config->buttons[FHW_BUTTON_A] = 2;
    config->buttons[FHW_BUTTON_UP] = FHW_BINDING_HAT_UP;
    config->buttons[FHW_BUTTON_RIGHT] = FHW_BINDING_HAT_RIGHT;
}

static void test_axes(void)
{
    fhw_config config;
    fhw_raw_state wheel;
    fhw_raw_state pedals;
    fhw_wheel_input input;

    base_config(&config);
    fhw_raw_clear(&wheel);
    fhw_raw_clear(&pedals);
    wheel.axes[FHW_AXIS_X] = FHW_RAW_AXIS_MAX;
    wheel.axes[FHW_AXIS_Z] = FHW_RAW_AXIS_MAX;
    wheel.axes[FHW_AXIS_RZ] = FHW_RAW_AXIS_MAX;
    pedals.axes[FHW_AXIS_SLIDER0] = FHW_RAW_AXIS_MAX / 2;
    fhw_map_input(&config, &wheel, &pedals, &input);
    expect_near("steering right", input.steering, 1.0f);
    expect_near("full throttle", input.pedals[FHW_PEDAL_THROTTLE], 1.0f);
    expect_near("inverted brake released", input.pedals[FHW_PEDAL_BRAKE], 0.0f);
    expect_near("clutch from the pedals device", input.pedals[FHW_PEDAL_CLUTCH], 0.5f);
    expect_near("handbrake not assigned", input.pedals[FHW_PEDAL_HANDBRAKE], 0.0f);

    config.steering_inverted = 1;
    wheel.axes[FHW_AXIS_X] = 0;
    fhw_map_input(&config, &wheel, NULL, &input);
    expect_near("steering inverted", input.steering, 1.0f);
    expect_near("pedals device missing", input.pedals[FHW_PEDAL_CLUTCH], 0.0f);
}

static void test_buttons_and_hat(void)
{
    fhw_config config;
    fhw_raw_state wheel;
    fhw_wheel_input input;

    base_config(&config);
    fhw_raw_clear(&wheel);
    wheel.buttons[1] = PRESSED;
    wheel.hat = 4500;
    fhw_map_input(&config, &wheel, NULL, &input);
    expect_true("button 2 -> A", (input.buttons >> FHW_BUTTON_A) & 1u);
    expect_true("diagonal -> up", (input.buttons >> FHW_BUTTON_UP) & 1u);
    expect_true("diagonal -> right", (input.buttons >> FHW_BUTTON_RIGHT) & 1u);
    expect_true("no B", !((input.buttons >> FHW_BUTTON_B) & 1u));

    wheel.hat = FHW_HAT_CENTERED;
    fhw_map_input(&config, &wheel, NULL, &input);
    expect_true("hat centered", !((input.buttons >> FHW_BUTTON_UP) & 1u));
    expect_true("hat 0xFFFF centered", !fhw_hat_matches(0xFFFFul, FHW_BINDING_HAT_UP));
    expect_true("hat 31500 = up", fhw_hat_matches(31500ul, FHW_BINDING_HAT_UP));
    expect_true("hat 18000 = down", fhw_hat_matches(18000ul, FHW_BINDING_HAT_DOWN));
    expect_true("button out of range", !fhw_binding_pressed(&wheel, FHW_RAW_BUTTON_COUNT + 1));
}

static void test_dpad_from_hat(void)
{
    fhw_config config;
    fhw_raw_state wheel;
    fhw_wheel_input input;

    base_config(&config);
    config.buttons[FHW_BUTTON_LEFT] = 0;
    config.dpad_from_hat = 1;
    fhw_raw_clear(&wheel);
    wheel.hat = 27000;
    fhw_map_input(&config, &wheel, NULL, &input);
    expect_true("hat left -> d-pad left", (input.buttons >> FHW_BUTTON_LEFT) & 1u);
}

int main(void)
{
    test_axes();
    test_buttons_and_hat();
    test_dpad_from_hat();
    if (failures != 0) {
        printf("test_input_mapping: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_input_mapping: OK\n");
    return 0;
}
