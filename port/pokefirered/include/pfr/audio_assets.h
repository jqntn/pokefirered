#ifndef PFR_AUDIO_ASSETS_H
#define PFR_AUDIO_ASSETS_H

#include "gba/m4a_internal.h"
#include "gba/types.h"

typedef struct PfrAudioClipAsset
{
  u16 song_id;
  u8 player_id;
  bool8 loop;
  u32 frame_count;
  const s8* samples;
} PfrAudioClipAsset;

typedef struct PfrCryAsset
{
  const struct WaveData* wave;
  u32 frame_count;
  const s8* samples;
} PfrCryAsset;

extern const PfrAudioClipAsset gPfrAudioSongAssets[];
extern const u32 gPfrAudioSongAssetCount;

extern const PfrCryAsset gPfrAudioCryAssets[];
extern const u32 gPfrAudioCryAssetCount;

extern const struct ToneData gCryTable[];
extern const struct ToneData gCryTable_Reverse[];

#endif
