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
  /* 65536 Hz keeps PSG/CGB aliasing closer to hardware behavior. */
  PFR_DEFAULT_AUDIO_SAMPLE_RATE = 65536,
  PFR_DRIVER_PCM_SAMPLE_RATE = 13379,
  PFR_DRIVER_PCM_FRAMES_PER_VBLANK = 224,
  PFR_AUDIO_FRAMES_PER_GBA_FRAME = 1097,
};

#define PFR_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

#endif
