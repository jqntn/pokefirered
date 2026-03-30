#include "pfr/audio.h"
#include "pfr/audio_assets.h"
#include "pfr/core.h"
#include "pfr/stubs.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "constants/global.h"
#include "constants/songs.h"
#include "constants/sound.h"
#include "gba/m4a_internal.h"
#include "global.h"
#include "m4a.h"
#include "main.h"

enum
{
  PFR_AUDIO_PLAYER_COUNT = 4,
  PFR_AUDIO_FRAMES_PER_VBLANK = 224,
  PFR_AUDIO_FULL_VOLUME = 256,
  PFR_AUDIO_MAX_PAN = 64,
  PFR_AUDIO_FADE_STEP = 16,
  PFR_CRY_DEFAULT_PITCH = 15360,
  PFR_CRY_DEFAULT_LENGTH = 140,
  PFR_TRACK_RUNTIME_STACK_DEPTH = 3,
};

typedef struct PfrRuntimeNote
{
  const PfrAudioVoice* voice;
  const PfrAudioSample* sample;
  double source_pos;
  double source_step;
  double phase;
  double gain;
  int pan;
  int remaining_ticks;
  int release_frames_total;
  int release_frames_left;
  float held_sample;
  uint32_t noise_state;
  bool active;
  bool released;
} PfrRuntimeNote;

typedef struct PfrTrackRuntime
{
  struct MusicPlayerTrack* track;
  const PfrAudioTrackAsset* asset;
  PfrRuntimeNote note;
} PfrTrackRuntime;

typedef struct PfrSongPlayer
{
  struct MusicPlayerInfo* info;
  struct MusicPlayerTrack* tracks;
  uint8_t track_capacity;
  const PfrAudioSongAsset* asset;
  PfrTrackRuntime runtime[MAX_MUSICPLAYER_TRACKS];
  double frames_until_tick;
  uint16_t volume;
  uint16_t fade_level;
  uint16_t fade_interval;
  uint16_t fade_counter;
  bool paused;
  bool stopped;
  bool temporary_fade;
  bool fade_in;
} PfrSongPlayer;

typedef struct PfrCryRuntime
{
  const PfrCryAsset* asset;
  double frame_position;
  double frame_step;
  uint32_t frame_limit;
  uint32_t frames_played;
  uint8_t volume;
  uint16_t length;
  uint8_t release;
  uint8_t priority;
  int8_t pan;
  int8_t chorus;
  bool reverse;
  bool active;
} PfrCryRuntime;

static const uint8_t sPfrClockTable[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1C,
  0x1E, 0x20, 0x24, 0x28, 0x2A, 0x2C, 0x30, 0x34, 0x36, 0x38, 0x3C, 0x40, 0x42,
  0x44, 0x48, 0x4C, 0x4E, 0x50, 0x54, 0x58, 0x5A, 0x5C, 0x60,
};

static struct MusicPlayerTrack sMPlayTrackBgm[10];
static struct MusicPlayerTrack sMPlayTrackSe1[3];
static struct MusicPlayerTrack sMPlayTrackSe2[9];
static struct MusicPlayerTrack sMPlayTrackSe3[1];

struct MusicPlayerInfo gMPlayInfo_BGM = { 0 };
struct MusicPlayerInfo gMPlayInfo_SE1 = { 0 };
struct MusicPlayerInfo gMPlayInfo_SE2 = { 0 };
struct MusicPlayerInfo gMPlayInfo_SE3 = { 0 };
struct SoundInfo gSoundInfo = { 0 };
u8 gMPlayMemAccArea[0x10] = { 0 };
MPlayFunc gMPlayJumpTable[36] = { 0 };
struct PokemonCrySong gPokemonCrySong = { 0 };
struct PokemonCrySong gPokemonCrySongs[MAX_POKEMON_CRIES] = { 0 };
struct MusicPlayerInfo gPokemonCryMusicPlayers[MAX_POKEMON_CRIES] = { 0 };
struct MusicPlayerTrack gPokemonCryTracks[MAX_POKEMON_CRIES * 2] = { 0 };
const struct PokemonCrySong gPokemonCrySongTemplate = { 1,
                                                        0,
                                                        255,
                                                        0,
                                                        NULL,
                                                        { NULL, NULL },
                                                        0,
                                                        0xC8,
                                                        C_V,
                                                        0xB2,
                                                        0,
                                                        0xC8,
                                                        (u8)(C_V + 16),
                                                        { 0xBD, 0 },
                                                        0xBE,
                                                        127,
                                                        { 0xCD, 0x0D },
                                                        0,
                                                        { 0xCD, 0x07 },
                                                        0,
                                                        0xBF,
                                                        C_V,
                                                        0xCF,
                                                        60,
                                                        127,
                                                        { 0xCD, 0x0C },
                                                        60,
                                                        { 0xCE, 0xB1 } };
char SoundMainRAM[1] = { 0 };
char gNumMusicPlayers[1] = { 4 };
char gMaxLines[1] = { 5 };
u8 gDisableMapMusicChangeOnMapLoad = 0;
u8 gDisableHelpSystemVolumeReduce = 0;
u32 gBattleTypeFlags = 0;

const struct MusicPlayer gMPlayTable[] = {
  { &gMPlayInfo_BGM, sMPlayTrackBgm, 10, 0 },
  { &gMPlayInfo_SE1, sMPlayTrackSe1, 3, 1 },
  { &gMPlayInfo_SE2, sMPlayTrackSe2, 9, 1 },
  { &gMPlayInfo_SE3, sMPlayTrackSe3, 1, 0 },
};

