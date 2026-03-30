#ifndef PFR_AUDIO_ASSETS_H
#define PFR_AUDIO_ASSETS_H

#include "gba/m4a_internal.h"
#include "gba/types.h"

typedef enum PfrAudioVoiceKind
{
  PFR_AUDIO_VOICE_DIRECTSOUND = 0,
  PFR_AUDIO_VOICE_DIRECTSOUND_NO_RESAMPLE = 1,
  PFR_AUDIO_VOICE_SQUARE1 = 2,
  PFR_AUDIO_VOICE_SQUARE2 = 3,
  PFR_AUDIO_VOICE_PROGRAMMABLE_WAVE = 4,
  PFR_AUDIO_VOICE_NOISE = 5,
  PFR_AUDIO_VOICE_KEYSPLIT = 6,
  PFR_AUDIO_VOICE_RHYTHM = 7,
} PfrAudioVoiceKind;

typedef struct PfrAudioSample
{
  const s8* samples;
  u32 sample_count;
  u32 sample_rate;
  u32 loop_start;
  bool8 loop_enabled;
  const struct WaveData* wave;
} PfrAudioSample;

typedef struct PfrAudioVoice
{
  u8 kind;
  u8 base_key;
  s8 pan;
  u8 attack;
  u8 decay;
  u8 sustain;
  u8 release;
  u8 duty_or_period;
  const PfrAudioSample* sample;
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

typedef struct PfrCryAsset
{
  const struct WaveData* wave;
  const PfrAudioSample* sample;
} PfrCryAsset;

extern const PfrAudioSongAsset gPfrAudioSongAssets[];
extern const u32 gPfrAudioSongAssetCount;

extern const PfrCryAsset gPfrAudioCryAssets[];
extern const u32 gPfrAudioCryAssetCount;

extern const struct ToneData gCryTable[];
extern const struct ToneData gCryTable_Reverse[];

const PfrAudioSongAsset*
pfr_audio_song_asset_for_header(const struct SongHeader* song_header);
const PfrAudioSongAsset*
pfr_audio_song_asset_for_id(u16 song_id);
const PfrCryAsset*
pfr_audio_cry_asset_for_wave(const struct WaveData* wave);

#endif
