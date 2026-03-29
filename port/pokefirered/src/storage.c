#include "pfr/storage.h"
#include "pfr/common.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool
pfr_storage_write_file(const char* path, const void* data, size_t size)
{
  FILE* file = fopen(path, "wb");

  if (file == NULL) {
    return false;
  }

  if (fwrite(data, 1, size, file) != size) {
    fclose(file);
    return false;
  }

  return fclose(file) == 0;
}

static bool
pfr_storage_replace_file(const char* temp_path, const char* path)
{
  char backup_path[PFR_MAX_PATH + 4];

  if (rename(temp_path, path) == 0) {
    return true;
  }

  if (snprintf(backup_path, sizeof(backup_path), "%s.bak", path) >=
      (int)sizeof(backup_path)) {
    return false;
  }

  remove(backup_path);

  if (rename(path, backup_path) != 0) {
    return false;
  }

  if (rename(temp_path, path) == 0) {
    remove(backup_path);
    return true;
  }

  rename(backup_path, path);
  return false;
}

bool
pfr_storage_default_path(char* buffer, size_t buffer_size)
{
  const char* application_directory;

  if (buffer_size > 0) {
    buffer[0] = '\0';
  }

  application_directory = GetApplicationDirectory();
  if (application_directory == NULL || application_directory[0] == '\0') {
    return false;
  }

  return snprintf(
           buffer, buffer_size, "%spokefirered.sav", application_directory) <
         (int)buffer_size;
}

bool
pfr_storage_load(const char* path, void* data, size_t size)
{
  FILE* file = fopen(path, "rb");
  size_t bytes_read;

  if (file == NULL) {
    memset(data, 0, size);
    return false;
  }

  memset(data, 0, size);

  bytes_read = fread(data, 1, size, file);
  if (bytes_read < size && ferror(file)) {
    fclose(file);
    return false;
  }

  fclose(file);
  return true;
}

bool
pfr_storage_save(const char* path, const void* data, size_t size)
{
  char temp_path[PFR_MAX_PATH + 4];

  if (snprintf(temp_path, sizeof(temp_path), "%s.tmp", path) >=
      (int)sizeof(temp_path)) {
    return false;
  }

  if (!pfr_storage_write_file(temp_path, data, size)) {
    return false;
  }

  return pfr_storage_replace_file(temp_path, path);
}
