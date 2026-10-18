#ifndef FHW_INPUT_MAPPING_H
#define FHW_INPUT_MAPPING_H

#include "config.h"

#define FHW_RAW_AXIS_MAX 65535L
#define FHW_RAW_BUTTON_COUNT 128
#define FHW_HAT_CENTERED 0xFFFFFFFFul
#define FHW_GEAR_NEUTRAL 0u
#define FHW_GEAR_REVERSE 0xFFu

typedef struct {
    long axes[FHW_AXIS_COUNT];
    unsigned char buttons[FHW_RAW_BUTTON_COUNT];
    unsigned long hat;
} fhw_raw_state;

typedef struct {
    const fhw_raw_state *wheel;
    const fhw_raw_state *pedals;
    const fhw_raw_state *shifter;
    const fhw_raw_state *handbrake;
} fhw_raw_sources;

typedef struct {
    float steering;
    float pedals[FHW_PEDAL_COUNT];
    unsigned int buttons;
    unsigned int shifter;
    unsigned char gear;
} fhw_wheel_input;

typedef struct {
    int primed;
    float initial[FHW_PEDAL_COUNT];
    int moved[FHW_PEDAL_COUNT];
} fhw_pedal_rest;

void fhw_raw_clear(fhw_raw_state *state);
int fhw_hat_matches(unsigned long hat, int binding);
int fhw_binding_pressed(const fhw_raw_state *state, int binding);
int fhw_has_pattern_shifter(const fhw_config *config);
unsigned char fhw_highest_gear(const fhw_config *config);
void fhw_map_input(const fhw_config *config, const fhw_raw_sources *sources, fhw_wheel_input *input);
void fhw_map_handbrake(const fhw_config *config, fhw_wheel_input *input);
void fhw_pedal_rest_reset(fhw_pedal_rest *rest);
void fhw_pedal_rest_apply(fhw_pedal_rest *rest, fhw_wheel_input *input);

#endif
