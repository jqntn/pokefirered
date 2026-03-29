#include "pfr/audio.h"
#include "pfr/audio_assets.h"
#include "pfr/core.h"
#include "pfr/stubs.h"

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
};

typedef struct PfrSongPlayer
{
  struct MusicPlayerInfo* info;
  const PfrAudioClipAsset* asset;
  uint32_t frame_index;
  uint16_t volume;
  uint16_t fade_level;
  uint16_t fade_interval;
  uint16_t fade_counter;
  int8_t pan;
  bool8 paused;
  bool8 temporary_fade;
  bool8 fade_in;
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
  bool8 reverse;
  bool8 active;
} PfrCryRuntime;

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
const struct PokemonCrySong gPokemonCrySongTemplate = { 0 };
char SoundMainRAM[1] = { 0 };
char gNumMusicPlayers[1] = { 4 };
char gMaxLines[1] = { 5 };
u8 gDisableMapMusicChangeOnMapLoad = 0;
u8 gDisableHelpSystemVolumeReduce = 0;
u32 gBattleTypeFlags = 0;

static PfrSongPlayer sSongPlayers[PFR_AUDIO_PLAYER_COUNT] = {
  { &gMPlayInfo_BGM,
    NULL,
    0,
    PFR_AUDIO_FULL_VOLUME,
    PFR_AUDIO_FULL_VOLUME,
    0,
    0,
    0,
    FALSE,
    FALSE,
    FALSE },
  { &gMPlayInfo_SE1,
    NULL,
    0,
    PFR_AUDIO_FULL_VOLUME,
    PFR_AUDIO_FULL_VOLUME,
    0,
    0,
    0,
    FALSE,
    FALSE,
    FALSE },
  { &gMPlayInfo_SE2,
    NULL,
    0,
    PFR_AUDIO_FULL_VOLUME,
    PFR_AUDIO_FULL_VOLUME,
    0,
    0,
    0,
    FALSE,
    FALSE,
    FALSE },
  { &gMPlayInfo_SE3,
    NULL,
    0,
    PFR_AUDIO_FULL_VOLUME,
    PFR_AUDIO_FULL_VOLUME,
    0,
    0,
    0,
    FALSE,
    FALSE,
    FALSE },
};

static PfrCryRuntime sCryRuntime;
static bool8 sVSyncEnabled;
static bool8 sCryStereo;
static u16 sCurrentBgmSongId;
static u16 sPendingSeSongId;
static bool8 sPendingSe;
static bool8 sPendingCry;
static uint16_t sCryPitch;
static uint16_t sCryLength;
static uint8_t sCryRelease;
static uint8_t sCryVolume;
static uint8_t sCryPriority;
static int8_t sCryPan;
static int8_t sCryChorus;

static void
pfr_audio_runtime_refresh_player(PfrSongPlayer* player);

static void
pfr_audio_runtime_stop_song_player(PfrSongPlayer* player)
{
  player->asset = NULL;
  player->frame_index = 0;
  player->fade_level = PFR_AUDIO_FULL_VOLUME;
  player->fade_interval = 0;
  player->fade_counter = 0;
  player->temporary_fade = FALSE;
  player->fade_in = FALSE;
  player->paused = FALSE;
  pfr_audio_runtime_refresh_player(player);
}

