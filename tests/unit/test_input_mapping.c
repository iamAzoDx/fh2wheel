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

static void map(const fhw_config *config, const fhw_raw_state *wheel, const fhw_raw_state *pedals,
                const fhw_raw_state *shifter, fhw_wheel_input *input)
{
    fhw_raw_sources sources = {wheel, pedals, shifter};

    fhw_map_input(config, &sources, input);
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
    map(&config, &wheel, &pedals, NULL, &input);
    expect_near("steering right", input.steering, 1.0f);
    expect_near("full throttle", input.pedals[FHW_PEDAL_THROTTLE], 1.0f);
    expect_near("inverted brake released", input.pedals[FHW_PEDAL_BRAKE], 0.0f);
    expect_near("clutch from the pedals device", input.pedals[FHW_PEDAL_CLUTCH], 0.5f);
    expect_near("handbrake not assigned", input.pedals[FHW_PEDAL_HANDBRAKE], 0.0f);

    config.steering_inverted = 1;
    wheel.axes[FHW_AXIS_X] = 0;
    map(&config, &wheel, NULL, NULL, &input);
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
    map(&config, &wheel, NULL, NULL, &input);
    expect_true("button 2 -> A", (input.buttons >> FHW_BUTTON_A) & 1u);
    expect_true("diagonal -> up", (input.buttons >> FHW_BUTTON_UP) & 1u);
    expect_true("diagonal -> right", (input.buttons >> FHW_BUTTON_RIGHT) & 1u);
    expect_true("no B", !((input.buttons >> FHW_BUTTON_B) & 1u));

    wheel.hat = FHW_HAT_CENTERED;
    map(&config, &wheel, NULL, NULL, &input);
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
    map(&config, &wheel, NULL, NULL, &input);
    expect_true("hat left -> d-pad left", (input.buttons >> FHW_BUTTON_LEFT) & 1u);
}

static void shifter_config(fhw_config *config)
{
    base_config(config);
    config->buttons[FHW_BUTTON_PADDLE_LEFT] = 14;
    config->buttons[FHW_BUTTON_PADDLE_RIGHT] = 13;
    config->shifter[FHW_SHIFTER_GEAR_1] = 21;
    config->shifter[FHW_SHIFTER_GEAR_2] = 22;
    config->shifter[FHW_SHIFTER_GEAR_6] = 26;
    config->shifter[FHW_SHIFTER_REVERSE] = 27;
    config->shifter[FHW_SHIFTER_UP] = 30;
    config->shifter[FHW_SHIFTER_DOWN] = 31;
}

static void test_pattern_shifter_on_the_wheel(void)
{
    fhw_config config;
    fhw_raw_state wheel;
    fhw_wheel_input input;

    shifter_config(&config);
    fhw_raw_clear(&wheel);
    map(&config, &wheel, NULL, NULL, &input);
    expect_true("no gear button -> neutral", input.gear == FHW_GEAR_NEUTRAL);

    wheel.buttons[21] = PRESSED;
    map(&config, &wheel, NULL, NULL, &input);
    expect_true("button 22 -> 2nd gear", input.gear == 2);
    expect_true("2nd gear shown as pressed", (input.shifter >> FHW_SHIFTER_GEAR_2) & 1u);

    wheel.buttons[21] = 0;
    wheel.buttons[25] = PRESSED;
    map(&config, &wheel, NULL, NULL, &input);
    expect_true("button 26 -> 6th gear", input.gear == 6);

    wheel.buttons[25] = 0;
    wheel.buttons[26] = PRESSED;
    map(&config, &wheel, NULL, NULL, &input);
    expect_true("button 27 -> reverse", input.gear == FHW_GEAR_REVERSE);

    wheel.buttons[20] = PRESSED;
    map(&config, &wheel, NULL, NULL, &input);
    expect_true("reverse wins over a gear", input.gear == FHW_GEAR_REVERSE);
}

static void test_shifter_on_its_own_device(void)
{
    fhw_config config;
    fhw_raw_state wheel;
    fhw_raw_state shifter;
    fhw_wheel_input input;

    shifter_config(&config);
    config.shifter_vid = 0x1234;
    config.shifter_pid = 0x5678;
    fhw_raw_clear(&wheel);
    fhw_raw_clear(&shifter);
    wheel.buttons[20] = PRESSED;
    shifter.buttons[25] = PRESSED;
    map(&config, &wheel, NULL, &shifter, &input);
    expect_true("gear read from the shifter device", input.gear == 6);

    map(&config, &wheel, NULL, NULL, &input);
    expect_true("shifter device missing -> neutral", input.gear == FHW_GEAR_NEUTRAL);
}

