#include "pfr/scripted_input.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/io_reg.h"

#define PFR_SCRIPTED_INPUT_LINE_BUFFER_SIZE 256

static void
pfr_scripted_input_set_error(char* error,
                             size_t error_size,
                             const char* format,
                             ...)
{
  va_list args;

  if (error == NULL || error_size == 0) {
    return;
  }

  va_start(args, format);
  vsnprintf(error, error_size, format, args);
  va_end(args);
}

static char*
pfr_scripted_input_trim(char* text)
{
  char* end;

  while (*text != '\0' && isspace((unsigned char)*text)) {
    text++;
  }

  end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1])) {
    end--;
  }

  *end = '\0';
  return text;
}

static bool
pfr_scripted_input_parse_u32(const char* text, uint32_t* value)
{
  char* end = NULL;
  unsigned long parsed = strtoul(text, &end, 0);

  if (text[0] == '\0' || end == NULL || *end != '\0' || parsed > 0xFFFFFFFFUL) {
    return false;
  }

  *value = (uint32_t)parsed;
  return true;
}

static int
pfr_scripted_input_ascii_casecmp(const char* lhs, const char* rhs)
{
  while (*lhs != '\0' && *rhs != '\0') {
    int lhs_char = tolower((unsigned char)*lhs);
    int rhs_char = tolower((unsigned char)*rhs);

    if (lhs_char != rhs_char) {
      return lhs_char - rhs_char;
    }

    lhs++;
    rhs++;
  }

  return tolower((unsigned char)*lhs) - tolower((unsigned char)*rhs);
}

static bool
pfr_scripted_input_is_key_delimiter(char ch)
{
  return ch == '+' || ch == ',' || ch == ' ' || ch == '\t';
}

static bool
pfr_scripted_input_parse_named_keys(const char* text, uint16_t* keys)
{
  char buffer[PFR_SCRIPTED_INPUT_LINE_BUFFER_SIZE];
  char* token = NULL;
  char* cursor = NULL;

  snprintf(buffer, sizeof(buffer), "%s", text);
  *keys = 0;

  cursor = buffer;
  while (*cursor != '\0') {
    while (*cursor != '\0' && pfr_scripted_input_is_key_delimiter(*cursor)) {
      cursor++;
    }

    if (*cursor == '\0') {
      break;
    }

    token = cursor;
    while (*cursor != '\0' && !pfr_scripted_input_is_key_delimiter(*cursor)) {
      cursor++;
    }

    if (*cursor != '\0') {
      *cursor = '\0';
      cursor++;
    }

    if (pfr_scripted_input_ascii_casecmp(token, "A") == 0) {
      *keys |= A_BUTTON;
    } else if (pfr_scripted_input_ascii_casecmp(token, "B") == 0) {
      *keys |= B_BUTTON;
    } else if (pfr_scripted_input_ascii_casecmp(token, "SELECT") == 0) {
      *keys |= SELECT_BUTTON;
    } else if (pfr_scripted_input_ascii_casecmp(token, "START") == 0) {
      *keys |= START_BUTTON;
    } else if (pfr_scripted_input_ascii_casecmp(token, "RIGHT") == 0) {
      *keys |= DPAD_RIGHT;
    } else if (pfr_scripted_input_ascii_casecmp(token, "LEFT") == 0) {
      *keys |= DPAD_LEFT;
    } else if (pfr_scripted_input_ascii_casecmp(token, "UP") == 0) {
      *keys |= DPAD_UP;
    } else if (pfr_scripted_input_ascii_casecmp(token, "DOWN") == 0) {
      *keys |= DPAD_DOWN;
    } else if (pfr_scripted_input_ascii_casecmp(token, "R") == 0) {
      *keys |= R_BUTTON;
    } else if (pfr_scripted_input_ascii_casecmp(token, "L") == 0) {
      *keys |= L_BUTTON;
    } else if (pfr_scripted_input_ascii_casecmp(token, "NONE") == 0) {
      continue;
    } else {
      return false;
    }
  }

  return true;
}

