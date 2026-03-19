#include "pfr/storage.h"
#include "pfr/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

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

bool
pfr_storage_default_path(char* buffer, size_t buffer_size)
{
  if (buffer_size > 0) {
    buffer[0] = '\0';
  }

  char executable_path[PFR_MAX_PATH];
  char* separator = NULL;

#if defined(_WIN32)
  DWORD length =
    GetModuleFileNameA(NULL, executable_path, (DWORD)sizeof(executable_path));

  if (length == 0 || length >= sizeof(executable_path)) {
    return false;
  }
#else
  ssize_t length =
    readlink("/proc/self/exe", executable_path, sizeof(executable_path) - 1);

  if (length < 0) {
    if (getcwd(executable_path, sizeof(executable_path)) == NULL) {
      return false;
    }

    length = (ssize_t)strlen(executable_path);
  }

  executable_path[length] = '\0';
#endif

  separator = strrchr(executable_path, '/');

#if defined(_WIN32)
  {
    char* windows_separator = strrchr(executable_path, '\\');

    if (windows_separator != NULL &&
        (separator == NULL || windows_separator > separator)) {
      separator = windows_separator;
    }
  }
#endif

  if (separator != NULL) {
    *separator = '\0';
  }

#if defined(_WIN32)
  return snprintf(buffer, buffer_size, "%s\\pokefirered.sav", executable_path) <
         (int)buffer_size;
#else
  return snprintf(buffer, buffer_size, "%s/pokefirered.sav", executable_path) <
         (int)buffer_size;
#endif
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

#if defined(_WIN32)
  return MoveFileExA(temp_path,
                     path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(temp_path, path) == 0;
#endif
}
