#ifndef PFR_AUDIO_ASSETS_H
#define PFR_AUDIO_ASSETS_H

#include "gba/m4a_internal.h"
#include "gba/types.h"

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
  PFR_AUDIO_VOICE_DIRECTSOUND_NO_RESAMPLE = TONEDATA_TYPE_FIX,
  PFR_AUDIO_VOICE_KEYSPLIT = TONEDATA_TYPE_SPL,
  PFR_AUDIO_VOICE_RHYTHM = TONEDATA_TYPE_RHY,
};

typedef struct PfrAudioTrackRelocation
{
  u32 offset;
  u32 target_offset;
} PfrAudioTrackRelocation;

typedef struct PfrAudioTrackAsset
{
  const u8* data;
  u32 length;
  const PfrAudioTrackRelocation* relocations;
  u32 relocation_count;
} PfrAudioTrackAsset;

typedef struct PfrAudioSongAsset
{
  u16 song_id;
  u8 player_id;
  bool8 loop;
  u8 track_count;
  u8 priority;
  u8 reverb;
  const struct SongHeader* song_header;
  const PfrAudioTrackAsset* tracks;
} PfrAudioSongAsset;

extern const PfrAudioSongAsset gPfrAudioSongAssets[];
extern const u32 gPfrAudioSongAssetCount;
extern const u8 gPfrKeysplitBlob[];

extern const struct ToneData gCryTable[];
extern const struct ToneData gCryTable_Reverse[];

const PfrAudioSongAsset*
pfr_audio_song_asset_for_header(const struct SongHeader* song_header);
const PfrAudioSongAsset*
pfr_audio_song_asset_for_id(u16 song_id);

#endif
