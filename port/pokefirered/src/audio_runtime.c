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

extern const u8 gCgb3Vol[];

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

extern void
SoundMain(void);
extern void
MPlayJumpTableCopy(MPlayFunc* dest);
extern void
MPlayExtender(struct CgbChannel*);
extern void
MPlayOpen(struct MusicPlayerInfo*, struct MusicPlayerTrack*, u8);
extern void
MPlayStart(struct MusicPlayerInfo*, struct SongHeader*);
extern void
MPlayContinue(struct MusicPlayerInfo*);
extern void
MPlayFadeOut(struct MusicPlayerInfo*, u16);
extern void
TrackStop(struct MusicPlayerInfo*, struct MusicPlayerTrack*);
extern void
DummyFunc(void);
extern void
ply_note(u32, struct MusicPlayerInfo*, struct MusicPlayerTrack*);

static struct MusicPlayerTrack sMPlayTrackBgm[10];
static struct MusicPlayerTrack sMPlayTrackSe1[3];
static struct MusicPlayerTrack sMPlayTrackSe2[9];
static struct MusicPlayerTrack sMPlayTrackSe3[1];

const struct ToneData voicegroup000 = { 0 };
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
static uint16_t sCryPitch;
static uint16_t sCryLength;
static uint8_t sCryRelease;
static uint8_t sCryVolume;
static uint8_t sCryPriority;
static int8_t sCryPan;
static int8_t sCryChorus;
static const struct SongHeader* sObservedSeHeaders[3];
static u32 sObservedSeClocks[3];
static bool8 sObservedSeActive[3];
static bool8 sObservedCryActive;
static uint32_t sObservedCryFramesPlayed;