static void
pfr_audio_runtime_stop_cry(void)
{
  sCryRuntime.asset = NULL;
  sCryRuntime.frame_position = 0.0;
  sCryRuntime.frame_step = 1.0;
  sCryRuntime.frame_limit = 0;
  sCryRuntime.frames_played = 0;
  sCryRuntime.active = FALSE;
  gPokemonCryMusicPlayers[0].status = 0;
}

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
pfr_audio_runtime_pan_gains(int pan, int32_t* left_gain, int32_t* right_gain)
{
  int clamped = pfr_audio_runtime_clamp_pan(pan);

  *left_gain =
    clamped > 0 ? (PFR_AUDIO_FULL_VOLUME - clamped * 4) : PFR_AUDIO_FULL_VOLUME;
  *right_gain =
    clamped < 0 ? (PFR_AUDIO_FULL_VOLUME + clamped * 4) : PFR_AUDIO_FULL_VOLUME;

  if (*left_gain < 0) {
    *left_gain = 0;
  }

  if (*right_gain < 0) {
    *right_gain = 0;
  }
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

static const PfrAudioClipAsset*
pfr_audio_runtime_find_song_asset(u16 song_id)
{
  size_t i;

  for (i = 0; i < gPfrAudioSongAssetCount; i++) {
    if (gPfrAudioSongAssets[i].song_id == song_id) {
      return &gPfrAudioSongAssets[i];
    }
  }

  return NULL;
}

static const PfrCryAsset*
pfr_audio_runtime_find_cry_asset(const struct WaveData* wave)
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
pfr_audio_runtime_refresh_player(PfrSongPlayer* player)
{
  player->info->ident = ID_NUMBER;
  player->info->trackCount = player->asset != NULL ? 1 : 0;
  player->info->songHeader = (struct SongHeader*)player->asset;
  player->info->fadeOI = player->fade_interval;
  player->info->fadeOC = player->fade_counter;
  player->info->fadeOV = player->fade_level;
  player->info->status = 0;

  if (player->asset != NULL) {
    player->info->status = MUSICPLAYER_STATUS_TRACK;

    if (player->paused) {
      player->info->status |= MUSICPLAYER_STATUS_PAUSE;
    }
  }
}

static void
pfr_audio_runtime_reset_song_player(PfrSongPlayer* player)
{
  player->asset = NULL;
  player->frame_index = 0;
  player->volume = PFR_AUDIO_FULL_VOLUME;
  player->fade_level = PFR_AUDIO_FULL_VOLUME;
  player->fade_interval = 0;
  player->fade_counter = 0;
  player->pan = 0;
  player->paused = FALSE;
  player->temporary_fade = FALSE;
  player->fade_in = FALSE;
  pfr_audio_runtime_refresh_player(player);
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
    pfr_audio_runtime_reset_song_player(&sSongPlayers[i]);
  }

  pfr_audio_runtime_stop_cry();

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

static void
pfr_audio_runtime_start_song(PfrSongPlayer* player,
                             const PfrAudioClipAsset* asset)
{
  player->asset = asset;
  player->frame_index = 0;
  player->volume = PFR_AUDIO_FULL_VOLUME;
  player->fade_level = PFR_AUDIO_FULL_VOLUME;
  player->fade_interval = 0;
  player->fade_counter = 0;
  player->pan = 0;
  player->paused = FALSE;
  player->temporary_fade = FALSE;
  player->fade_in = FALSE;
  pfr_audio_runtime_refresh_player(player);
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
      player->fade_in = FALSE;
    } else {
      player->fade_level = (uint16_t)(player->fade_level + PFR_AUDIO_FADE_STEP);
    }
  } else if (player->fade_level <= PFR_AUDIO_FADE_STEP) {
    player->fade_level = 0;
    player->fade_interval = 0;
    player->fade_counter = 0;

    if (player->temporary_fade) {
      player->paused = TRUE;
      player->temporary_fade = FALSE;
    } else {
      pfr_audio_runtime_stop_song_player(player);
      return;
    }
  } else {
    player->fade_level = (uint16_t)(player->fade_level - PFR_AUDIO_FADE_STEP);
  }

  pfr_audio_runtime_refresh_player(player);
}

static void
pfr_audio_runtime_mix_song_player(PfrSongPlayer* player,
                                  int32_t* mix_buffer,
                                  size_t frame_count)
{
  int32_t left_gain;
  int32_t right_gain;
  size_t frame_index;
  int32_t effective_volume;

  if (player->asset == NULL || player->paused) {
    return;
  }

  effective_volume = (int32_t)player->volume * (int32_t)player->fade_level /
                     PFR_AUDIO_FULL_VOLUME;
  pfr_audio_runtime_pan_gains(player->pan, &left_gain, &right_gain);

  for (frame_index = 0; frame_index < frame_count; frame_index++) {
    size_t source_offset;
    int32_t sample_left;
    int32_t sample_right;
    size_t dest_offset;

    if (player->frame_index >= player->asset->frame_count) {
      if (player->asset->loop) {
        player->frame_index = 0;
      } else {
        pfr_audio_runtime_stop_song_player(player);
        break;
      }
    }

    source_offset = (size_t)player->frame_index * PFR_AUDIO_CHANNEL_COUNT;
    sample_left =
      (int32_t)player->asset->samples[source_offset + 0] * effective_volume;
    sample_right =
      (int32_t)player->asset->samples[source_offset + 1] * effective_volume;
    sample_left = sample_left * left_gain / PFR_AUDIO_FULL_VOLUME;
    sample_right = sample_right * right_gain / PFR_AUDIO_FULL_VOLUME;

    dest_offset = frame_index * PFR_AUDIO_CHANNEL_COUNT;
    mix_buffer[dest_offset + 0] += sample_left;
    mix_buffer[dest_offset + 1] += sample_right;
    player->frame_index++;
  }
}

