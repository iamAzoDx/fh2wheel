#ifndef FHW_FORCE_LOOP_H
#define FHW_FORCE_LOOP_H

#include "config.h"
#include "ffb_vm.h"
#include "wheel_device.h"

int fhw_force_start(const fhw_config *config, void *module);
int fhw_force_wheel_present(void);
void fhw_force_lock(void);
void fhw_force_unlock(void);
fhw_vm *fhw_force_vm(void);
double fhw_force_now_ms(void);
void fhw_force_snapshot(fhw_wheel_input *input, unsigned long long *timestamp);

#endif
