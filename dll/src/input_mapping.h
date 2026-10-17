#ifndef FHW_INPUT_MAPPING_H
#define FHW_INPUT_MAPPING_H

#include "config.h"

#define FHW_RAW_AXIS_MAX 65535L
#define FHW_RAW_BUTTON_COUNT 128
#define FHW_HAT_CENTERED 0xFFFFFFFFul

typedef struct {
    long axes[FHW_AXIS_COUNT];
    unsigned char buttons[FHW_RAW_BUTTON_COUNT];
    unsigned long hat;
} fhw_raw_state;

typedef struct {
    float steering;
    float pedals[FHW_PEDAL_COUNT];
    unsigned int buttons;
} fhw_wheel_input;

void fhw_raw_clear(fhw_raw_state *state);
int fhw_hat_matches(unsigned long hat, int binding);
int fhw_binding_pressed(const fhw_raw_state *state, int binding);
void fhw_map_input(const fhw_config *config, const fhw_raw_state *wheel, const fhw_raw_state *pedals,
                   fhw_wheel_input *input);

#endif