static PfrSongPlayer sSongPlayers[PFR_AUDIO_PLAYER_COUNT] = {
  { &gMPlayInfo_BGM,
    sMPlayTrackBgm,
    10,
    NULL,
    { 0 },
    0.0,
    PFR_AUDIO_FULL_VOLUME,
    PFR_AUDIO_FULL_VOLUME,
    0,
    0,
    false,
    true,
    false,
    false },
  { &gMPlayInfo_SE1,
    sMPlayTrackSe1,
    3,
    NULL,
    { 0 },
    0.0,
    PFR_AUDIO_FULL_VOLUME,
    PFR_AUDIO_FULL_VOLUME,
    0,
    0,
    false,
    true,
    false,
    false },
  { &gMPlayInfo_SE2,
    sMPlayTrackSe2,
    9,
    NULL,
    { 0 },
    0.0,
    PFR_AUDIO_FULL_VOLUME,
    PFR_AUDIO_FULL_VOLUME,
    0,
    0,
    false,
    true,
    false,
    false },
  { &gMPlayInfo_SE3,
    sMPlayTrackSe3,
    1,
    NULL,
    { 0 },
    0.0,
    PFR_AUDIO_FULL_VOLUME,
    PFR_AUDIO_FULL_VOLUME,
    0,
    0,
    false,
    true,
    false,
    false },
};

static PfrCryRuntime sCryRuntime;
static bool8 sVSyncEnabled;
static bool8 sCryStereo;
static uint16_t sCurrentBgmSongId;
static uint16_t sPendingSeSongId;
static bool8 sPendingSe;
static bool8 sPendingCry;
static uint16_t sCryPitch;
static uint16_t sCryLength;
static uint8_t sCryRelease;
static uint8_t sCryVolume;
static uint8_t sCryPriority;
static int8_t sCryPan;
static int8_t sCryChorus;

static int
pfr_audio_runtime_clamp_pan(int pan)
{
  if (pan < -PFR_AUDIO_MAX_PAN) {
    return -PFR_AUDIO_MAX_PAN;
  }

  if (pan > PFR_AUDIO_MAX_PAN - 1) {
    return PFR_AUDIO_MAX_PAN - 1;
  }

  return pan;
}

static int16_t
pfr_audio_runtime_clamp_sample(int32_t sample)
{
  if (sample < -32768) {
    return -32768;
  }

  if (sample > 32767) {
    return 32767;
  }

  return (int16_t)sample;
}

static void
pfr_audio_runtime_pan_gains(int pan, double gain, double* left, double* right)
{
  int clamped = pfr_audio_runtime_clamp_pan(pan);

  *right = ((double)(clamped + 64) / 127.0) * gain;
  *left = ((double)(63 - clamped) / 127.0) * gain;
}

static void
pfr_audio_runtime_clear_note(PfrRuntimeNote* note)
{
  memset(note, 0, sizeof(*note));
}

static void
pfr_audio_runtime_release_note(PfrRuntimeNote* note)
{
  note->released = true;
}

static void
pfr_audio_runtime_stop_track_runtime(PfrTrackRuntime* runtime)
{
  if (runtime->track != NULL) {
    memset(runtime->track, 0, sizeof(*runtime->track));
  }
  pfr_audio_runtime_clear_note(&runtime->note);
}

static void
pfr_audio_runtime_refresh_player(PfrSongPlayer* player)
{
  uint32_t status = 0;
  uint8_t track_count = 0;

  player->info->ident = ID_NUMBER;
  player->info->songHeader =
    (struct SongHeader*)(player->asset != NULL ? player->asset->song_header
                                               : NULL);
  player->info->fadeOI = player->fade_interval;
  player->info->fadeOC = player->fade_counter;
  player->info->fadeOV = player->fade_level;
  player->info->tracks = player->tracks;
  player->info->memAccArea = gMPlayMemAccArea;
  player->info->tone = NULL;

  if (player->asset != NULL) {
    track_count = player->asset->track_count;
    player->info->priority = player->asset->priority;
  } else {
    player->info->priority = 0;
  }

  player->info->trackCount = track_count;

  if (player->asset != NULL && !player->stopped) {
    status |= MUSICPLAYER_STATUS_TRACK;
  }

  if (player->paused) {
    status |= MUSICPLAYER_STATUS_PAUSE;
  }

  player->info->status = status;
}

static void
pfr_audio_runtime_full_stop_player(PfrSongPlayer* player)
{
  size_t i;

  player->paused = false;
  player->stopped = true;

  for (i = 0; i < player->track_capacity; i++) {
    pfr_audio_runtime_clear_note(&player->runtime[i].note);
  }

  pfr_audio_runtime_refresh_player(player);
}

static void
pfr_audio_runtime_pause_player(PfrSongPlayer* player)
{
  size_t i;

  player->paused = true;
  player->stopped = false;

  for (i = 0; i < player->track_capacity; i++) {
    pfr_audio_runtime_clear_note(&player->runtime[i].note);
  }

  pfr_audio_runtime_refresh_player(player);
}

static void
pfr_audio_runtime_drop_player(PfrSongPlayer* player)
{
  size_t i;

  player->asset = NULL;
  player->paused = false;
  player->stopped = true;
  player->frames_until_tick = 0.0;
  player->volume = PFR_AUDIO_FULL_VOLUME;
  player->fade_level = PFR_AUDIO_FULL_VOLUME;
  player->fade_interval = 0;
  player->fade_counter = 0;
  player->temporary_fade = false;
  player->fade_in = false;

  for (i = 0; i < player->track_capacity; i++) {
    player->runtime[i].track = &player->tracks[i];
    player->runtime[i].asset = NULL;
    pfr_audio_runtime_stop_track_runtime(&player->runtime[i]);
  }

  pfr_audio_runtime_refresh_player(player);
}

static PfrSongPlayer*
pfr_audio_runtime_player_from_info(struct MusicPlayerInfo* info)
{
  size_t i;

  for (i = 0; i < PFR_ARRAY_COUNT(sSongPlayers); i++) {
    if (sSongPlayers[i].info == info) {
      return &sSongPlayers[i];
    }
  }

  return NULL;
}

const PfrAudioSongAsset*
pfr_audio_song_asset_for_header(const struct SongHeader* song_header)
{
  size_t i;

  for (i = 0; i < gPfrAudioSongAssetCount; i++) {
    if (gPfrAudioSongAssets[i].song_header == song_header) {
      return &gPfrAudioSongAssets[i];
    }
  }

  return NULL;
}

