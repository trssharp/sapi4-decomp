#pragma once
#include <stdint.h>
// load msttssyn.dll's sections from `path` (1: ok)
int dll_image_load(const char *path);
void *decomp_dll_data(uint32_t addr);
void **decomp_dll_ptr(uint32_t addr);
