#ifndef PFR_AUDIO_H
#define PFR_AUDIO_H

#include "pfr/common.h"

typedef struct PfrAudioState
{
  double phase_a;
  double phase_b;
  double gain;
  int sample_rate;
  uint32_t bgm_step;
  int se_samples_remaining;
  int cry_samples_remaining;
} PfrAudioState;

void
pfr_audio_reset(PfrAudioState* audio_state);
void
pfr_audio_generate(PfrAudioState* audio_state,
                   int16_t* samples,
                   size_t sample_count,
                   uint16_t held_keys,
                   uint32_t frame_counter);
bool
pfr_audio_has_signal(const int16_t* samples, size_t sample_count);

#endif