static void
pfr_audio_runtime_mix_cry(int32_t* mix_buffer, size_t frame_count)
{
  int32_t left_gain;
  int32_t right_gain;
  size_t frame_index;

  if (!sCryRuntime.active || sCryRuntime.asset == NULL) {
    return;
  }

  pfr_audio_runtime_pan_gains(
    sCryStereo ? sCryRuntime.pan : 0, &left_gain, &right_gain);

  for (frame_index = 0; frame_index < frame_count; frame_index++) {
    uint32_t source_frame;
    size_t source_offset;
    int32_t sample_left;
    int32_t sample_right;
    size_t dest_offset;

    if (!sCryRuntime.active) {
      break;
    }

    if (sCryRuntime.frames_played >= sCryRuntime.frame_limit) {
      pfr_audio_runtime_stop_cry();
      break;
    }

    if (sCryRuntime.frame_position < 0.0 ||
        sCryRuntime.frame_position >= (double)sCryRuntime.asset->frame_count) {
      pfr_audio_runtime_stop_cry();
      break;
    }

    source_frame = (uint32_t)sCryRuntime.frame_position;
    source_offset = (size_t)source_frame * PFR_AUDIO_CHANNEL_COUNT;
    sample_left = (int32_t)sCryRuntime.asset->samples[source_offset + 0] *
                  sCryRuntime.volume;
    sample_right = (int32_t)sCryRuntime.asset->samples[source_offset + 1] *
                   sCryRuntime.volume;

    if (!sCryStereo) {
      int32_t mono_sample = (sample_left + sample_right) / 2;

      sample_left = mono_sample;
      sample_right = mono_sample;
    }

    sample_left = sample_left * left_gain / 127;
    sample_right = sample_right * right_gain / 127;

    dest_offset = frame_index * PFR_AUDIO_CHANNEL_COUNT;
    mix_buffer[dest_offset + 0] += sample_left;
    mix_buffer[dest_offset + 1] += sample_right;

    sCryRuntime.frame_position += sCryRuntime.frame_step;
    sCryRuntime.frames_played++;
  }

  if (!sCryRuntime.active) {
    return;
  }

  gPokemonCryMusicPlayers[0].status = MUSICPLAYER_STATUS_TRACK;
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
  int32_t mix_buffer[PFR_AUDIO_FRAMES_PER_VBLANK * PFR_AUDIO_CHANNEL_COUNT];
  int16_t output[PFR_AUDIO_FRAMES_PER_VBLANK * PFR_AUDIO_CHANNEL_COUNT];
  size_t i;

  memset(mix_buffer, 0, sizeof(mix_buffer));

  for (i = 0; i < PFR_ARRAY_COUNT(sSongPlayers); i++) {
    pfr_audio_runtime_update_fade(&sSongPlayers[i]);
    pfr_audio_runtime_mix_song_player(
      &sSongPlayers[i], mix_buffer, PFR_AUDIO_FRAMES_PER_VBLANK);
  }

  pfr_audio_runtime_mix_cry(mix_buffer, PFR_AUDIO_FRAMES_PER_VBLANK);

  for (i = 0; i < PFR_ARRAY_COUNT(output); i++) {
    output[i] = pfr_audio_runtime_clamp_sample(mix_buffer[i]);
  }

  pfr_audio_queue_source_frames(output, PFR_AUDIO_FRAMES_PER_VBLANK);
}

void
m4aSoundVSync(void)
{
  if (!sVSyncEnabled) {
    return;
  }

  gSoundInfo.pcmDmaCounter++;
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
  const PfrAudioClipAsset* asset = pfr_audio_runtime_find_song_asset(n);
  PfrSongPlayer* player;

  if (asset == NULL) {
    if (n == MUS_NONE || n == MUS_DUMMY) {
      pfr_audio_runtime_stop_song_player(&sSongPlayers[0]);
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
  const PfrAudioClipAsset* asset = pfr_audio_runtime_find_song_asset(n);
  PfrSongPlayer* player;

  if (asset == NULL) {
    return;
  }

  player = &sSongPlayers[asset->player_id];
  if (player->asset == asset && !player->paused) {
    return;
  }

  m4aSongNumStart(n);
}

void
m4aSongNumStop(u16 n)
{
  const PfrAudioClipAsset* asset = pfr_audio_runtime_find_song_asset(n);
  PfrSongPlayer* player;

  if (asset == NULL) {
    return;
  }

  player = &sSongPlayers[asset->player_id];
  if (player->asset == asset) {
    pfr_audio_runtime_stop_song_player(player);
  }
}

void
m4aMPlayAllStop(void)
{
  size_t i;

  for (i = 0; i < PFR_ARRAY_COUNT(sSongPlayers); i++) {
    pfr_audio_runtime_stop_song_player(&sSongPlayers[i]);
  }

  pfr_audio_runtime_stop_cry();
}

void
m4aMPlayStop(struct MusicPlayerInfo* mplayInfo)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);

  if (player != NULL) {
    pfr_audio_runtime_stop_song_player(player);
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

  player->paused = FALSE;
  player->fade_level = PFR_AUDIO_FULL_VOLUME;
  player->fade_interval = 0;
  player->fade_counter = 0;
  player->temporary_fade = FALSE;
  player->fade_in = FALSE;
  pfr_audio_runtime_refresh_player(player);
}

void
m4aMPlayFadeOut(struct MusicPlayerInfo* mplayInfo, u16 speed)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);

  if (player == NULL || player->asset == NULL) {
    return;
  }

  player->temporary_fade = FALSE;
  player->fade_in = FALSE;
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

  player->temporary_fade = TRUE;
  player->fade_in = FALSE;
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

  player->paused = FALSE;
  player->temporary_fade = FALSE;
  player->fade_in = TRUE;
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
  player->temporary_fade = FALSE;
  player->fade_in = FALSE;
  pfr_audio_runtime_refresh_player(player);
}

