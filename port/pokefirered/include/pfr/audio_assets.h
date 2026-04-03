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

typedef enum PfrAudioVoiceKind
{
  PFR_AUDIO_VOICE_DIRECTSOUND = 0,
  PFR_AUDIO_VOICE_SQUARE1 = 1,
  PFR_AUDIO_VOICE_SQUARE2 = 2,
  PFR_AUDIO_VOICE_PROGRAMMABLE_WAVE = 3,
  PFR_AUDIO_VOICE_NOISE = 4,
  PFR_AUDIO_VOICE_DIRECTSOUND_NO_RESAMPLE = TONEDATA_TYPE_FIX,
  PFR_AUDIO_VOICE_KEYSPLIT = TONEDATA_TYPE_SPL,
  PFR_AUDIO_VOICE_RHYTHM = TONEDATA_TYPE_RHY,
} PfrAudioVoiceKind;

typedef struct PfrAudioVoice
{
  u8 kind;
  u8 key;
  u8 length;
  u8 pan_sweep;
  const void* wav;
  u8 attack;
  u8 decay;
  u8 sustain;
  u8 release;
  const struct PfrAudioVoice* subgroup;
  const u8* keysplit_table;
  u16 subgroup_count;
} PfrAudioVoice;

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
  const PfrAudioVoice* voicegroup;
  u16 voicegroup_count;
  const PfrAudioTrackAsset* tracks;
} PfrAudioSongAsset;

extern const PfrAudioSongAsset gPfrAudioSongAssets[];
extern const u32 gPfrAudioSongAssetCount;

extern const struct ToneData gCryTable[];
extern const struct ToneData gCryTable_Reverse[];

const PfrAudioSongAsset*
pfr_audio_song_asset_for_header(const struct SongHeader* song_header);
const PfrAudioSongAsset*
pfr_audio_song_asset_for_id(u16 song_id);

#endif
