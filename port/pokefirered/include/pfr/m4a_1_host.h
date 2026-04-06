#ifndef PFR_M4A_1_HOST_H
#define PFR_M4A_1_HOST_H

#include "gba/m4a_internal.h"

#ifndef TONEDATA_TYPE_REV
#define TONEDATA_TYPE_REV 0x10
#endif

#ifndef TONEDATA_TYPE_CMP
#define TONEDATA_TYPE_CMP 0x20
#endif

enum
{
  PFR_AUDIO_VOICE_DIRECTSOUND = 0,
  PFR_AUDIO_VOICE_SQUARE1 = 1,
  PFR_AUDIO_VOICE_SQUARE2 = 2,
  PFR_AUDIO_VOICE_PROGRAMMABLE_WAVE = 3,
  PFR_AUDIO_VOICE_NOISE = 4,
};

u8*
pfr_m4a_host_resolve_pointer(struct MusicPlayerInfo* mplayInfo,
                             struct MusicPlayerTrack* track);
const u8*
pfr_m4a_host_keysplit_table(const struct ToneData* tone);

#endif