void
m4aMPlayVolumeControl(struct MusicPlayerInfo* mplayInfo,
                      u16 trackBits,
                      u16 volume)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);

  (void)trackBits;

  if (player == NULL) {
    return;
  }

  player->volume = volume;
  pfr_audio_runtime_refresh_player(player);
}

void
m4aMPlayPanpotControl(struct MusicPlayerInfo* mplayInfo, u16 trackBits, s8 pan)
{
  PfrSongPlayer* player = pfr_audio_runtime_player_from_info(mplayInfo);

  (void)trackBits;

  if (player == NULL) {
    return;
  }

  player->pan = pan;
}

void
m4aMPlayTempoControl(struct MusicPlayerInfo* mplayInfo, u16 tempo)
{
  (void)mplayInfo;
  (void)tempo;
}

void
m4aMPlayPitchControl(struct MusicPlayerInfo* mplayInfo,
                     u16 trackBits,
                     s16 pitch)
{
  (void)mplayInfo;
  (void)trackBits;
  (void)pitch;
}

void
m4aMPlayModDepthSet(struct MusicPlayerInfo* mplayInfo,
                    u16 trackBits,
                    u8 modDepth)
{
  (void)mplayInfo;
  (void)trackBits;
  (void)modDepth;
}

void
m4aMPlayLFOSpeedSet(struct MusicPlayerInfo* mplayInfo,
                    u16 trackBits,
                    u8 lfoSpeed)
{
  (void)mplayInfo;
  (void)trackBits;
  (void)lfoSpeed;
}

struct MusicPlayerInfo*
SetPokemonCryTone(struct ToneData* tone)
{
  const PfrCryAsset* asset = pfr_audio_runtime_find_cry_asset(tone->wav);
  double pitch_scale = (double)sCryPitch / (double)PFR_CRY_DEFAULT_PITCH;

  if (asset == NULL) {
    pfr_audio_runtime_stop_cry();
    return &gPokemonCryMusicPlayers[0];
  }

  sCryRuntime.asset = asset;
  sCryRuntime.frame_position =
    (tone->type == 0x30) ? (double)(asset->frame_count - 1) : 0.0;
  sCryRuntime.frame_step = (tone->type == 0x30) ? -pitch_scale : pitch_scale;
  sCryRuntime.frame_limit =
    (asset->frame_count *
     (uint32_t)(sCryLength == 0 ? PFR_CRY_DEFAULT_LENGTH : sCryLength)) /
    PFR_CRY_DEFAULT_LENGTH;
  if (sCryRuntime.frame_limit == 0 ||
      sCryRuntime.frame_limit > asset->frame_count) {
    sCryRuntime.frame_limit = asset->frame_count;
  }
  sCryRuntime.frames_played = 0;
  sCryRuntime.volume = sCryVolume;
  sCryRuntime.length = sCryLength;
  sCryRuntime.release = sCryRelease;
  sCryRuntime.priority = sCryPriority;
  sCryRuntime.pan = sCryPan;
  sCryRuntime.chorus = sCryChorus;
  sCryRuntime.reverse = tone->type == 0x30;
  sCryRuntime.active = TRUE;
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
  sCryPitch = (u8)(val & 0xFF);
}

void
SetPokemonCryLength(u16 val)
{
  sCryLength = (uint8_t)val;
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
  return sSongPlayers[0].asset != NULL && !sSongPlayers[0].paused;
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
