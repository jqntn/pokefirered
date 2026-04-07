#include "pfr/audio.h"

#include <string.h>

typedef struct PfrAudioQueue
{
  int16_t frames[PFR_AUDIO_QUEUE_CAPACITY * PFR_AUDIO_CHANNEL_COUNT];
  size_t read_index;
  size_t frame_count;
  uint64_t underrun_count;
  uint64_t overrun_count;
  uint32_t source_sample_rate;
  double resample_position;
} PfrAudioQueue;

static PfrAudioQueue sPfrAudioQueue;

static size_t
pfr_audio_frame_offset(size_t index)
{
  return (index % PFR_AUDIO_QUEUE_CAPACITY) * PFR_AUDIO_CHANNEL_COUNT;
}

static size_t
pfr_audio_copy_frames(int16_t* output,
                      size_t frame_count,
                      size_t queue_index,
                      size_t available_frames)
{
  size_t frames_before_wrap =
    PFR_AUDIO_QUEUE_CAPACITY - (queue_index % PFR_AUDIO_QUEUE_CAPACITY);
  size_t first_copy = frame_count;

  if (first_copy > available_frames) {
    first_copy = available_frames;
  }

  if (first_copy > frames_before_wrap) {
    first_copy = frames_before_wrap;
  }

  memcpy(output,
         &sPfrAudioQueue.frames[pfr_audio_frame_offset(queue_index)],
         first_copy * PFR_AUDIO_CHANNEL_COUNT * sizeof(int16_t));

  if (frame_count > first_copy) {
    size_t remaining = frame_count - first_copy;

    memcpy(output + first_copy * PFR_AUDIO_CHANNEL_COUNT,
           &sPfrAudioQueue.frames[0],
           remaining * PFR_AUDIO_CHANNEL_COUNT * sizeof(int16_t));
  }

  return first_copy;
}

void
pfr_audio_reset(void)
{
  memset(&sPfrAudioQueue, 0, sizeof(sPfrAudioQueue));
  sPfrAudioQueue.source_sample_rate = PFR_DEFAULT_AUDIO_SAMPLE_RATE;
}

void
pfr_audio_shutdown(void)
{
}

uint32_t
pfr_audio_source_sample_rate(void)
{
  return sPfrAudioQueue.source_sample_rate;
}

size_t
pfr_audio_available_frames(void)
{
  return sPfrAudioQueue.frame_count;
}

void
pfr_audio_queue_source_frames(const int16_t* samples, size_t frame_count)
{
  size_t i;

  if (samples == NULL || frame_count == 0) {
    return;
  }

  for (i = 0; i < frame_count; i++) {
    size_t write_index;
    size_t frame_offset;

    if (sPfrAudioQueue.frame_count == PFR_AUDIO_QUEUE_CAPACITY) {
      sPfrAudioQueue.read_index =
        (sPfrAudioQueue.read_index + 1) % PFR_AUDIO_QUEUE_CAPACITY;
      sPfrAudioQueue.frame_count--;
      sPfrAudioQueue.overrun_count++;
    }

    write_index = (sPfrAudioQueue.read_index + sPfrAudioQueue.frame_count) %
                  PFR_AUDIO_QUEUE_CAPACITY;
    frame_offset = pfr_audio_frame_offset(write_index);
    sPfrAudioQueue.frames[frame_offset + 0] =
      samples[i * PFR_AUDIO_CHANNEL_COUNT + 0];
    sPfrAudioQueue.frames[frame_offset + 1] =
      samples[i * PFR_AUDIO_CHANNEL_COUNT + 1];
    sPfrAudioQueue.frame_count++;
  }
}