static int16_t
pfr_clamp16(int32_t v)
{
  if (v < -32768) {
    return -32768;
  }
  if (v > 32767) {
    return 32767;
  }
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
      s32 nextSample;
      u32 advance;

      if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON)) {
        continue;
      }
      if (chan->statusFlags & SOUND_CHANNEL_SF_START) {
        continue;
      }
      if (chan->wav == NULL || chan->currentPointer == NULL) {
        continue;
      }

      sample = *chan->currentPointer;
      nextSample = sample;

      if (!(chan->type & TONEDATA_TYPE_FIX)) {
        if (chan->type & TONEDATA_TYPE_REV) {
          if (chan->count > 1) {
            nextSample = chan->currentPointer[-1];
          }
        } else {
          if (chan->count > 1) {
            nextSample = chan->currentPointer[1];
          }
        }

        sample += ((s32)chan->fw * (nextSample - sample)) / (s32)si->pcmFreq;
      }

      mixL += (sample * chan->leftVolume * chan->envelopeVolumeLeft) >> 8;
      mixR += (sample * chan->rightVolume * chan->envelopeVolumeRight) >> 8;

      if (chan->type & TONEDATA_TYPE_FIX) {
        advance = 1;
      } else {
        chan->fw += chan->frequency;
        advance = chan->fw / (u32)si->pcmFreq;
        chan->fw %= (u32)si->pcmFreq;
      }

      if (advance > 0) {
        if (advance < chan->count) {
          if (chan->type & TONEDATA_TYPE_REV) {
            chan->currentPointer -= advance;
          } else {
            chan->currentPointer += advance;
          }
          chan->count -= advance;
        } else {
          if (chan->statusFlags & SOUND_CHANNEL_SF_LOOP) {
            if (chan->type & TONEDATA_TYPE_REV) {
              chan->currentPointer = (s8*)chan->wav->data + chan->wav->size - 1;
              chan->count = chan->wav->size - chan->wav->loopStart;
            } else {
              chan->currentPointer =
                (s8*)chan->wav->data + chan->wav->loopStart;
              chan->count = chan->wav->size - chan->wav->loopStart;
            }
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

static double
pfr_audio_cgb_square_step(u32 freq)
{
  u32 reg = freq & 0x7FFu;
  u32 denom = 2048u - reg;

  if (denom == 0) {
    return 0.0;
  }

  return 131072.0 / (double)denom / (double)PFR_DEFAULT_AUDIO_SAMPLE_RATE;
}

static double
pfr_audio_cgb_wave_step(u32 freq)
{
  u32 reg = freq & 0x7FFu;
  u32 denom = 2048u - reg;

  if (denom == 0) {
    return 0.0;
  }

  return (65536.0 * 32.0) / (double)denom /
         (double)PFR_DEFAULT_AUDIO_SAMPLE_RATE;
}

static double
pfr_audio_cgb_noise_step(u32 freq)
{
  u32 ratio_index = freq & 0x7u;
  u32 shift = (freq >> 4) & 0xFu;
  double divisor = ratio_index == 0 ? 8.0 : (double)ratio_index * 16.0;

  return 524288.0 / divisor / (double)(1u << (shift + 1)) /
         (double)PFR_DEFAULT_AUDIO_SAMPLE_RATE;
}

static u32*
pfr_audio_cgb_phase_ptr(struct CgbChannel* chan)
{
  return (u32*)&chan->dummy4[0];
}

static u32*
pfr_audio_cgb_aux_ptr(struct CgbChannel* chan)
{
  return (u32*)&chan->dummy4[4];
}

static u8
pfr_audio_cgb_wave_volume(u8 envelopeVolume)
{
  switch (gCgb3Vol[envelopeVolume & 0x0Fu]) {
    case 0x20:
      return 16;
    case 0x40:
      return 8;
    case 0x60:
      return 4;
    case 0x80:
      return 12;
    default:
      return 0;
  }
}

static void
pfr_audio_mix_cgb_channels(struct SoundInfo* si,
                           int16_t* output,
                           u32 numSamples)
{
  u32 frame;

  if (si->cgbChans == NULL) {
    return;
  }

  for (frame = 0; frame < numSamples; frame++) {
    s32 mixL = output[frame * 2 + 0];
    s32 mixR = output[frame * 2 + 1];
    u32 ch;

    for (ch = 0; ch < 4; ch++) {
      struct CgbChannel* chan = &si->cgbChans[ch];
      double step;
      s32 sample = 0;
      s32 gain;
      u8 envelopeVolume = chan->envelopeVolume;

      if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON)) {
        continue;
      }

      switch (chan->type & TONEDATA_TYPE_CGB) {
        case PFR_AUDIO_VOICE_SQUARE1:
        case PFR_AUDIO_VOICE_SQUARE2: {
          static const double sDutyCycles[] = { 0.125, 0.25, 0.5, 0.75 };
          double duty = sDutyCycles[(uintptr_t)chan->wavePointer & 0x3u];
          double phase;

          step = pfr_audio_cgb_square_step(chan->frequency);
          phase =
            ((double)(*pfr_audio_cgb_phase_ptr(chan) & 0xFFFFu) / 65536.0) +
            step;
          phase -= floor(phase);
          *pfr_audio_cgb_phase_ptr(chan) =
            (*pfr_audio_cgb_phase_ptr(chan) & 0xFFFF0000u) |
            (u32)(phase * 65536.0);
          sample = phase < duty ? 108 : -108;
          break;
        }
        case PFR_AUDIO_VOICE_PROGRAMMABLE_WAVE: {
          const s8* wave =
            (const s8*)(chan->currentPointer != NULL ? chan->currentPointer
                                                     : chan->wavePointer);
          u32 wave_length = 32;
          double phase;

          if (wave == NULL) {
            continue;
          }

          step = pfr_audio_cgb_wave_step(chan->frequency);
          phase =
            ((double)(*pfr_audio_cgb_phase_ptr(chan) & 0xFFFFu) / 65536.0) +
            step;
          *pfr_audio_cgb_phase_ptr(chan) = (u32)(phase * 65536.0);
          sample = wave[((u32)phase) % wave_length];
          break;
        }
        case PFR_AUDIO_VOICE_NOISE: {
          double phase =
            ((double)(*pfr_audio_cgb_phase_ptr(chan) & 0xFFFFu) / 65536.0) +
            pfr_audio_cgb_noise_step(chan->frequency);

          while (phase >= 1.0) {
            u32* noise_state = pfr_audio_cgb_aux_ptr(chan);
            u32 bit;

            phase -= 1.0;
            if ((*noise_state & 0x7FFFu) == 0) {
              *noise_state = 0x7FFFu;
            }

            bit = ((*noise_state ^ (*noise_state >> 1)) & 0x1u);
            *noise_state = (*noise_state >> 1) | (bit << 14);
            if (((uintptr_t)chan->wavePointer & 0x1u) != 0) {
              *noise_state = (*noise_state & ~(1u << 6)) | (bit << 6);
            }
          }

          *pfr_audio_cgb_phase_ptr(chan) = (u32)(phase * 65536.0);
          sample = ((*pfr_audio_cgb_aux_ptr(chan) & 0x1u) == 0) ? 108 : -108;
          break;
        }
        default:
          continue;
      }

      if ((chan->type & TONEDATA_TYPE_CGB) ==
          PFR_AUDIO_VOICE_PROGRAMMABLE_WAVE) {
        envelopeVolume = pfr_audio_cgb_wave_volume(chan->envelopeVolume);
      }

      gain = ((s32)(si->masterVolume + 1) * (s32)envelopeVolume + 15) / 16;
      mixR += (sample * chan->rightVolume * gain) / 15;
      mixL += (sample * chan->leftVolume * gain) / 15;
    }

    output[frame * 2 + 0] = pfr_clamp16(mixL);
    output[frame * 2 + 1] = pfr_clamp16(mixR);
  }
}

void
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

  if (!sCryRuntime.active || sCryRuntime.asset == NULL) {
    return;
  }

  asset_sample = sCryRuntime.asset->sample;
  if (asset_sample == NULL || asset_sample->sample_count == 0) {
    pfr_audio_runtime_stop_cry();
    return;
  }

  clamped = sCryStereo ? sCryRuntime.pan : 0;
  if (clamped < -64) {
    clamped = -64;
  }
  if (clamped > 63) {
    clamped = 63;
  }
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
  MPlayExtender(gCgbChans);

  MPlayOpen(&gMPlayInfo_BGM, sMPlayTrackBgm, 10);
  MPlayOpen(&gMPlayInfo_SE1, sMPlayTrackSe1, 3);
  MPlayOpen(&gMPlayInfo_SE2, sMPlayTrackSe2, 9);
  MPlayOpen(&gMPlayInfo_SE3, sMPlayTrackSe3, 1);
  gMPlayInfo_BGM.unk_B = (u8)gMPlayTable[0].unk_A;
  gMPlayInfo_SE1.unk_B = (u8)gMPlayTable[1].unk_A;
  gMPlayInfo_SE2.unk_B = (u8)gMPlayTable[2].unk_A;
  gMPlayInfo_SE3.unk_B = (u8)gMPlayTable[3].unk_A;
  gMPlayInfo_BGM.memAccArea = gMPlayMemAccArea;
  gMPlayInfo_SE1.memAccArea = gMPlayMemAccArea;
  gMPlayInfo_SE2.memAccArea = gMPlayMemAccArea;
  gMPlayInfo_SE3.memAccArea = gMPlayMemAccArea;

  memcpy(&gPokemonCrySong, &gPokemonCrySongTemplate, sizeof(gPokemonCrySong));
  memset(
    gPokemonCrySongs, 0, sizeof(struct PokemonCrySong) * MAX_POKEMON_CRIES);
  memset(gPokemonCryMusicPlayers,
         0,
         sizeof(struct MusicPlayerInfo) * MAX_POKEMON_CRIES);
  memset(gPokemonCryTracks,
         0,
         sizeof(struct MusicPlayerTrack) * MAX_POKEMON_CRIES * 2);
  MPlayOpen(&gPokemonCryMusicPlayers[0], &gPokemonCryTracks[0], 2);
  MPlayOpen(&gPokemonCryMusicPlayers[1], &gPokemonCryTracks[2], 2);
  gPokemonCryTracks[0].chan = 0;
  gPokemonCryTracks[2].chan = 0;
  memset(&sCryRuntime, 0, sizeof(sCryRuntime));

  sVSyncEnabled = FALSE;
  sCryStereo = FALSE;
  sCryPitch = PFR_CRY_DEFAULT_PITCH;
  sCryLength = PFR_CRY_DEFAULT_LENGTH;
  sCryRelease = 0;
  sCryVolume = CRY_VOLUME;
  sCryPriority = CRY_PRIORITY_NORMAL;
  sCryPan = 0;
  sCryChorus = 0;
  memset((void*)sObservedSeHeaders, 0, sizeof(sObservedSeHeaders));
  memset(sObservedSeClocks, 0, sizeof(sObservedSeClocks));
  memset(sObservedSeActive, 0, sizeof(sObservedSeActive));
  sObservedCryActive = FALSE;
  sObservedCryFramesPlayed = 0;
}

