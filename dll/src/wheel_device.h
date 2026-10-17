#ifndef FHW_WHEEL_DEVICE_H
#define FHW_WHEEL_DEVICE_H

#include "config.h"
#include "input_mapping.h"

int fhw_device_open(const fhw_config *config, void *module);
int fhw_device_poll(fhw_wheel_input *input);
int fhw_device_has_force(void);
void fhw_device_set_force(float force);
void fhw_device_close(void);

#endif