const PfrAudioSongAsset*
pfr_audio_song_asset_for_id(u16 song_id)
{
  size_t i;

  for (i = 0; i < gPfrAudioSongAssetCount; i++) {
    if (gPfrAudioSongAssets[i].song_id == song_id) {
      return &gPfrAudioSongAssets[i];
    }
  }

  return NULL;
}

const PfrCryAsset*
pfr_audio_cry_asset_for_wave(const struct WaveData* wave)
{
  size_t i;

  for (i = 0; i < gPfrAudioCryAssetCount; i++) {
    if (gPfrAudioCryAssets[i].wave == wave) {
      return &gPfrAudioCryAssets[i];
    }
  }

  return NULL;
}

static void
pfr_audio_runtime_reset_state(void)
{
  size_t i;

  memset(&gSoundInfo, 0, sizeof(gSoundInfo));
  memset(&gPokemonCrySong, 0, sizeof(gPokemonCrySong));
  memset(gPokemonCrySongs, 0, sizeof(gPokemonCrySongs));
  memset(gPokemonCryMusicPlayers, 0, sizeof(gPokemonCryMusicPlayers));
  memset(gPokemonCryTracks, 0, sizeof(gPokemonCryTracks));

  for (i = 0; i < PFR_ARRAY_COUNT(sSongPlayers); i++) {
    pfr_audio_runtime_drop_player(&sSongPlayers[i]);
  }

  memset(&sCryRuntime, 0, sizeof(sCryRuntime));
  sVSyncEnabled = FALSE;
  sCryStereo = FALSE;
  sCurrentBgmSongId = 0;
  sPendingSeSongId = 0;
  sPendingSe = FALSE;
  sPendingCry = FALSE;
  sCryPitch = PFR_CRY_DEFAULT_PITCH;
  sCryLength = PFR_CRY_DEFAULT_LENGTH;
  sCryRelease = 0;
  sCryVolume = CRY_VOLUME;
  sCryPriority = CRY_PRIORITY_NORMAL;
  sCryPan = 0;
  sCryChorus = 0;
}

static double
pfr_audio_runtime_note_frequency(int midi_note, int bend, int bend_range)
{
  double semitone = (double)midi_note + ((double)bend / 64.0) * bend_range;

  return 440.0 * pow(2.0, (semitone - 69.0) / 12.0);
}

static const PfrAudioVoice*
pfr_audio_runtime_resolve_voice_group(const PfrAudioVoice* group,
                                      uint16_t group_count,
                                      int voice_index,
                                      int pitch)
{
  const PfrAudioVoice* voice;

  if (group == NULL || group_count == 0) {
    return NULL;
  }

  if (voice_index < 0) {
    voice_index = 0;
  } else if ((uint16_t)voice_index >= group_count) {
    voice_index = group_count - 1;
  }

  voice = &group[voice_index];

  while (voice->kind == PFR_AUDIO_VOICE_KEYSPLIT ||
         voice->kind == PFR_AUDIO_VOICE_RHYTHM) {
    int sub_index = 0;

    if (voice->subgroup == NULL || voice->subgroup_count == 0) {
      return NULL;
    }

    if (voice->kind == PFR_AUDIO_VOICE_KEYSPLIT) {
      if (voice->keysplit_table == NULL) {
        return NULL;
      }

      if (pitch < 0) {
        pitch = 0;
      } else if (pitch > 127) {
        pitch = 127;
      }

      sub_index = voice->keysplit_table[pitch];
    } else {
      sub_index = pitch - 36;
      if (sub_index < 0) {
        sub_index = 0;
      } else if ((uint16_t)sub_index >= voice->subgroup_count) {
        sub_index = voice->subgroup_count - 1;
      }
    }

    voice = &voice->subgroup[sub_index];
  }

  return voice;
}

static void
pfr_audio_runtime_note_from_voice(PfrRuntimeNote* note,
                                  const PfrAudioVoice* voice,
                                  const struct MusicPlayerTrack* track,
                                  uint16_t player_volume,
                                  uint16_t fade_level,
                                  uint8_t pitch,
                                  uint8_t velocity)
{
  double voice_gain;
  int pan;

  pfr_audio_runtime_clear_note(note);
  if (voice == NULL) {
    return;
  }

  voice_gain = ((double)track->vol / 127.0) * ((double)velocity / 127.0) *
               ((double)player_volume / (double)PFR_AUDIO_FULL_VOLUME) *
               ((double)fade_level / (double)PFR_AUDIO_FULL_VOLUME) * 0.65;
  pan = track->pan + track->panX + voice->pan;

  note->voice = voice;
  note->sample = voice->sample;
  note->gain = voice_gain;
  note->pan = pan;
  note->release_frames_total = voice->release == 0 ? 16 : voice->release * 16;
  note->release_frames_left = note->release_frames_total;
  note->noise_state = 0x12345678u + pitch;
  note->active = true;

  switch (voice->kind) {
    case PFR_AUDIO_VOICE_DIRECTSOUND:
    case PFR_AUDIO_VOICE_DIRECTSOUND_NO_RESAMPLE:
      if (voice->sample == NULL || voice->sample->sample_rate == 0) {
        note->active = false;
        return;
      }

      if (voice->kind == PFR_AUDIO_VOICE_DIRECTSOUND_NO_RESAMPLE) {
        note->source_step =
          (double)voice->sample->sample_rate / PFR_DEFAULT_AUDIO_SAMPLE_RATE;
      } else {
        int midi_key = pitch + track->keyShift + track->keyShiftX;
        int bend = track->bend + track->pitX / 256;
        double ratio =
          pfr_audio_runtime_note_frequency(midi_key, bend, track->bendRange) /
          pfr_audio_runtime_note_frequency(voice->base_key, 0, 2);

        note->source_step = ratio * (double)voice->sample->sample_rate /
                            PFR_DEFAULT_AUDIO_SAMPLE_RATE;
      }
      break;
    case PFR_AUDIO_VOICE_SQUARE1:
    case PFR_AUDIO_VOICE_SQUARE2:
    case PFR_AUDIO_VOICE_NOISE:
      note->source_step =
        pfr_audio_runtime_note_frequency(
          pitch + track->keyShift, track->bend, track->bendRange) /
        PFR_DEFAULT_AUDIO_SAMPLE_RATE;
      break;
    case PFR_AUDIO_VOICE_PROGRAMMABLE_WAVE:
      if (voice->sample == NULL || voice->sample->sample_count == 0) {
        note->active = false;
        return;
      }

      note->source_step =
        pfr_audio_runtime_note_frequency(
          pitch + track->keyShift, track->bend, track->bendRange) *
        (double)voice->sample->sample_count / PFR_DEFAULT_AUDIO_SAMPLE_RATE;
      break;
    default:
      note->active = false;
      return;
  }
}

