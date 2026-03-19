#include "gba/io_reg.h"
#include <math.h>
#include "pfr/audio.h"

void
pfr_audio_reset(PfrAudioState* audio_state)
{
  audio_state->phase = 0.0;
  audio_state->gain = 0.12;
  audio_state->sample_rate = PFR_DEFAULT_AUDIO_SAMPLE_RATE;
}

void
pfr_audio_generate(PfrAudioState* audio_state,
                   int16_t* samples,
                   size_t sample_count,
                   uint16_t held_keys,
                   uint32_t frame_counter)
{
  const double two_pi = 6.28318530717958647692;
  double frequency = 220.0 + (double)(frame_counter & 7U) * 7.5;
  double gain = audio_state->gain;
  size_t i;

  if ((held_keys & A_BUTTON) != 0) {
    frequency = 440.0;
    gain = 0.18;
  } else if ((held_keys & B_BUTTON) != 0) {
    frequency = 523.25;
    gain = 0.18;
  } else if ((held_keys & START_BUTTON) != 0) {
    frequency = 330.0;
    gain = 0.2;
  }

  for (i = 0; i < sample_count; i++) {
    double value = sin(audio_state->phase) * gain;

    samples[i] = (int16_t)(value * 32767.0);
    audio_state->phase += two_pi * frequency / (double)audio_state->sample_rate;

    if (audio_state->phase >= two_pi) {
      audio_state->phase -= two_pi;
    }
  }
}

bool
pfr_audio_has_signal(const int16_t* samples, size_t sample_count)
{
  size_t i;

  for (i = 0; i < sample_count; i++) {
    if (samples[i] != 0) {
      return true;
    }
  }

  return false;
}
