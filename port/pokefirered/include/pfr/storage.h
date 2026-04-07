#ifndef PFR_STORAGE_H
#define PFR_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

bool
pfr_storage_default_path(char* buffer, size_t buffer_size);
bool
pfr_storage_load(const char* path, void* data, size_t size);
bool
pfr_storage_save(const char* path, const void* data, size_t size);

#endif
