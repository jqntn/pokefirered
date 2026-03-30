#ifndef PFR_SCRIPTED_INPUT_H
#define PFR_SCRIPTED_INPUT_H

#include "pfr/common.h"

typedef struct PfrScriptedInputEvent
{
  uint32_t frame;
  uint16_t keys;
} PfrScriptedInputEvent;

typedef struct PfrScriptedInput
{
  PfrScriptedInputEvent* items;
  size_t count;
  size_t capacity;
} PfrScriptedInput;

void
pfr_scripted_input_init(PfrScriptedInput* scripted_input);
void
pfr_scripted_input_free(PfrScriptedInput* scripted_input);
bool
pfr_scripted_input_append(PfrScriptedInput* scripted_input,
                          uint32_t frame,
                          uint16_t keys,
                          char* error,
                          size_t error_size);
bool
pfr_scripted_input_load_manifest(PfrScriptedInput* scripted_input,
                                 const char* path,
                                 char* error,
                                 size_t error_size);
uint16_t
pfr_scripted_input_keys_for_frame(const PfrScriptedInput* scripted_input,
                                  uint32_t frame);

#endif
