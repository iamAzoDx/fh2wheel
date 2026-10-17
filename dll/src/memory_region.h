#ifndef FHW_MEMORY_REGION_H
#define FHW_MEMORY_REGION_H

#include <stddef.h>

int fhw_region_is_readable(const void *address);
int fhw_range_is_readable(const void *address, size_t length);
int fhw_region_is_executable(const void *address);
void fhw_describe_address(const void *address, char *buffer, size_t capacity);

#endif
