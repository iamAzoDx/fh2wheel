#ifndef FHW_CONFIG_H
#define FHW_CONFIG_H

#include <stddef.h>

#define FHW_BINDING_NONE 0
#define FHW_BINDING_HAT_UP (-1)
#define FHW_BINDING_HAT_DOWN (-2)
#define FHW_BINDING_HAT_LEFT (-3)
#define FHW_BINDING_HAT_RIGHT (-4)
#define FHW_MAX_BUTTON_NUMBER 128
#define FHW_MIN_GAIN_PERCENT 0
#define FHW_MAX_GAIN_PERCENT 300
#define FHW_MIN_FREQUENCY_HZ 50u
#define FHW_MAX_FREQUENCY_HZ 1000u

typedef enum {
    FHW_AXIS_NONE,
    FHW_AXIS_X,
    FHW_AXIS_Y,
    FHW_AXIS_Z,
    FHW_AXIS_RX,
    FHW_AXIS_RY,
    FHW_AXIS_RZ,
    FHW_AXIS_SLIDER0,
    FHW_AXIS_SLIDER1,
    FHW_AXIS_COUNT
} fhw_axis;

typedef enum {
    FHW_SOURCE_WHEEL,
    FHW_SOURCE_PEDALS
} fhw_source;

typedef enum {
    FHW_BUTTON_A,
    FHW_BUTTON_B,
    FHW_BUTTON_X,
    FHW_BUTTON_Y,
    FHW_BUTTON_VIEW,
    FHW_BUTTON_MENU,
    FHW_BUTTON_UP,
    FHW_BUTTON_DOWN,
    FHW_BUTTON_LEFT,
    FHW_BUTTON_RIGHT,
    FHW_BUTTON_PADDLE_LEFT,
    FHW_BUTTON_PADDLE_RIGHT,
    FHW_BUTTON_COUNT
} fhw_button;

typedef enum {
    FHW_PEDAL_THROTTLE,
    FHW_PEDAL_BRAKE,
    FHW_PEDAL_CLUTCH,
    FHW_PEDAL_HANDBRAKE,
    FHW_PEDAL_COUNT
} fhw_pedal;

typedef struct {
    fhw_axis axis;
    fhw_source source;
    int inverted;
} fhw_pedal_mapping;

typedef struct {
    int enabled;
    unsigned int wheel_vid;
    unsigned int wheel_pid;
    unsigned int pedals_vid;
    unsigned int pedals_pid;
    fhw_axis steering_axis;
    int steering_inverted;
    fhw_pedal_mapping pedals[FHW_PEDAL_COUNT];
    int buttons[FHW_BUTTON_COUNT];
    int dpad_from_hat;
    unsigned int rotation_degrees;
    unsigned int reported_device_id;
    int force_enabled;
    int force_gain_percent;
    int force_inverted;
    unsigned int force_frequency_hz;
    int trace;
} fhw_config;

void fhw_config_defaults(fhw_config *config);
void fhw_config_load(fhw_config *config);
int fhw_config_save(const fhw_config *config);
const char *fhw_axis_name(fhw_axis axis);
const char *fhw_button_name(fhw_button button);
const char *fhw_pedal_name(fhw_pedal pedal);
void fhw_binding_text(int binding, wchar_t *out, size_t capacity);

#endif
