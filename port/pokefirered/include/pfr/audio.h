#ifndef PFR_AUDIO_H
#define PFR_AUDIO_H

#include "pfr/common.h"

enum
{
  PFR_AUDIO_CHANNEL_COUNT = 2,
  PFR_AUDIO_QUEUE_CAPACITY = PFR_DEFAULT_AUDIO_SAMPLE_RATE * 12,
};

typedef struct PfrAudioStats
{
  uint32_t source_sample_rate;
  size_t queued_frames;
  uint64_t underrun_count;
  uint64_t overrun_count;
} PfrAudioStats;

void
pfr_audio_reset(void);
void
pfr_audio_shutdown(void);
uint32_t
pfr_audio_source_sample_rate(void);
size_t
pfr_audio_available_frames(void);
void
pfr_audio_queue_source_frames(const int16_t* samples, size_t frame_count);
size_t
pfr_audio_drain_source_frames(int16_t* output, size_t frame_count);
size_t
pfr_audio_drain_resampled_frames(int16_t* output,
                                 size_t frame_count,
                                 uint32_t output_sample_rate);
PfrAudioStats
pfr_audio_stats(void);

#endif