static void
pfr_audio_runtime_start_note(PfrSongPlayer* player,
                             PfrTrackRuntime* runtime,
                             uint8_t note_cmd)
{
  struct MusicPlayerTrack* track = runtime->track;
  const uint8_t* cmd = track->cmdPtr;
  uint8_t duration = 0;

  if (note_cmd != 0xCF) {
    duration = sPfrClockTable[note_cmd - 0xCF];
  }

  track->gateTime = duration;

  if (cmd[0] < 0x80) {
    track->key = cmd[0];
    cmd++;
  }

  if (cmd[0] < 0x80) {
    track->velocity = cmd[0];
    cmd++;
  }

  if (cmd[0] < 0x80) {
    track->gateTime = (uint8_t)(track->gateTime + cmd[0]);
    cmd++;
  }

  track->cmdPtr = (u8*)cmd;

  if (runtime->note.active && !runtime->note.released) {
    pfr_audio_runtime_release_note(&runtime->note);
  }

  pfr_audio_runtime_note_from_voice(
    &runtime->note,
    pfr_audio_runtime_resolve_voice_group(player->asset->voicegroup,
                                          player->asset->voicegroup_count,
                                          track->tone.key,
                                          track->key),
    track,
    player->volume,
    player->fade_level,
    track->key,
    track->velocity);

  runtime->note.remaining_ticks = note_cmd == 0xCF ? -1 : duration;
}

static const PfrAudioTrackRelocation*
pfr_audio_runtime_find_relocation(const PfrAudioTrackAsset* asset,
                                  size_t offset)
{
  uint32_t i;

  for (i = 0; i < asset->relocation_count; i++) {
    if (asset->relocations[i].offset == offset) {
      return &asset->relocations[i];
    }
  }

  return NULL;
}

static const uint8_t*
pfr_audio_runtime_resolve_target(const PfrAudioTrackAsset* asset,
                                 const uint8_t* operand_ptr)
{
  size_t offset = (size_t)(operand_ptr - asset->data);
  const PfrAudioTrackRelocation* relocation =
    pfr_audio_runtime_find_relocation(asset, offset);

  if (relocation == NULL) {
    return asset->data;
  }

  return asset->data + relocation->target_offset;
}

static void
pfr_audio_runtime_step_track_command(PfrSongPlayer* player,
                                     PfrTrackRuntime* runtime)
{
  struct MusicPlayerTrack* track = runtime->track;
  uint8_t status = *track->cmdPtr;

  if (status < 0x80) {
    status = track->runningStatus;
  } else {
    track->cmdPtr++;
    if (status >= 0xBD) {
      track->runningStatus = status;
    }
  }

  if (status >= 0xCF) {
    pfr_audio_runtime_start_note(player, runtime, status);
    return;
  }

  if (status <= 0xB0) {
    track->wait = sPfrClockTable[status - 0x80];
    return;
  }

  switch (status) {
    case 0xB1:
      track->flags = 0;
      pfr_audio_runtime_release_note(&runtime->note);
      break;
    case 0xB2:
      track->cmdPtr =
        (u8*)pfr_audio_runtime_resolve_target(runtime->asset, track->cmdPtr);
      break;
    case 0xB3:
      if (track->patternLevel < PFR_TRACK_RUNTIME_STACK_DEPTH) {
        track->patternStack[track->patternLevel++] = track->cmdPtr + 4;
      }

      track->cmdPtr =
        (u8*)pfr_audio_runtime_resolve_target(runtime->asset, track->cmdPtr);
      break;
    case 0xB4:
      if (track->patternLevel != 0) {
        track->cmdPtr = track->patternStack[--track->patternLevel];
      }
      break;
    case 0xBB:
      player->info->tempoD = *track->cmdPtr++;
      break;
    case 0xBC:
      track->keyShift = (s8)*track->cmdPtr++;
      break;
    case 0xBD:
      track->tone.key = *track->cmdPtr++;
      break;
    case 0xBE:
      track->vol = *track->cmdPtr++;
      break;
    case 0xBF:
      track->pan = (s8)(*track->cmdPtr++ - C_V);
      break;
    case 0xC0:
      track->bend = (s8)(*track->cmdPtr++ - C_V);
      break;
    case 0xC1:
      track->bendRange = *track->cmdPtr++;
      break;
    case 0xC2:
      track->lfoSpeed = *track->cmdPtr++;
      break;
    case 0xC3:
      track->lfoDelay = *track->cmdPtr;
      track->lfoDelayC = *track->cmdPtr++;
      break;
    case 0xC4:
      track->mod = *track->cmdPtr++;
      break;
    case 0xC5:
      track->modT = *track->cmdPtr++;
      break;
    case 0xC8:
      track->tune = (s8)(*track->cmdPtr++ - C_V);
      break;
    case 0xCD: {
      uint8_t xcmd = *track->cmdPtr++;

      switch (xcmd) {
        case 0x07:
          track->tone.release = *track->cmdPtr++;
          break;
        case 0x08:
          track->pseudoEchoVolume = *track->cmdPtr++;
          break;
        case 0x09:
          track->pseudoEchoLength = *track->cmdPtr++;
          break;
        case 0x0C:
          track->cmdPtr += 2;
          break;
        case 0x0D:
          track->cmdPtr += 4;
          break;
        default:
          break;
      }
      break;
    }
    case 0xCE:
      pfr_audio_runtime_release_note(&runtime->note);
      break;
    default:
      break;
  }
}