static void test_sequential_shifter(void)
{
    fhw_config config;
    fhw_raw_state wheel;
    fhw_wheel_input input;

    shifter_config(&config);
    fhw_raw_clear(&wheel);
    wheel.buttons[29] = PRESSED;
    map(&config, &wheel, NULL, NULL, &input);
    expect_true("shift up -> right paddle", (input.buttons >> FHW_BUTTON_PADDLE_RIGHT) & 1u);
    expect_true("shift up is not the left paddle", !((input.buttons >> FHW_BUTTON_PADDLE_LEFT) & 1u));

    wheel.buttons[29] = 0;
    wheel.buttons[30] = PRESSED;
    map(&config, &wheel, NULL, NULL, &input);
    expect_true("shift down -> left paddle", (input.buttons >> FHW_BUTTON_PADDLE_LEFT) & 1u);

    wheel.buttons[30] = 0;
    wheel.buttons[12] = PRESSED;
    map(&config, &wheel, NULL, NULL, &input);
    expect_true("paddles still work", (input.buttons >> FHW_BUTTON_PADDLE_RIGHT) & 1u);
}

static void test_shifter_capabilities(void)
{
    fhw_config config;

    base_config(&config);
    expect_true("no gear bound -> no H-shifter", !fhw_has_pattern_shifter(&config));
    expect_true("no gear bound -> 7 gears", fhw_highest_gear(&config) == FHW_SHIFTER_GEAR_COUNT);

    config.shifter[FHW_SHIFTER_UP] = 30;
    expect_true("sequential only -> no H-shifter", !fhw_has_pattern_shifter(&config));

    shifter_config(&config);
    expect_true("gears bound -> H-shifter", fhw_has_pattern_shifter(&config));
    expect_true("highest bound gear is 6", fhw_highest_gear(&config) == 6);

    base_config(&config);
    config.shifter[FHW_SHIFTER_REVERSE] = 27;
    expect_true("reverse only -> H-shifter", fhw_has_pattern_shifter(&config));
}

static void test_pedals_untouched_since_start(void)
{
    fhw_config config;
    fhw_raw_state wheel;
    fhw_wheel_input input;
    fhw_pedal_rest rest;

    base_config(&config);
    config.pedals[FHW_PEDAL_THROTTLE].inverted = 1;
    fhw_raw_clear(&wheel);
    wheel.axes[FHW_AXIS_Z] = FHW_RAW_AXIS_MAX / 2;
    wheel.axes[FHW_AXIS_RZ] = FHW_RAW_AXIS_MAX / 2;
    fhw_pedal_rest_reset(&rest);

    map(&config, &wheel, NULL, NULL, &input);
    fhw_pedal_rest_apply(&rest, &input);
    expect_near("throttle stuck half-way at start -> released", input.pedals[FHW_PEDAL_THROTTLE], 0.0f);
    expect_near("brake stuck half-way at start -> released", input.pedals[FHW_PEDAL_BRAKE], 0.0f);

    wheel.axes[FHW_AXIS_Z] = FHW_RAW_AXIS_MAX / 2 + 300;
    map(&config, &wheel, NULL, NULL, &input);
    fhw_pedal_rest_apply(&rest, &input);
    expect_near("noise does not wake the throttle", input.pedals[FHW_PEDAL_THROTTLE], 0.0f);

    wheel.axes[FHW_AXIS_Z] = FHW_RAW_AXIS_MAX;
    map(&config, &wheel, NULL, NULL, &input);
    fhw_pedal_rest_apply(&rest, &input);
    expect_near("throttle released once moved", input.pedals[FHW_PEDAL_THROTTLE], 0.0f);

    wheel.axes[FHW_AXIS_Z] = FHW_RAW_AXIS_MAX / 2;
    map(&config, &wheel, NULL, NULL, &input);
    fhw_pedal_rest_apply(&rest, &input);
    expect_near("throttle follows the pedal once moved", input.pedals[FHW_PEDAL_THROTTLE], 0.5f);
    expect_near("untouched brake still released", input.pedals[FHW_PEDAL_BRAKE], 0.0f);
}

int main(void)
{
    test_axes();
    test_buttons_and_hat();
    test_dpad_from_hat();
    test_pattern_shifter_on_the_wheel();
    test_shifter_on_its_own_device();
    test_sequential_shifter();
    test_shifter_capabilities();
    test_pedals_untouched_since_start();
    if (failures != 0) {
        printf("test_input_mapping: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_input_mapping: OK\n");
    return 0;
}
