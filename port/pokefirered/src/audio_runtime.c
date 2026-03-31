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
  PFR_AUDIO_FRAMES_PER_VBLANK = 224,
  PFR_CRY_DEFAULT_PITCH = 15360,
  PFR_CRY_DEFAULT_LENGTH = 140,
};

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

extern void SoundMain(void);
extern void MPlayJumpTableCopy(MPlayFunc *dest);
extern void MPlayOpen(struct MusicPlayerInfo *, struct MusicPlayerTrack *, u8);
extern void MPlayStart(struct MusicPlayerInfo *, struct SongHeader *);
extern void MPlayContinue(struct MusicPlayerInfo *);
extern void MPlayFadeOut(struct MusicPlayerInfo *, u16);
extern void TrackStop(struct MusicPlayerInfo *, struct MusicPlayerTrack *);
extern void DummyFunc(void);
extern void ply_note(u32, struct MusicPlayerInfo *, struct MusicPlayerTrack *);

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

static int16_t
pfr_clamp16(int32_t v)
{
  if (v < -32768)
    return -32768;
  if (v > 32767)
    return 32767;
  return (int16_t)v;
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
pfr_audio_mix_channels(struct SoundInfo* si, int16_t* output, u32 numSamples)
{
  u32 frame;

  for (frame = 0; frame < numSamples; frame++) {
    s32 mixL = 0, mixR = 0;
    u32 ch;

    for (ch = 0; ch < si->maxChans; ch++) {
      struct SoundChannel* chan = &si->chans[ch];
      s32 sample;
      u32 advance;

      if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
        continue;
      if (chan->statusFlags & SOUND_CHANNEL_SF_START)
        continue;
      if (chan->wav == NULL || chan->currentPointer == NULL)
        continue;

      sample = *chan->currentPointer;
      mixL += (sample * chan->leftVolume * chan->envelopeVolumeLeft) >> 8;
      mixR += (sample * chan->rightVolume * chan->envelopeVolumeRight) >> 8;

      chan->fw += chan->frequency;
      advance = chan->fw / (u32)si->pcmFreq;
      chan->fw %= (u32)si->pcmFreq;

      if (advance > 0) {
        if (advance <= chan->count) {
          chan->currentPointer += advance;
          chan->count -= advance;
        } else {
          if (chan->statusFlags & SOUND_CHANNEL_SF_LOOP) {
            chan->currentPointer =
              (s8*)chan->wav->data + chan->wav->loopStart;
            chan->count = chan->wav->size - chan->wav->loopStart;
          } else {
            chan->statusFlags = 0;
          }
        }
      }
    }

    output[frame * 2 + 0] = pfr_clamp16(mixL);
    output[frame * 2 + 1] = pfr_clamp16(mixR);
  }
}

static void
pfr_audio_runtime_stop_cry(void)
{
  memset(&sCryRuntime, 0, sizeof(sCryRuntime));
  gPokemonCryMusicPlayers[0].status = 0;
}

static void
pfr_audio_mix_cry(int16_t* output, u32 numSamples)
{
  u32 frame;
  const PfrAudioSample* asset_sample;
  double left_gain, right_gain;
  int clamped;

  if (!sCryRuntime.active || sCryRuntime.asset == NULL)
    return;

  asset_sample = sCryRuntime.asset->sample;
  if (asset_sample == NULL || asset_sample->sample_count == 0) {
    pfr_audio_runtime_stop_cry();
    return;
  }

  clamped = sCryStereo ? sCryRuntime.pan : 0;
  if (clamped < -64)
    clamped = -64;
  if (clamped > 63)
    clamped = 63;
  right_gain =
    ((double)(clamped + 64) / 127.0) * ((double)sCryRuntime.volume / 127.0);
  left_gain =
    ((double)(63 - clamped) / 127.0) * ((double)sCryRuntime.volume / 127.0);

  for (frame = 0; frame < numSamples; frame++) {
    double sample;
    s32 sL, sR;

    if (sCryRuntime.frames_played >= sCryRuntime.frame_limit ||
        sCryRuntime.frame_position < 0.0 ||
        sCryRuntime.frame_position >= asset_sample->sample_count) {
      pfr_audio_runtime_stop_cry();
      return;
    }

    sample =
      (double)asset_sample->samples[(uint32_t)sCryRuntime.frame_position] /
      127.0;
    sL = (s32)(sample * left_gain * 32767.0);
    sR = (s32)(sample * right_gain * 32767.0);

    output[frame * 2 + 0] = pfr_clamp16(output[frame * 2 + 0] + sL);
    output[frame * 2 + 1] = pfr_clamp16(output[frame * 2 + 1] + sR);

    sCryRuntime.frame_position += sCryRuntime.frame_step;
    sCryRuntime.frames_played++;
  }

  gPokemonCryMusicPlayers[0].status = MUSICPLAYER_STATUS_TRACK;
}

void
SampleFreqSet(u32 freq)
{
  (void)freq;
}

void
MPlayExtender(struct CgbChannel* cgbChans)
{
  (void)cgbChans;
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

  memset(&gSoundInfo, 0, sizeof(gSoundInfo));
  gPfrSoundInfoPtr = &gSoundInfo;

  m4aSoundMode(SOUND_MODE_DA_BIT_8 | SOUND_MODE_FREQ_13379 |
               (12 << SOUND_MODE_MASVOL_SHIFT) |
               (5 << SOUND_MODE_MAXCHN_SHIFT));

  gSoundInfo.plynote = (PlyNoteFunc)ply_note;
  gSoundInfo.CgbSound = (CgbSoundFunc)DummyFunc;
  gSoundInfo.CgbOscOff = (CgbOscOffFunc)DummyFunc;
  gSoundInfo.MidiKeyToCgbFreq = (MidiKeyToCgbFreqFunc)DummyFunc;
  gSoundInfo.ExtVolPit = (ExtVolPitFunc)DummyFunc;
  gSoundInfo.MPlayJumpTable = gMPlayJumpTable;
  gSoundInfo.ident = ID_NUMBER;

  MPlayJumpTableCopy(gMPlayJumpTable);

  MPlayOpen(&gMPlayInfo_BGM, sMPlayTrackBgm, 10);
  MPlayOpen(&gMPlayInfo_SE1, sMPlayTrackSe1, 3);
  MPlayOpen(&gMPlayInfo_SE2, sMPlayTrackSe2, 9);
  MPlayOpen(&gMPlayInfo_SE3, sMPlayTrackSe3, 1);

  memset(&gPokemonCrySong, 0, sizeof(gPokemonCrySong));
  memset(gPokemonCrySongs, 0, sizeof(gPokemonCrySongs));
  memset(gPokemonCryMusicPlayers, 0, sizeof(gPokemonCryMusicPlayers));
  memset(gPokemonCryTracks, 0, sizeof(gPokemonCryTracks));
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

void
m4aSoundMain(void)
{
  int16_t output[PFR_AUDIO_FRAMES_PER_VBLANK * PFR_AUDIO_CHANNEL_COUNT];

  SoundMain();

  pfr_audio_mix_channels(&gSoundInfo, output, PFR_AUDIO_FRAMES_PER_VBLANK);
  pfr_audio_mix_cry(output, PFR_AUDIO_FRAMES_PER_VBLANK);

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
  const struct MusicPlayer* mplayTable = gMPlayTable;
  const struct Song* songTable = gSongTable;
  const struct Song* song = &songTable[n];
  const struct MusicPlayer* mplay;

  if (song->header == NULL)
    return;

  mplay = &mplayTable[song->ms];
  MPlayStart(mplay->info, song->header);

  if (mplay->info == &gMPlayInfo_BGM) {
    sCurrentBgmSongId = n;
  } else {
    sPendingSeSongId = n;
    sPendingSe = TRUE;
  }
}

void
m4aSongNumStartOrChange(u16 n)
{
  const struct Song* song = &gSongTable[n];
  const struct MusicPlayer* mplay = &gMPlayTable[song->ms];

  if (mplay->info->songHeader == song->header
      && !(mplay->info->status & MUSICPLAYER_STATUS_PAUSE)) {
    return;
  }

  m4aSongNumStart(n);
}

void
m4aSongNumStartOrContinue(u16 n)
{
  const struct Song* song = &gSongTable[n];
  const struct MusicPlayer* mplay = &gMPlayTable[song->ms];

  if (mplay->info->status & MUSICPLAYER_STATUS_PAUSE) {
    MPlayContinue(mplay->info);
  } else if (mplay->info->songHeader != song->header) {
    m4aSongNumStart(n);
  }
}

void
m4aSongNumContinue(u16 n)
{
  const struct Song* song = &gSongTable[n];
  const struct MusicPlayer* mplay = &gMPlayTable[song->ms];

  MPlayContinue(mplay->info);
}

void
m4aSongNumStop(u16 n)
{
  const struct Song* song = &gSongTable[n];
  const struct MusicPlayer* mplay = &gMPlayTable[song->ms];

  if (mplay->info->songHeader == song->header) {
    m4aMPlayStop(mplay->info);
  }
}

void
m4aMPlayAllStop(void)
{
  s32 i;

  for (i = 0; i < (s32)(sizeof(gMPlayTable) / sizeof(gMPlayTable[0])); i++) {
    m4aMPlayStop(gMPlayTable[i].info);
  }

  pfr_audio_runtime_stop_cry();
}

void
m4aMPlayAllContinue(void)
{
  s32 i;

  for (i = 0; i < (s32)(sizeof(gMPlayTable) / sizeof(gMPlayTable[0])); i++) {
    MPlayContinue(gMPlayTable[i].info);
  }
}

void
m4aMPlayStop(struct MusicPlayerInfo* mplayInfo)
{
  s32 i;
  struct MusicPlayerTrack* track;

  if (mplayInfo->ident != ID_NUMBER)
    return;

  mplayInfo->ident++;
  mplayInfo->status |= MUSICPLAYER_STATUS_PAUSE;

  i = mplayInfo->trackCount;
  track = mplayInfo->tracks;

  while (i > 0) {
    TrackStop(mplayInfo, track);
    i--;
    track++;
  }

  mplayInfo->ident = ID_NUMBER;

  if (mplayInfo == &gPokemonCryMusicPlayers[0]) {
    pfr_audio_runtime_stop_cry();
  }
}

void
m4aMPlayContinue(struct MusicPlayerInfo* mplayInfo)
{
  MPlayContinue(mplayInfo);
}

void
m4aMPlayFadeOut(struct MusicPlayerInfo* mplayInfo, u16 speed)
{
  MPlayFadeOut(mplayInfo, speed);
}

void
m4aMPlayFadeOutTemporarily(struct MusicPlayerInfo* mplayInfo, u16 speed)
{
  if (mplayInfo->ident != ID_NUMBER)
    return;

  mplayInfo->ident++;
  mplayInfo->fadeOI = speed;
  mplayInfo->fadeOC = speed;
  mplayInfo->fadeOV = (u16)FADE_VOL_MAX;
  mplayInfo->status |= TEMPORARY_FADE;
  mplayInfo->ident = ID_NUMBER;
}

void
m4aMPlayFadeIn(struct MusicPlayerInfo* mplayInfo, u16 speed)
{
  if (mplayInfo->ident != ID_NUMBER)
    return;

  mplayInfo->ident++;
  mplayInfo->fadeOI = speed;
  mplayInfo->fadeOC = speed;
  mplayInfo->fadeOV = 0;
  mplayInfo->status &= ~MUSICPLAYER_STATUS_PAUSE;
  mplayInfo->status |= FADE_IN;
  mplayInfo->ident = ID_NUMBER;
}

void
m4aMPlayImmInit(struct MusicPlayerInfo* mplayInfo)
{
  s32 trackCount = mplayInfo->trackCount;
  struct MusicPlayerTrack* track = mplayInfo->tracks;

  while (trackCount > 0) {
    if (track->flags & MPT_FLG_EXIST) {
      if (track->flags & MPT_FLG_START) {
        track->flags = MPT_FLG_EXIST;
        track->bendRange = 2;
        track->volX = 64;
        track->lfoSpeed = 22;
        track->tone.type = 1;
      }
    }

    trackCount--;
    track++;
  }
}

void
m4aMPlayVolumeControl(struct MusicPlayerInfo* mplayInfo, u16 trackBits,
                      u16 volume)
{
  s32 i;
  struct MusicPlayerTrack* track;
  u32 bit;

  if (mplayInfo->ident != ID_NUMBER)
    return;

  mplayInfo->ident++;

  i = mplayInfo->trackCount;
  track = mplayInfo->tracks;
  bit = 1;

  while (i > 0) {
    if (bit & trackBits) {
      if (track->flags & MPT_FLG_EXIST) {
        track->volX = (u8)(volume / 4);
        track->flags |= MPT_FLG_VOLCHG;
      }
    }

    i--;
    track++;
    bit <<= 1;
  }

  mplayInfo->ident = ID_NUMBER;
}

void
m4aMPlayPanpotControl(struct MusicPlayerInfo* mplayInfo, u16 trackBits,
                      s8 pan)
{
  s32 i;
  struct MusicPlayerTrack* track;
  u32 bit;

  if (mplayInfo->ident != ID_NUMBER)
    return;

  mplayInfo->ident++;

  i = mplayInfo->trackCount;
  track = mplayInfo->tracks;
  bit = 1;

  while (i > 0) {
    if (bit & trackBits) {
      if (track->flags & MPT_FLG_EXIST) {
        track->panX = pan;
        track->flags |= MPT_FLG_VOLCHG;
      }
    }

    i--;
    track++;
    bit <<= 1;
  }

  mplayInfo->ident = ID_NUMBER;
}

void
m4aMPlayTempoControl(struct MusicPlayerInfo* mplayInfo, u16 tempo)
{
  if (mplayInfo->ident != ID_NUMBER)
    return;

  mplayInfo->ident++;
  mplayInfo->tempoU = tempo;
  mplayInfo->tempoI = (mplayInfo->tempoD * mplayInfo->tempoU) >> 8;
  mplayInfo->ident = ID_NUMBER;
}

void
m4aMPlayPitchControl(struct MusicPlayerInfo* mplayInfo, u16 trackBits,
                     s16 pitch)
{
  s32 i;
  struct MusicPlayerTrack* track;
  u32 bit;

  if (mplayInfo->ident != ID_NUMBER)
    return;

  mplayInfo->ident++;

  i = mplayInfo->trackCount;
  track = mplayInfo->tracks;
  bit = 1;

  while (i > 0) {
    if (bit & trackBits) {
      if (track->flags & MPT_FLG_EXIST) {
        track->keyShiftX = (s8)(pitch >> 8);
        track->pitX = (u8)pitch;
        track->flags |= MPT_FLG_PITCHG;
      }
    }

    i--;
    track++;
    bit <<= 1;
  }

  mplayInfo->ident = ID_NUMBER;
}

void
m4aMPlayModDepthSet(struct MusicPlayerInfo* mplayInfo, u16 trackBits,
                    u8 modDepth)
{
  s32 i;
  struct MusicPlayerTrack* track;
  u32 bit;

  if (mplayInfo->ident != ID_NUMBER)
    return;

  mplayInfo->ident++;

  i = mplayInfo->trackCount;
  track = mplayInfo->tracks;
  bit = 1;

  while (i > 0) {
    if (bit & trackBits) {
      if (track->flags & MPT_FLG_EXIST) {
        track->mod = modDepth;

        if (!modDepth)
          track->modM = 0;
      }
    }

    i--;
    track++;
    bit <<= 1;
  }

  mplayInfo->ident = ID_NUMBER;
}

void
m4aMPlayLFOSpeedSet(struct MusicPlayerInfo* mplayInfo, u16 trackBits,
                    u8 lfoSpeed)
{
  s32 i;
  struct MusicPlayerTrack* track;
  u32 bit;

  if (mplayInfo->ident != ID_NUMBER)
    return;

  mplayInfo->ident++;

  i = mplayInfo->trackCount;
  track = mplayInfo->tracks;
  bit = 1;

  while (i > 0) {
    if (bit & trackBits) {
      if (track->flags & MPT_FLG_EXIST) {
        track->lfoSpeed = lfoSpeed;

        if (!track->lfoSpeed)
          track->modM = 0;
      }
    }

    i--;
    track++;
    bit <<= 1;
  }

  mplayInfo->ident = ID_NUMBER;
}

u16
SpeciesToCryId(u16 species)
{
  return species;
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
  return !(gMPlayInfo_BGM.status & MUSICPLAYER_STATUS_PAUSE) &&
         gMPlayInfo_BGM.songHeader != NULL;
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