static void
pfr_audio_runtime_update_fade(PfrSongPlayer* player)
{
  if (player->asset == NULL || player->fade_interval == 0) {
    return;
  }

  if (player->fade_counter > 0) {
    player->fade_counter--;
  }

  if (player->fade_counter != 0) {
    return;
  }

  player->fade_counter = player->fade_interval;

  if (player->fade_in) {
    if (player->fade_level + PFR_AUDIO_FADE_STEP >= PFR_AUDIO_FULL_VOLUME) {
      player->fade_level = PFR_AUDIO_FULL_VOLUME;
      player->fade_interval = 0;
      player->fade_counter = 0;
      player->fade_in = false;
    } else {
      player->fade_level = (uint16_t)(player->fade_level + PFR_AUDIO_FADE_STEP);
    }
  } else if (player->fade_level <= PFR_AUDIO_FADE_STEP) {
    player->fade_level = 0;
    player->fade_interval = 0;
    player->fade_counter = 0;

    if (player->temporary_fade) {
      player->paused = true;
    } else {
      player->stopped = true;
    }

    player->temporary_fade = false;
    player->fade_in = false;
  } else {
    player->fade_level = (uint16_t)(player->fade_level - PFR_AUDIO_FADE_STEP);
  }

  pfr_audio_runtime_refresh_player(player);
}

static void
pfr_audio_runtime_tick_player(PfrSongPlayer* player)
{
  uint8_t i;
  bool any_track_active = false;

  if (player->asset == NULL || player->paused || player->stopped) {
    return;
  }

  pfr_audio_runtime_update_fade(player);

  for (i = 0; i < player->asset->track_count; i++) {
    PfrTrackRuntime* runtime = &player->runtime[i];
    struct MusicPlayerTrack* track = runtime->track;

    if ((track->flags & MPT_FLG_EXIST) == 0) {
      continue;
    }

    while ((track->flags & MPT_FLG_EXIST) != 0 && track->wait == 0) {
      pfr_audio_runtime_step_track_command(player, runtime);
    }

    if (track->wait > 0) {
      track->wait--;
    }

    if (runtime->note.active && runtime->note.remaining_ticks > 0) {
      runtime->note.remaining_ticks--;
      if (runtime->note.remaining_ticks == 0) {
        pfr_audio_runtime_release_note(&runtime->note);
      }
    }

    if ((track->flags & MPT_FLG_EXIST) != 0 || runtime->note.active) {
      any_track_active = true;
    }
  }

  if (!any_track_active) {
    if (player->asset->loop) {
      const PfrAudioSongAsset* asset = player->asset;
      size_t j;

      for (j = 0; j < player->track_capacity; j++) {
        pfr_audio_runtime_stop_track_runtime(&player->runtime[j]);
      }

      player->paused = false;
      player->stopped = false;
      player->frames_until_tick = 0.0;

      for (j = 0; j < asset->track_count; j++) {
        struct MusicPlayerTrack* track = &player->tracks[j];

        memset(track, 0, sizeof(*track));
        track->flags = MPT_FLG_EXIST;
        track->cmdPtr = (u8*)asset->tracks[j].data;
        track->vol = 127;
        track->velocity = 100;
        track->bendRange = 2;
        track->volX = 64;
        track->lfoSpeed = 22;
        player->runtime[j].track = track;
        player->runtime[j].asset = &asset->tracks[j];
      }
    } else {
      player->stopped = true;
      pfr_audio_runtime_refresh_player(player);
    }
  }
}

static void
pfr_audio_runtime_mix_note(const PfrRuntimeNote* note_const,
                           int32_t* mix_left,
                           int32_t* mix_right)
{
  PfrRuntimeNote* note = (PfrRuntimeNote*)note_const;
  double sample = 0.0;
  double left_gain;
  double right_gain;

  if (!note->active) {
    return;
  }

  if (note->voice == NULL) {
    note->active = false;
    return;
  }

  switch (note->voice->kind) {
    case PFR_AUDIO_VOICE_DIRECTSOUND:
    case PFR_AUDIO_VOICE_DIRECTSOUND_NO_RESAMPLE:
      if (note->sample == NULL || note->sample->sample_count == 0) {
        note->active = false;
        return;
      }

      if ((uint32_t)note->source_pos >= note->sample->sample_count) {
        if (note->sample->loop_enabled &&
            note->sample->loop_start < note->sample->sample_count) {
          uint32_t loop_len =
            note->sample->sample_count - note->sample->loop_start;

          note->source_pos =
            note->sample->loop_start +
            fmod(note->source_pos - note->sample->loop_start, loop_len);
        } else if (!note->released &&
                   note->voice->kind !=
                     PFR_AUDIO_VOICE_DIRECTSOUND_NO_RESAMPLE) {
          note->source_pos = fmod(note->source_pos, note->sample->sample_count);
        } else {
          note->release_frames_left = 0;
          note->active = false;
          return;
        }
      }

      sample =
        (double)note->sample->samples[(uint32_t)note->source_pos] / 127.0;
      note->source_pos += note->source_step;
      break;
    case PFR_AUDIO_VOICE_SQUARE1:
    case PFR_AUDIO_VOICE_SQUARE2: {
      static const double sDutyCycles[] = { 0.125, 0.25, 0.5, 0.75 };
      double duty = sDutyCycles[note->voice->duty_or_period & 3];

      note->phase += note->source_step;
      note->phase -= floor(note->phase);
      sample = note->phase < duty ? 0.85 : -0.85;
      break;
    }
    case PFR_AUDIO_VOICE_PROGRAMMABLE_WAVE:
      if (note->sample == NULL || note->sample->sample_count == 0) {
        note->active = false;
        return;
      }

      sample = (double)note->sample
                 ->samples[(uint32_t)note->phase % note->sample->sample_count] /
               127.0;
      note->phase += note->source_step;
      break;
    case PFR_AUDIO_VOICE_NOISE:
      note->phase += note->source_step;
      if (note->phase >= 1.0) {
        note->phase -= floor(note->phase);
        note->noise_state = note->noise_state * 1664525u + 1013904223u;
        note->held_sample =
          ((float)((note->noise_state >> 16) & 0xFF) / 127.5f) - 1.0f;
      }
      sample = note->held_sample;
      break;
    default:
      note->active = false;
      return;
  }

  if (note->released) {
    if (note->release_frames_left <= 0) {
      note->active = false;
      return;
    }

    sample *= (double)note->release_frames_left / note->release_frames_total;
    note->release_frames_left--;
  }

  pfr_audio_runtime_pan_gains(note->pan, note->gain, &left_gain, &right_gain);
  *mix_left += (int32_t)(sample * left_gain * 32767.0);
  *mix_right += (int32_t)(sample * right_gain * 32767.0);
}

