#ifndef FHW_CONTROLLER_STATICS_H
#define FHW_CONTROLLER_STATICS_H

#include "wheel_controller.h"

typedef long (*fhw_activation_function)(const void *class_name, const void *interface_id, void **factory);

void fhw_statics_init(fhw_activation_function next_activation);
void fhw_statics_wrap(const void *class_name, const void *interface_id, void **factory);
fhw_user_provider fhw_statics_user_provider(void);

#endif
