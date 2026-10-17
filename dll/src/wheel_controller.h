#ifndef FHW_WHEEL_CONTROLLER_H
#define FHW_WHEEL_CONTROLLER_H

#include <windows.h>

#include "config.h"

typedef HRESULT (*fhw_user_provider)(void **user);

void fhw_wheel_init(const fhw_config *config, fhw_user_provider user_provider);
void *fhw_wheel_controller(void);

#endif