static void
pfr_audio_runtime_mix_player_frame(PfrSongPlayer* player,
                                   int32_t* mix_left,
                                   int32_t* mix_right)
{
  uint8_t i;
  double tick_frames;

  if (player->asset == NULL || player->paused || player->stopped) {
    return;
  }

  if (player->frames_until_tick <= 0.0) {
    pfr_audio_runtime_tick_player(player);
    tick_frames = ((double)PFR_DEFAULT_AUDIO_SAMPLE_RATE * 60.0) /
                  (((double)player->info->tempoD * 2.0) * 24.0);
    if (tick_frames < 1.0) {
      tick_frames = 1.0;
    }
    player->frames_until_tick += tick_frames;
  }

  for (i = 0; i < player->track_capacity; i++) {
    pfr_audio_runtime_mix_note(&player->runtime[i].note, mix_left, mix_right);
  }

  player->frames_until_tick -= 1.0;
}

static void
pfr_audio_runtime_stop_cry(void)
{
  memset(&sCryRuntime, 0, sizeof(sCryRuntime));
  gPokemonCryMusicPlayers[0].status = 0;
}

static void
pfr_audio_runtime_mix_cry_frame(int32_t* mix_left, int32_t* mix_right)
{
  double sample;
  double left_gain;
  double right_gain;
  const PfrAudioSample* asset_sample;

  if (!sCryRuntime.active || sCryRuntime.asset == NULL) {
    return;
  }

  asset_sample = sCryRuntime.asset->sample;
  if (asset_sample == NULL || asset_sample->sample_count == 0) {
    pfr_audio_runtime_stop_cry();
    return;
  }

  if (sCryRuntime.frames_played >= sCryRuntime.frame_limit ||
      sCryRuntime.frame_position < 0.0 ||
      sCryRuntime.frame_position >= asset_sample->sample_count) {
    pfr_audio_runtime_stop_cry();
    return;
  }

  sample =
    (double)asset_sample->samples[(uint32_t)sCryRuntime.frame_position] / 127.0;
  pfr_audio_runtime_pan_gains(sCryStereo ? sCryRuntime.pan : 0,
                              (double)sCryRuntime.volume / 127.0,
                              &left_gain,
                              &right_gain);
  *mix_left += (int32_t)(sample * left_gain * 32767.0);
  *mix_right += (int32_t)(sample * right_gain * 32767.0);

  sCryRuntime.frame_position += sCryRuntime.frame_step;
  sCryRuntime.frames_played++;
  gPokemonCryMusicPlayers[0].status = MUSICPLAYER_STATUS_TRACK;
}

static void
pfr_audio_runtime_start_song(PfrSongPlayer* player,
                             const PfrAudioSongAsset* asset)
{
  size_t i;

  player->asset = asset;
  player->paused = false;
  player->stopped = false;
  player->frames_until_tick = 0.0;
  player->volume = PFR_AUDIO_FULL_VOLUME;
  player->fade_level = PFR_AUDIO_FULL_VOLUME;
  player->fade_interval = 0;
  player->fade_counter = 0;
  player->temporary_fade = false;
  player->fade_in = false;
  player->info->clock = 0;
  player->info->tempoD = 150;
  player->info->tempoU = 0x100;
  player->info->tempoI = 150;
  player->info->tempoC = 0;

  for (i = 0; i < player->track_capacity; i++) {
    player->runtime[i].track = &player->tracks[i];
    pfr_audio_runtime_stop_track_runtime(&player->runtime[i]);
    player->runtime[i].asset =
      i < asset->track_count ? &asset->tracks[i] : NULL;

    if (i < asset->track_count) {
      struct MusicPlayerTrack* track = &player->tracks[i];

      memset(track, 0, sizeof(*track));
      track->flags = MPT_FLG_EXIST;
      track->cmdPtr = (u8*)asset->tracks[i].data;
      track->vol = 127;
      track->velocity = 100;
      track->bendRange = 2;
      track->volX = 64;
      track->lfoSpeed = 22;
    }
  }

  pfr_audio_runtime_refresh_player(player);
}

u16
SpeciesToCryId(u16 species)
{
  return species;
}

void
m4aSoundMode(u32 mode)
{
  gSoundInfo.masterVolume =
    (u8)((mode & SOUND_MODE_MASVOL) >> SOUND_MODE_MASVOL_SHIFT);
  gSoundInfo.maxChans =
    (u8)((mode & SOUND_MODE_MAXCHN) >> SOUND_MODE_MAXCHN_SHIFT);
  gSoundInfo.freq = (u8)((mode & SOUND_MODE_FREQ) >> SOUND_MODE_FREQ_SHIFT);
  gSoundInfo.mode = (u8)(mode & 0xFF);
  gSoundInfo.pcmFreq = PFR_DEFAULT_AUDIO_SAMPLE_RATE;
  gSoundInfo.pcmSamplesPerVBlank = PFR_AUDIO_FRAMES_PER_VBLANK;
}

