#include "pfr/audio.h"
#include "../../../include/constants/songs.h"
#include "gba/io_reg.h"
#include "pfr/stubs.h"
#include <math.h>

static double
pfr_audio_square(double phase)
{
  return phase < 3.14159265358979323846 ? 1.0 : -1.0;
}

static double
pfr_audio_title_frequency(uint32_t bgm_step, double ratio)
{
  static const double sTitlePattern[] = {
    392.00, 523.25, 659.25, 783.99, 659.25, 523.25, 440.00, 329.63,
  };

  return sTitlePattern[bgm_step % PFR_ARRAY_COUNT(sTitlePattern)] * ratio;
}

void
pfr_audio_reset(PfrAudioState* audio_state)
{
  audio_state->phase_a = 0.0;
  audio_state->phase_b = 0.0;
  audio_state->gain = 0.12;
  audio_state->sample_rate = PFR_DEFAULT_AUDIO_SAMPLE_RATE;
  audio_state->bgm_step = 0;
  audio_state->se_samples_remaining = 0;
  audio_state->cry_samples_remaining = 0;
}

void
pfr_audio_generate(PfrAudioState* audio_state,
                   int16_t* samples,
                   size_t sample_count,
                   uint16_t held_keys,
                   uint32_t frame_counter)
{
  const double two_pi = 6.28318530717958647692;
  double bgm_freq_a = 0.0;
  double bgm_freq_b = 0.0;
  double gain = 0.0;
  u16 se_song = 0;
  size_t i;

  (void)frame_counter;

  if (pfr_stub_is_bgm_playing()) {
    switch (pfr_stub_current_bgm()) {
      case MUS_TITLE:
        bgm_freq_a = pfr_audio_title_frequency(audio_state->bgm_step, 1.0);
        bgm_freq_b = pfr_audio_title_frequency(audio_state->bgm_step, 0.5);
        gain = 0.10;
        break;
      default:
        bgm_freq_a = 220.0;
        bgm_freq_b = 330.0;
        gain = 0.08;
        break;
    }
  }

  if (pfr_stub_take_se(&se_song)) {
    audio_state->se_samples_remaining = audio_state->sample_rate / 12;
  }

  if (pfr_stub_take_cry()) {
    audio_state->cry_samples_remaining = audio_state->sample_rate / 3;
  } else if (bgm_freq_a == 0.0) {
    if ((held_keys & A_BUTTON) != 0) {
      bgm_freq_a = 440.0;
      bgm_freq_b = 220.0;
      gain = 0.12;
    } else if ((held_keys & B_BUTTON) != 0) {
      bgm_freq_a = 523.25;
      bgm_freq_b = 261.63;
      gain = 0.12;
    } else if ((held_keys & START_BUTTON) != 0) {
      bgm_freq_a = 330.0;
      bgm_freq_b = 165.0;
      gain = 0.14;
    }
  }

  for (i = 0; i < sample_count; i++) {
    double value = 0.0;

    if (bgm_freq_a > 0.0) {
      value += sin(audio_state->phase_a) * gain;
      value += pfr_audio_square(audio_state->phase_b) * (gain * 0.35);
      audio_state->phase_a +=
        two_pi * bgm_freq_a / (double)audio_state->sample_rate;
      audio_state->phase_b +=
        two_pi * bgm_freq_b / (double)audio_state->sample_rate;

      if (audio_state->phase_a >= two_pi) {
        audio_state->phase_a -= two_pi;
      }

      if (audio_state->phase_b >= two_pi) {
        audio_state->phase_b -= two_pi;
      }
    }

    if (audio_state->se_samples_remaining > 0) {
      double se_phase = (double)audio_state->se_samples_remaining /
                        (double)audio_state->sample_rate;
      double se_freq = se_song == SE_SELECT ? 1174.66 : 880.0;

      value += sin(two_pi * se_freq * se_phase) * 0.22;
      audio_state->se_samples_remaining--;
    }

    if (audio_state->cry_samples_remaining > 0) {
      double cry_progress = (double)audio_state->cry_samples_remaining /
                            (double)audio_state->sample_rate;
      double cry_freq = 880.0 - cry_progress * 960.0;

      value += sin(audio_state->phase_a * 0.5) * 0.08;
      value += pfr_audio_square(two_pi * cry_freq * cry_progress) * 0.12;
      audio_state->cry_samples_remaining--;
    }

    if (value > 1.0) {
      value = 1.0;
    } else if (value < -1.0) {
      value = -1.0;
    }

    samples[i] = (int16_t)(value * 32767.0);
  }

  if (bgm_freq_a > 0.0) {
    audio_state->bgm_step++;
    if (audio_state->bgm_step >= 8U * 6U) {
      audio_state->bgm_step = 0;
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
