#ifndef PFR_COMMON_H
#define PFR_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
  PFR_SCREEN_WIDTH = 240,
  PFR_SCREEN_HEIGHT = 160,
  PFR_EWRAM_SIZE = 0x40000,
  PFR_IWRAM_SIZE = 0x8000,
  PFR_VRAM_SIZE = 0x18000,
  PFR_PLTT_SIZE = 0x400,
  PFR_OAM_SIZE = 0x400,
  PFR_IO_SIZE = 0x400,
  PFR_SAVE_SIZE = 128 * 1024,
  PFR_MAX_PATH = 1024,
  // Final host mix rate. This is intentionally higher than the original
  // DirectSound rate so PSG/CGB channels are not folded down into the same
  // low-rate output path as sampled audio.
  PFR_DEFAULT_AUDIO_SAMPLE_RATE = 32768,
  // Original m4a DirectSound rate selected by SOUND_MODE_FREQ_13379.
  PFR_DRIVER_PCM_SAMPLE_RATE = 13379,
  PFR_DRIVER_PCM_FRAMES_PER_VBLANK = 224,
  PFR_AUDIO_FRAMES_PER_GBA_FRAME = 549,
};

#define PFR_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

#endif