void
m4aSoundInit(void)
{
  pfr_audio_reset();
  pfr_audio_runtime_reset_state();
  gPfrSoundInfoPtr = &gSoundInfo;
  m4aSoundMode(SOUND_MODE_DA_BIT_8 | SOUND_MODE_FREQ_13379 |
               (12 << SOUND_MODE_MASVOL_SHIFT) |
               (5 << SOUND_MODE_MAXCHN_SHIFT));
  gSoundInfo.ident = ID_NUMBER;
}

void
m4aSoundMain(void)
{
  int16_t output[PFR_AUDIO_FRAMES_PER_VBLANK * PFR_AUDIO_CHANNEL_COUNT];
  size_t frame;

  for (frame = 0; frame < PFR_AUDIO_FRAMES_PER_VBLANK; frame++) {
    int32_t mix_left = 0;
    int32_t mix_right = 0;
    size_t player_index;

    for (player_index = 0; player_index < PFR_ARRAY_COUNT(sSongPlayers);
         player_index++) {
      pfr_audio_runtime_mix_player_frame(
        &sSongPlayers[player_index], &mix_left, &mix_right);
    }

    pfr_audio_runtime_mix_cry_frame(&mix_left, &mix_right);

    output[frame * 2 + 0] = pfr_audio_runtime_clamp_sample(mix_left);
    output[frame * 2 + 1] = pfr_audio_runtime_clamp_sample(mix_right);
  }

  pfr_audio_queue_source_frames(output, PFR_AUDIO_FRAMES_PER_VBLANK);
}

void
m4aSoundVSync(void)
{
  if (sVSyncEnabled) {
    gSoundInfo.pcmDmaCounter++;
  }
}

void
m4aSoundVSyncOn(void)
{
  sVSyncEnabled = TRUE;
}

void
m4aSoundVSyncOff(void)
{
  sVSyncEnabled = FALSE;
}

void
m4aSongNumStart(u16 n)
{
  const PfrAudioSongAsset* asset = pfr_audio_song_asset_for_id(n);
  PfrSongPlayer* player;

  if (asset == NULL) {
    if (n == MUS_NONE || n == MUS_DUMMY) {
      pfr_audio_runtime_full_stop_player(&sSongPlayers[0]);
    }
    return;
  }

  player = &sSongPlayers[asset->player_id];
  pfr_audio_runtime_start_song(player, asset);

  if (player == &sSongPlayers[0]) {
    sCurrentBgmSongId = n;
  } else {
    sPendingSeSongId = n;
    sPendingSe = TRUE;
  }
}

void
m4aSongNumStartOrChange(u16 n)
{
  const PfrAudioSongAsset* asset = pfr_audio_song_asset_for_id(n);
  PfrSongPlayer* player;

  if (asset == NULL) {
    return;
  }

  player = &sSongPlayers[asset->player_id];
  if (player->asset == asset && !player->paused && !player->stopped) {
    return;
  }

  m4aSongNumStart(n);
}

void
m4aSongNumStop(u16 n)
{
  const PfrAudioSongAsset* asset = pfr_audio_song_asset_for_id(n);
  PfrSongPlayer* player;

  if (asset == NULL) {
    return;
  }

  player = &sSongPlayers[asset->player_id];
  if (player->asset == asset) {
    pfr_audio_runtime_pause_player(player);
  }
}

void
m4aMPlayAllStop(void)
{
  size_t i;

  for (i = 0; i < PFR_ARRAY_COUNT(sSongPlayers); i++) {
    pfr_audio_runtime_pause_player(&sSongPlayers[i]);
  }

  pfr_audio_runtime_stop_cry();
}

void
m4aMPlayStop(struct MusicPlayerInfo* mplayInfo)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);

  if (player != NULL) {
    pfr_audio_runtime_pause_player(player);
    return;
  }

  if (mplayInfo == &gPokemonCryMusicPlayers[0]) {
    pfr_audio_runtime_stop_cry();
  }
}

void
m4aMPlayContinue(struct MusicPlayerInfo* mplayInfo)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);

  if (player == NULL || player->asset == NULL) {
    return;
  }

  player->paused = false;
  player->stopped = false;
  pfr_audio_runtime_refresh_player(player);
}

void
m4aMPlayFadeOut(struct MusicPlayerInfo* mplayInfo, u16 speed)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);

  if (player == NULL || player->asset == NULL) {
    return;
  }

  player->temporary_fade = false;
  player->fade_in = false;
  player->fade_interval = speed == 0 ? 1 : speed;
  player->fade_counter = player->fade_interval;
  pfr_audio_runtime_refresh_player(player);
}

void
m4aMPlayFadeOutTemporarily(struct MusicPlayerInfo* mplayInfo, u16 speed)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);

  if (player == NULL || player->asset == NULL) {
    return;
  }

  player->temporary_fade = true;
  player->fade_in = false;
  player->fade_interval = speed == 0 ? 1 : speed;
  player->fade_counter = player->fade_interval;
  pfr_audio_runtime_refresh_player(player);
}

void
m4aMPlayFadeIn(struct MusicPlayerInfo* mplayInfo, u16 speed)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);

  if (player == NULL || player->asset == NULL) {
    return;
  }

  player->paused = false;
  player->stopped = false;
  player->temporary_fade = false;
  player->fade_in = true;
  player->fade_level = 0;
  player->fade_interval = speed == 0 ? 1 : speed;
  player->fade_counter = player->fade_interval;
  pfr_audio_runtime_refresh_player(player);
}

void
m4aMPlayImmInit(struct MusicPlayerInfo* mplayInfo)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);

  if (player == NULL) {
    return;
  }

  player->fade_level = PFR_AUDIO_FULL_VOLUME;
  player->fade_interval = 0;
  player->fade_counter = 0;
  player->temporary_fade = false;
  player->fade_in = false;
  pfr_audio_runtime_refresh_player(player);
}

void
m4aMPlayVolumeControl(struct MusicPlayerInfo* mplayInfo,
                      u16 trackBits,
                      u16 volume)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);
  size_t i;

  if (player == NULL) {
    return;
  }

  player->volume = volume;

  for (i = 0; i < player->track_capacity; i++) {
    if ((trackBits & (1u << i)) != 0) {
      player->tracks[i].volX = (u8)(volume / 4);
    }
  }
}