void
m4aSoundMain(void)
{
  int16_t output[PFR_AUDIO_FRAMES_PER_VBLANK * PFR_AUDIO_CHANNEL_COUNT];

  SoundMain();

  pfr_audio_mix_channels(&gSoundInfo, output, PFR_AUDIO_FRAMES_PER_VBLANK);
  pfr_audio_mix_cgb_channels(&gSoundInfo, output, PFR_AUDIO_FRAMES_PER_VBLANK);
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
  memset(
    gPokemonCrySongs, 0, sizeof(struct PokemonCrySong) * MAX_POKEMON_CRIES);
  pfr_audio_runtime_stop_cry();
}

u16
pfr_stub_current_bgm(void)
{
  const PfrAudioSongAsset* asset =
    pfr_audio_song_asset_for_header(gMPlayInfo_BGM.songHeader);

  return asset == NULL ? 0 : asset->song_id;
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
  const struct MusicPlayerInfo* sePlayers[] = {
    &gMPlayInfo_SE1,
    &gMPlayInfo_SE2,
    &gMPlayInfo_SE3,
  };
  u32 i;

  for (i = 0; i < ARRAY_COUNT(sePlayers); i++) {
    const struct MusicPlayerInfo* mplay = sePlayers[i];
    const struct SongHeader* header = mplay->songHeader;
    bool8 active =
      (header != NULL) && ((mplay->status & MUSICPLAYER_STATUS_PAUSE) == 0);

    if (!active) {
      sObservedSeHeaders[i] = header;
      sObservedSeClocks[i] = mplay->clock;
      sObservedSeActive[i] = FALSE;
      continue;
    }

    if (!sObservedSeActive[i] || header != sObservedSeHeaders[i] ||
        mplay->clock < sObservedSeClocks[i]) {
      const PfrAudioSongAsset* asset = pfr_audio_song_asset_for_header(header);

      sObservedSeHeaders[i] = header;
      sObservedSeClocks[i] = mplay->clock;
      sObservedSeActive[i] = TRUE;
      if (asset != NULL) {
        *songNum = asset->song_id;
        return TRUE;
      }
    }

    sObservedSeClocks[i] = mplay->clock;
  }

  return FALSE;
}

bool8
pfr_stub_take_cry(void)
{
  bool8 active = sCryRuntime.active;
  bool8 started = active && (!sObservedCryActive || sCryRuntime.frames_played <
                                                      sObservedCryFramesPlayed);

  sObservedCryActive = active;
  sObservedCryFramesPlayed = sCryRuntime.frames_played;
  return started;
}