size_t
pfr_audio_drain_source_frames(int16_t* output, size_t frame_count)
{
  size_t available = sPfrAudioQueue.frame_count;
  size_t to_copy = frame_count;

  if (to_copy > available) {
    to_copy = available;
  }

  if (output != NULL && to_copy != 0) {
    pfr_audio_copy_frames(
      output, to_copy, sPfrAudioQueue.read_index, sPfrAudioQueue.frame_count);
  }

  sPfrAudioQueue.read_index =
    (sPfrAudioQueue.read_index + to_copy) % PFR_AUDIO_QUEUE_CAPACITY;
  sPfrAudioQueue.frame_count -= to_copy;

  if (to_copy < frame_count) {
    size_t missing = frame_count - to_copy;

    if (output != NULL) {
      memset(output + to_copy * PFR_AUDIO_CHANNEL_COUNT,
             0,
             missing * PFR_AUDIO_CHANNEL_COUNT * sizeof(int16_t));
    }

    sPfrAudioQueue.underrun_count++;
  }

  return to_copy;
}

size_t
pfr_audio_drain_resampled_frames(int16_t* output,
                                 size_t frame_count,
                                 uint32_t output_sample_rate)
{
  size_t frame_index;

  if (output == NULL || frame_count == 0) {
    return 0;
  }

  if (output_sample_rate == 0 ||
      output_sample_rate == sPfrAudioQueue.source_sample_rate) {
    return pfr_audio_drain_source_frames(output, frame_count);
  }

  for (frame_index = 0; frame_index < frame_count; frame_index++) {
    size_t base_frame = (size_t)sPfrAudioQueue.resample_position;
    double fraction = sPfrAudioQueue.resample_position - (double)base_frame;
    int16_t source0[2] = { 0, 0 };
    int16_t source1[2] = { 0, 0 };

    if (base_frame < sPfrAudioQueue.frame_count) {
      size_t source_index =
        (sPfrAudioQueue.read_index + base_frame) % PFR_AUDIO_QUEUE_CAPACITY;
      size_t source_offset = pfr_audio_frame_offset(source_index);

      source0[0] = sPfrAudioQueue.frames[source_offset + 0];
      source0[1] = sPfrAudioQueue.frames[source_offset + 1];
    }

    if (base_frame + 1 < sPfrAudioQueue.frame_count) {
      size_t source_index =
        (sPfrAudioQueue.read_index + base_frame + 1) % PFR_AUDIO_QUEUE_CAPACITY;
      size_t source_offset = pfr_audio_frame_offset(source_index);

      source1[0] = sPfrAudioQueue.frames[source_offset + 0];
      source1[1] = sPfrAudioQueue.frames[source_offset + 1];
    } else {
      source1[0] = source0[0];
      source1[1] = source0[1];
    }

    output[frame_index * 2 + 0] =
      (int16_t)(source0[0] + (double)(source1[0] - source0[0]) * fraction);
    output[frame_index * 2 + 1] =
      (int16_t)(source0[1] + (double)(source1[1] - source0[1]) * fraction);

    sPfrAudioQueue.resample_position +=
      (double)sPfrAudioQueue.source_sample_rate / (double)output_sample_rate;
  }

  if (sPfrAudioQueue.frame_count != 0) {
    size_t consumed = (size_t)sPfrAudioQueue.resample_position;

    if (consumed > sPfrAudioQueue.frame_count) {
      consumed = sPfrAudioQueue.frame_count;
    }

    sPfrAudioQueue.read_index =
      (sPfrAudioQueue.read_index + consumed) % PFR_AUDIO_QUEUE_CAPACITY;
    sPfrAudioQueue.frame_count -= consumed;
    sPfrAudioQueue.resample_position -= (double)consumed;
  } else {
    sPfrAudioQueue.resample_position = 0.0;
    sPfrAudioQueue.underrun_count++;
  }

  return frame_count;
}

PfrAudioStats
pfr_audio_stats(void)
{
  PfrAudioStats stats;

  stats.source_sample_rate = sPfrAudioQueue.source_sample_rate;
  stats.queued_frames = sPfrAudioQueue.frame_count;
  stats.underrun_count = sPfrAudioQueue.underrun_count;
  stats.overrun_count = sPfrAudioQueue.overrun_count;
  return stats;
}