void
m4aMPlayPanpotControl(struct MusicPlayerInfo* mplayInfo, u16 trackBits, s8 pan)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);
  size_t i;

  if (player == NULL) {
    return;
  }

  for (i = 0; i < player->track_capacity; i++) {
    if ((trackBits & (1u << i)) != 0) {
      player->tracks[i].panX = pan;
    }
  }
}

void
m4aMPlayTempoControl(struct MusicPlayerInfo* mplayInfo, u16 tempo)
{
  if (mplayInfo != NULL) {
    mplayInfo->tempoD = tempo;
  }
}

void
m4aMPlayPitchControl(struct MusicPlayerInfo* mplayInfo,
                     u16 trackBits,
                     s16 pitch)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);
  size_t i;

  if (player == NULL) {
    return;
  }

  for (i = 0; i < player->track_capacity; i++) {
    if ((trackBits & (1u << i)) != 0) {
      player->tracks[i].keyShiftX = (s8)(pitch >> 8);
      player->tracks[i].pitX = (u8)pitch;
    }
  }
}

void
m4aMPlayModDepthSet(struct MusicPlayerInfo* mplayInfo,
                    u16 trackBits,
                    u8 modDepth)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);
  size_t i;

  if (player == NULL) {
    return;
  }

  for (i = 0; i < player->track_capacity; i++) {
    if ((trackBits & (1u << i)) != 0) {
      player->tracks[i].mod = modDepth;
    }
  }
}

void
m4aMPlayLFOSpeedSet(struct MusicPlayerInfo* mplayInfo,
                    u16 trackBits,
                    u8 lfoSpeed)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);
  size_t i;

  if (player == NULL) {
    return;
  }

  for (i = 0; i < player->track_capacity; i++) {
    if ((trackBits & (1u << i)) != 0) {
      player->tracks[i].lfoSpeed = lfoSpeed;
    }
  }
}

struct MusicPlayerInfo*
SetPokemonCryTone(struct ToneData* tone)
{
  const PfrCryAsset* asset = pfr_audio_cry_asset_for_wave(tone->wav);
  double pitch_scale = (double)sCryPitch / (double)PFR_CRY_DEFAULT_PITCH;

  if (asset == NULL || asset->sample == NULL) {
    pfr_audio_runtime_stop_cry();
    return &gPokemonCryMusicPlayers[0];
  }

  sCryRuntime.asset = asset;
  sCryRuntime.frame_position =
    (tone->type == 0x30) ? (double)(asset->sample->sample_count - 1) : 0.0;
  sCryRuntime.frame_step = (tone->type == 0x30) ? -pitch_scale : pitch_scale;
  sCryRuntime.frame_limit =
    (asset->sample->sample_count *
     (uint32_t)(sCryLength == 0 ? PFR_CRY_DEFAULT_LENGTH : sCryLength)) /
    PFR_CRY_DEFAULT_LENGTH;
  if (sCryRuntime.frame_limit == 0 ||
      sCryRuntime.frame_limit > asset->sample->sample_count) {
    sCryRuntime.frame_limit = asset->sample->sample_count;
  }
  sCryRuntime.frames_played = 0;
  sCryRuntime.volume = sCryVolume;
  sCryRuntime.length = sCryLength;
  sCryRuntime.release = sCryRelease;
  sCryRuntime.priority = sCryPriority;
  sCryRuntime.pan = sCryPan;
  sCryRuntime.chorus = sCryChorus;
  sCryRuntime.reverse = tone->type == 0x30;
  sCryRuntime.active = true;
  sPendingCry = TRUE;
  gPokemonCryMusicPlayers[0].ident = ID_NUMBER;
  gPokemonCryMusicPlayers[0].trackCount = 1;
  gPokemonCryMusicPlayers[0].status = MUSICPLAYER_STATUS_TRACK;
  return &gPokemonCryMusicPlayers[0];
}

void
SetPokemonCryVolume(u8 val)
{
  sCryVolume = val;
}

void
SetPokemonCryPanpot(s8 val)
{
  sCryPan = val;
}

void
SetPokemonCryPitch(s16 val)
{
  sCryPitch = (uint16_t)val;
}

void
SetPokemonCryLength(u16 val)
{
  sCryLength = val;
}

void
SetPokemonCryRelease(u8 val)
{
  sCryRelease = val;
}

void
SetPokemonCryProgress(u32 val)
{
  (void)val;
}

bool32
IsPokemonCryPlaying(struct MusicPlayerInfo* mplayInfo)
{
  (void)mplayInfo;
  return sCryRuntime.active;
}

void
SetPokemonCryChorus(s8 val)
{
  sCryChorus = val;
}

void
SetPokemonCryStereo(u32 val)
{
  sCryStereo = val ? TRUE : FALSE;

  if (val) {
    gSoundInfo.mode &= (u8)~1;
  } else {
    gSoundInfo.mode |= 1;
  }
}

void
SetPokemonCryPriority(u8 val)
{
  sCryPriority = val;
}

void
ClearPokemonCrySongs(void)
{
  memset(gPokemonCrySongs, 0, sizeof(gPokemonCrySongs));
  pfr_audio_runtime_stop_cry();
}

u16
pfr_stub_current_bgm(void)
{
  return sCurrentBgmSongId;
}

bool8
pfr_stub_is_bgm_playing(void)
{
  return sSongPlayers[0].asset != NULL && !sSongPlayers[0].paused &&
         !sSongPlayers[0].stopped;
}

bool8
pfr_stub_take_se(u16* songNum)
{
  if (!sPendingSe) {
    return FALSE;
  }

  *songNum = sPendingSeSongId;
  sPendingSe = FALSE;
  return TRUE;
}

bool8
pfr_stub_take_cry(void)
{
  bool8 pending = sPendingCry;

  sPendingCry = FALSE;
  return pending;
}