static bool
pfr_scripted_input_parse_keys(const char* text, uint16_t* keys)
{
  uint32_t parsed = 0;

  if (pfr_scripted_input_parse_u32(text, &parsed)) {
    *keys = (uint16_t)parsed;
    return true;
  }

  return pfr_scripted_input_parse_named_keys(text, keys);
}

void
pfr_scripted_input_init(PfrScriptedInput* scripted_input)
{
  scripted_input->items = NULL;
  scripted_input->count = 0;
  scripted_input->capacity = 0;
}

void
pfr_scripted_input_free(PfrScriptedInput* scripted_input)
{
  free(scripted_input->items);
  scripted_input->items = NULL;
  scripted_input->count = 0;
  scripted_input->capacity = 0;
}

bool
pfr_scripted_input_append(PfrScriptedInput* scripted_input,
                          uint32_t frame,
                          uint16_t keys,
                          char* error,
                          size_t error_size)
{
  if (scripted_input->count == scripted_input->capacity) {
    size_t new_capacity =
      scripted_input->capacity == 0 ? 8 : scripted_input->capacity * 2;
    PfrScriptedInputEvent* new_items = (PfrScriptedInputEvent*)realloc(
      scripted_input->items, new_capacity * sizeof(PfrScriptedInputEvent));

    if (new_items == NULL) {
      pfr_scripted_input_set_error(error, error_size, "out of memory");
      return false;
    }

    scripted_input->items = new_items;
    scripted_input->capacity = new_capacity;
  }

  scripted_input->items[scripted_input->count].frame = frame;
  scripted_input->items[scripted_input->count].keys = keys;
  scripted_input->count++;
  return true;
}

bool
pfr_scripted_input_load_manifest(PfrScriptedInput* scripted_input,
                                 const char* path,
                                 char* error,
                                 size_t error_size)
{
  FILE* file = fopen(path, "r");
  char line_buffer[PFR_SCRIPTED_INPUT_LINE_BUFFER_SIZE];
  unsigned long line_number = 0;

  if (file == NULL) {
    pfr_scripted_input_set_error(
      error, error_size, "failed to open input manifest: %s", path);
    return false;
  }

  while (fgets(line_buffer, sizeof(line_buffer), file) != NULL) {
    char* line;
    char* separator;
    char* frame_text;
    char* keys_text;
    uint32_t frame = 0;
    uint16_t keys = 0;

    line_number++;
    line = pfr_scripted_input_trim(line_buffer);
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }

    separator = strchr(line, '|');
    if (separator == NULL) {
      fclose(file);
      pfr_scripted_input_set_error(error,
                                   error_size,
                                   "invalid input manifest line %lu in %s",
                                   line_number,
                                   path);
      return false;
    }

    *separator = '\0';
    frame_text = pfr_scripted_input_trim(line);
    keys_text = pfr_scripted_input_trim(separator + 1);

    if (!pfr_scripted_input_parse_u32(frame_text, &frame) ||
        !pfr_scripted_input_parse_keys(keys_text, &keys)) {
      fclose(file);
      pfr_scripted_input_set_error(error,
                                   error_size,
                                   "invalid input manifest line %lu in %s",
                                   line_number,
                                   path);
      return false;
    }

    if (!pfr_scripted_input_append(
          scripted_input, frame, keys, error, error_size)) {
      fclose(file);
      return false;
    }
  }

  fclose(file);
  return true;
}

uint16_t
pfr_scripted_input_keys_for_frame(const PfrScriptedInput* scripted_input,
                                  uint32_t frame)
{
  size_t i;
  uint16_t keys = 0;

  for (i = 0; i < scripted_input->count; i++) {
    if (scripted_input->items[i].frame == frame) {
      keys |= scripted_input->items[i].keys;
    }
  }

  return keys;
}
