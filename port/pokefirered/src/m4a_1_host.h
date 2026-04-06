#ifndef PFR_M4A_1_HOST_H
#define PFR_M4A_1_HOST_H

#include "gba/m4a_internal.h"

u8*
pfr_m4a_host_resolve_pointer(struct MusicPlayerInfo* mplayInfo,
                             struct MusicPlayerTrack* track);
const u8*
pfr_m4a_host_keysplit_table(const struct ToneData* tone);

#endif
