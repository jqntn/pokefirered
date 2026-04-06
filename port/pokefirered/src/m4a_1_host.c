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
#include "gba/io_reg.h"
#include "gba/m4a_internal.h"
#include "global.h"
#include "m4a_1_host.h"
#include "m4a.h"
#include "main.h"

extern const u8 gCgb3Vol[];

enum
{
  // Convert the 4-bit PSG envelope domain into a 16-bit host mix domain.
  // The final quarter/half/full hardware mix ratio from SOUNDCNT_H is applied
  // separately in the PSG mixer.
  PFR_AUDIO_CGB_LEVEL_SCALE = 256,
};

extern void
SoundMain(void);
extern void
MPlayJumpTableCopy(MPlayFunc* dest);
extern void
MPlayOpen(struct MusicPlayerInfo*, struct MusicPlayerTrack*, u8);
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

static const struct SongHeader* sObservedSeHeaders[3];
static u32 sObservedSeClocks[3];
static bool8 sObservedSeActive[3];
static u32 sObservedCryClocks[MAX_POKEMON_CRIES];
static bool8 sObservedCryActive[MAX_POKEMON_CRIES];
static int32_t sPfrOutCapLeft;
static int32_t sPfrOutCapRight;
static int32_t sPfrOutHpfQ16;

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

static int8_t
pfr_clamp8(int32_t v)
{
  if (v < -128) {
    return -128;
  }
  if (v > 127) {
    return 127;
  }
  return (int8_t)v;
}

static int32_t
pfr_audio_output_hpf_q16(u32 sampleRate)
{
  double coeff;

  if (sampleRate == 0) {
    return 0;
  }

  // Pan Docs models the GBA output HPF using 0.999958^(4194304 / rate).
  coeff = pow(0.999958, 4194304.0 / (double)sampleRate);
  if (coeff < 0.0) {
    coeff = 0.0;
  } else if (coeff > 1.0) {
    coeff = 1.0;
  }

  return (int32_t)(coeff * 65536.0 + 0.5);
}

static s32
pfr_audio_song_id_for_header(const struct SongHeader* song_header)
{
  size_t i;

  if (song_header == NULL) {
    return -1;
  }

  for (i = 0; i < gPfrAudioSongAssetCount; i++) {
    if (gSongTable[i].header == song_header) {
      return (s32)i;
    }
  }

  return -1;
}

const PfrAudioSongAsset*
pfr_audio_song_asset_for_header(const struct SongHeader* song_header)
{
  s32 song_id = pfr_audio_song_id_for_header(song_header);

  if (song_id < 0) {
    return NULL;
  }

  return &gPfrAudioSongAssets[song_id];
}

u8*
pfr_m4a_host_resolve_pointer(struct MusicPlayerInfo* mplayInfo,
                             struct MusicPlayerTrack* track)
{
  const struct PokemonCrySong* crySong = NULL;
  const PfrAudioSongAsset* asset;
  u32 offset;
  u32 i;

  if ((uintptr_t)mplayInfo->songHeader >= (uintptr_t)&gPokemonCrySongs[0] &&
      (uintptr_t)mplayInfo->songHeader <
        (uintptr_t)(&gPokemonCrySongs[MAX_POKEMON_CRIES])) {
    crySong = (const struct PokemonCrySong*)mplayInfo->songHeader;
  }

  if (crySong != NULL &&
      track->cmdPtr == (u8*)(uintptr_t)&crySong->gotoTarget) {
    return (u8*)(uintptr_t)&crySong->cont[0];
  }

  asset = pfr_audio_song_asset_for_header(mplayInfo->songHeader);
  if (asset == NULL) {
    return track->cmdPtr;
  }

  for (i = 0; i < mplayInfo->songHeader->trackCount; i++) {
    const PfrAudioTrackAsset* trackAsset = &asset->tracks[i];

    if (track->cmdPtr < trackAsset->data ||
        track->cmdPtr >= trackAsset->data + trackAsset->length) {
      continue;
    }

    offset = (u32)(track->cmdPtr - trackAsset->data);
    for (u32 relocationIndex = 0;
         relocationIndex < trackAsset->relocation_count;
         relocationIndex++) {
      if (trackAsset->relocations[relocationIndex].offset == offset) {
        return (u8*)(trackAsset->data +
                     trackAsset->relocations[relocationIndex].target_offset);
      }
    }
    break;
  }

  return track->cmdPtr;
}

const u8*
pfr_m4a_host_keysplit_table(const struct ToneData* tone)
{
  u32 offset = (u32)tone->attack | ((u32)tone->decay << 8) |
               ((u32)tone->sustain << 16) | ((u32)tone->release << 24);

  return &gPfrKeysplitBlob[offset];
}

const PfrAudioSongAsset*
pfr_audio_song_asset_for_id(u16 song_id)
{
  if (song_id >= gPfrAudioSongAssetCount) {
    return NULL;
  }

  return &gPfrAudioSongAssets[song_id];
}

u32
MidiKeyToCgbFreq(u8 chanNum, u8 key, u8 fineAdjust)
{
  if (chanNum == PFR_AUDIO_VOICE_NOISE) {
    if (key <= 20) {
      key = 0;
    } else {
      key -= 21;
      if (key > 59) {
        key = 59;
      }
    }

    return gNoiseTable[key];
  } else {
    s32 val1;
    s32 val2;

    if (key <= 35) {
      fineAdjust = 0;
      key = 0;
    } else {
      key -= 36;
      if (key > 130) {
        key = 130;
        fineAdjust = 255;
      }
    }

    val1 = gCgbScaleTable[key];
    val1 = gCgbFreqTable[val1 & 0xF] >> (val1 >> 4);

    val2 = gCgbScaleTable[key + 1];
    val2 = gCgbFreqTable[val2 & 0xF] >> (val2 >> 4);

    return (u32)(val1 + ((fineAdjust * (val2 - val1)) >> 8) + 2048);
  }
}

void
CgbOscOff(u8 chanNum)
{
  switch (chanNum) {
    case 1:
      REG_NR12 = 8;
      REG_NR14 = 0x80;
      break;
    case 2:
      REG_NR22 = 8;
      REG_NR24 = 0x80;
      break;
    case 3:
      REG_NR30 = 0;
      break;
    default:
      REG_NR42 = 8;
      REG_NR44 = 0x80;
      break;
  }
}

static int
pfr_port_cgb_pan(struct CgbChannel* chan)
{
  u32 rightVolume = chan->rightVolume;
  u32 leftVolume = chan->leftVolume;

  if ((u8)rightVolume >= (u8)leftVolume) {
    if (rightVolume / 2 >= leftVolume) {
      chan->pan = 0x0F;
      return 1;
    }
  } else if (leftVolume / 2 >= rightVolume) {
    chan->pan = 0xF0;
    return 1;
  }

  return 0;
}

void
CgbModVol(struct CgbChannel* chan)
{
  struct SoundInfo* soundInfo = SOUND_INFO_PTR;

  chan->envelopeGoal = (u8)((chan->leftVolume + chan->rightVolume) / 16);
  if (!(soundInfo->mode & 1) && pfr_port_cgb_pan(chan)) {
    if (chan->envelopeGoal > 15) {
      chan->envelopeGoal = 15;
    }
  } else {
    chan->pan = 0xFF;
  }

  chan->sustainGoal = (u8)((chan->envelopeGoal * chan->sustain + 15) >> 4);
  chan->pan &= chan->panMask;
}

void
CgbSound(void)
{
  s32 ch;
  struct CgbChannel* channels;
  s32 prevC15;
  struct SoundInfo* soundInfo = SOUND_INFO_PTR;
  vu8* nrx0ptr;
  vu8* nrx1ptr;
  vu8* nrx2ptr;
  vu8* nrx3ptr;
  vu8* nrx4ptr;
  s32 envelopeStepTimeAndDir;
  int mask = 0xFF;

  if (soundInfo->c15 != 0) {
    soundInfo->c15--;
  } else {
    soundInfo->c15 = 14;
  }

  for (ch = 1, channels = soundInfo->cgbChans; ch <= 4; ch++, channels++) {
    if (!(channels->statusFlags & SOUND_CHANNEL_SF_ON)) {
      continue;
    }

    switch (ch) {
      case 1:
        nrx0ptr = (vu8*)REG_ADDR_NR10;
        nrx1ptr = (vu8*)REG_ADDR_NR11;
        nrx2ptr = (vu8*)REG_ADDR_NR12;
        nrx3ptr = (vu8*)REG_ADDR_NR13;
        nrx4ptr = (vu8*)REG_ADDR_NR14;
        break;
      case 2:
        nrx0ptr = (vu8*)(REG_ADDR_NR10 + 1);
        nrx1ptr = (vu8*)REG_ADDR_NR21;
        nrx2ptr = (vu8*)REG_ADDR_NR22;
        nrx3ptr = (vu8*)REG_ADDR_NR23;
        nrx4ptr = (vu8*)REG_ADDR_NR24;
        break;
      case 3:
        nrx0ptr = (vu8*)REG_ADDR_NR30;
        nrx1ptr = (vu8*)REG_ADDR_NR31;
        nrx2ptr = (vu8*)REG_ADDR_NR32;
        nrx3ptr = (vu8*)REG_ADDR_NR33;
        nrx4ptr = (vu8*)REG_ADDR_NR34;
        break;
      default:
        nrx0ptr = (vu8*)(REG_ADDR_NR30 + 1);
        nrx1ptr = (vu8*)REG_ADDR_NR41;
        nrx2ptr = (vu8*)REG_ADDR_NR42;
        nrx3ptr = (vu8*)REG_ADDR_NR43;
        nrx4ptr = (vu8*)REG_ADDR_NR44;
        break;
    }

    prevC15 = soundInfo->c15;
    envelopeStepTimeAndDir = *nrx2ptr;

    if (channels->statusFlags & SOUND_CHANNEL_SF_START) {
      if (!(channels->statusFlags & SOUND_CHANNEL_SF_STOP)) {
        channels->statusFlags = SOUND_CHANNEL_SF_ENV_ATTACK;
        channels->modify = CGB_CHANNEL_MO_PIT | CGB_CHANNEL_MO_VOL;
        CgbModVol(channels);
        switch (ch) {
          case 1:
            *nrx0ptr = channels->sweep;
            /* fallthrough */
          case 2:
            *nrx1ptr = (u8)(((u32)(uintptr_t)channels->wavePointer << 6) +
                            channels->length);
            goto init_env_step_time_dir;
          case 3:
            if (channels->wavePointer != channels->currentPointer) {
              *nrx0ptr = 0x40;
              REG_WAVE_RAM0 = channels->wavePointer[0];
              REG_WAVE_RAM1 = channels->wavePointer[1];
              REG_WAVE_RAM2 = channels->wavePointer[2];
              REG_WAVE_RAM3 = channels->wavePointer[3];
              channels->currentPointer = channels->wavePointer;
            }
            *nrx0ptr = 0;
            *nrx1ptr = channels->length;
            if (channels->length != 0) {
              channels->n4 = 0xC0;
            } else {
              channels->n4 = 0x80;
            }
            break;
          default:
            *nrx1ptr = (u8)channels->length;
            *nrx3ptr = (u8)((u32)(uintptr_t)channels->wavePointer << 3);
          init_env_step_time_dir:
            envelopeStepTimeAndDir =
              channels->attack + CGB_NRx2_ENV_DIR_INC;
            if (channels->length != 0) {
              channels->n4 = 0x40;
            } else {
              channels->n4 = 0x00;
            }
            break;
        }
        channels->envelopeCounter = channels->attack;
        if ((s8)(channels->attack & mask) != 0) {
          channels->envelopeVolume = 0;
          goto envelope_step_complete;
        } else {
          goto envelope_decay_start;
        }
      } else {
        goto oscillator_off;
      }
    } else if (channels->statusFlags & SOUND_CHANNEL_SF_IEC) {
      channels->pseudoEchoLength--;
      if ((s8)(channels->pseudoEchoLength & mask) <= 0) {
      oscillator_off:
        CgbOscOff((u8)ch);
        channels->statusFlags = 0;
        goto channel_complete;
      }
      goto envelope_complete;
    } else if ((channels->statusFlags & SOUND_CHANNEL_SF_STOP) &&
               (channels->statusFlags & SOUND_CHANNEL_SF_ENV)) {
      channels->statusFlags &= ~SOUND_CHANNEL_SF_ENV;
      channels->envelopeCounter = channels->release;
      if ((s8)(channels->release & mask) != 0) {
        channels->modify |= CGB_CHANNEL_MO_VOL;
        if (ch != 3) {
          envelopeStepTimeAndDir =
            channels->release | CGB_NRx2_ENV_DIR_DEC;
        }
        goto envelope_step_complete;
      } else {
        goto envelope_pseudoecho_start;
      }
    } else {
    envelope_step_repeat:
      if (channels->envelopeCounter == 0) {
        if (ch == 3) {
          channels->modify |= CGB_CHANNEL_MO_VOL;
        }

        CgbModVol(channels);
        if ((channels->statusFlags & SOUND_CHANNEL_SF_ENV) ==
            SOUND_CHANNEL_SF_ENV_RELEASE) {
          channels->envelopeVolume--;
          if ((s8)(channels->envelopeVolume & mask) <= 0) {
          envelope_pseudoecho_start:
            channels->envelopeVolume =
              ((channels->envelopeGoal * channels->pseudoEchoVolume) + 0xFF) >>
              8;
            if (channels->envelopeVolume != 0) {
              channels->statusFlags |= SOUND_CHANNEL_SF_IEC;
              channels->modify |= CGB_CHANNEL_MO_VOL;
              if (ch != 3) {
                envelopeStepTimeAndDir = CGB_NRx2_ENV_DIR_INC;
              }
              goto envelope_complete;
            } else {
              goto oscillator_off;
            }
          } else {
            channels->envelopeCounter = channels->release;
          }
        } else if ((channels->statusFlags & SOUND_CHANNEL_SF_ENV) ==
                   SOUND_CHANNEL_SF_ENV_SUSTAIN) {
        envelope_sustain:
          channels->envelopeVolume = channels->sustainGoal;
          channels->envelopeCounter = 7;
        } else if ((channels->statusFlags & SOUND_CHANNEL_SF_ENV) ==
                   SOUND_CHANNEL_SF_ENV_DECAY) {
          int envelopeVolume;
          int sustainGoal;

          channels->envelopeVolume--;
          envelopeVolume = (s8)(channels->envelopeVolume & mask);
          sustainGoal = (s8)channels->sustainGoal;
          if (envelopeVolume <= sustainGoal) {
          envelope_sustain_start:
            if (channels->sustain == 0) {
              channels->statusFlags &= ~SOUND_CHANNEL_SF_ENV;
              goto envelope_pseudoecho_start;
            } else {
              channels->statusFlags--;
              channels->modify |= CGB_CHANNEL_MO_VOL;
              if (ch != 3) {
                envelopeStepTimeAndDir = CGB_NRx2_ENV_DIR_INC;
              }
              goto envelope_sustain;
            }
          } else {
            channels->envelopeCounter = channels->decay;
          }
        } else {
          channels->envelopeVolume++;
          if ((u8)(channels->envelopeVolume & mask) >= channels->envelopeGoal) {
          envelope_decay_start:
            channels->statusFlags--;
            channels->envelopeCounter = channels->decay;
            if ((u8)(channels->envelopeCounter & mask) != 0) {
              channels->modify |= CGB_CHANNEL_MO_VOL;
              channels->envelopeVolume = channels->envelopeGoal;
              if (ch != 3) {
                envelopeStepTimeAndDir =
                  channels->decay | CGB_NRx2_ENV_DIR_DEC;
              }
            } else {
              goto envelope_sustain_start;
            }
          } else {
            channels->envelopeCounter = channels->attack;
          }
        }
      }
    }

  envelope_step_complete:
    channels->envelopeCounter--;
    if (prevC15 == 0) {
      prevC15--;
      goto envelope_step_repeat;
    }

  envelope_complete:
    if (channels->modify & CGB_CHANNEL_MO_PIT) {
      if (ch < 4 && (channels->type & TONEDATA_TYPE_FIX)) {
        int dacPwmRate = REG_SOUNDBIAS_H;

        if (dacPwmRate < 0x40) {
          channels->frequency = (channels->frequency + 2) & 0x7FC;
        } else if (dacPwmRate < 0x80) {
          channels->frequency = (channels->frequency + 1) & 0x7FE;
        }
      }

      if (ch != 4) {
        *nrx3ptr = (u8)channels->frequency;
      } else {
        *nrx3ptr = (u8)((*nrx3ptr & 0x08) | channels->frequency);
      }
      channels->n4 = (channels->n4 & 0xC0) +
                     (*((u8*)(&channels->frequency) + 1));
      *nrx4ptr = (s8)(channels->n4 & mask);
    }

    if (channels->modify & CGB_CHANNEL_MO_VOL) {
      REG_NR51 = (REG_NR51 & ~channels->panMask) | channels->pan;
      if (ch == 3) {
        *nrx2ptr = gCgb3Vol[channels->envelopeVolume];
        if (channels->n4 & 0x80) {
          *nrx0ptr = 0x80;
          *nrx4ptr = channels->n4;
          channels->n4 &= 0x7F;
        }
      } else {
        u32 envMask = 0xF;

        *nrx2ptr = (u8)((envelopeStepTimeAndDir & envMask) +
                        (channels->envelopeVolume << 4));
        *nrx4ptr = (u8)(channels->n4 | 0x80);
        if (ch == 1 && !(*nrx0ptr & 0x08)) {
          *nrx4ptr = (u8)(channels->n4 | 0x80);
        }
      }
    }

  channel_complete:
    channels->modify = 0;
  }
}

void
MPlayExtender(struct CgbChannel* cgbChans)
{
  struct SoundInfo* soundInfo = SOUND_INFO_PTR;

  REG_SOUNDCNT_X =
    SOUND_MASTER_ENABLE | SOUND_4_ON | SOUND_3_ON | SOUND_2_ON | SOUND_1_ON;
  REG_SOUNDCNT_L = 0;
  REG_NR12 = 0x8;
  REG_NR22 = 0x8;
  REG_NR42 = 0x8;
  REG_NR14 = 0x80;
  REG_NR24 = 0x80;
  REG_NR44 = 0x80;
  REG_NR30 = 0;
  REG_NR50 = 0x77;

  if (soundInfo->ident != ID_NUMBER) {
    return;
  }

  soundInfo->ident++;
  gMPlayJumpTable[8] = (MPlayFunc)ply_memacc;
  gMPlayJumpTable[17] = (MPlayFunc)ply_lfos;
  gMPlayJumpTable[19] = (MPlayFunc)ply_mod;
  gMPlayJumpTable[28] = (MPlayFunc)ply_xcmd;
  gMPlayJumpTable[29] = (MPlayFunc)ply_endtie;
  gMPlayJumpTable[30] = (MPlayFunc)SampleFreqSet;
  gMPlayJumpTable[31] = (MPlayFunc)TrackStop;
  gMPlayJumpTable[32] = (MPlayFunc)FadeOutBody;
  gMPlayJumpTable[33] = (MPlayFunc)TrkVolPitSet;
  soundInfo->cgbChans = cgbChans;
  soundInfo->CgbSound = CgbSound;
  soundInfo->CgbOscOff = CgbOscOff;
  soundInfo->MidiKeyToCgbFreq = MidiKeyToCgbFreq;
  soundInfo->maxLines = (u8)gMaxLines[0];

  memset(cgbChans, 0, sizeof(struct CgbChannel) * 4);

  cgbChans[0].type = PFR_AUDIO_VOICE_SQUARE1;
  cgbChans[0].panMask = 0x11;
  cgbChans[1].type = PFR_AUDIO_VOICE_SQUARE2;
  cgbChans[1].panMask = 0x22;
  cgbChans[2].type = PFR_AUDIO_VOICE_PROGRAMMABLE_WAVE;
  cgbChans[2].panMask = 0x44;
  cgbChans[3].type = PFR_AUDIO_VOICE_NOISE;
  cgbChans[3].panMask = 0x88;

  soundInfo->ident = ID_NUMBER;
}

static void
pfr_audio_driver_pcm_to_output(const struct SoundInfo* si,
                               u32 chunkOffset,
                               int16_t* output,
                               u32 numSamples)
{
  const s8* fifoA = &si->pcmBuffer[chunkOffset];
  const s8* fifoB = &si->pcmBuffer[PCM_DMA_BUF_SIZE + chunkOffset];
  u16 soundcnt_h = REG_SOUNDCNT_H;
  u32 frame;

  for (frame = 0; frame < numSamples; frame++) {
    s32 sampleA = fifoA[frame];
    s32 sampleB = fifoB[frame];
    s32 outL = 0;
    s32 outR = 0;

    if ((soundcnt_h & SOUND_A_MIX_FULL) == 0) {
      sampleA /= 2;
    }
    if ((soundcnt_h & SOUND_B_MIX_FULL) == 0) {
      sampleB /= 2;
    }

    if (soundcnt_h & SOUND_A_LEFT_OUTPUT) {
      outL += sampleA;
    }
    if (soundcnt_h & SOUND_A_RIGHT_OUTPUT) {
      outR += sampleA;
    }
    if (soundcnt_h & SOUND_B_LEFT_OUTPUT) {
      outL += sampleB;
    }
    if (soundcnt_h & SOUND_B_RIGHT_OUTPUT) {
      outR += sampleB;
    }

    output[frame * 2 + 0] = (int16_t)pfr_clamp8(outL) << 8;
    output[frame * 2 + 1] = (int16_t)pfr_clamp8(outR) << 8;
  }
}

static u32
pfr_audio_cgb_square_step(u32 freq)
{
  u32 reg = freq & 0x7FFu;
  u32 denom = 2048u - reg;

  if (denom == 0) {
    return 0;
  }

  return (u32)((1048576.0 * 65536.0) / (double)denom /
               (double)PFR_DEFAULT_AUDIO_SAMPLE_RATE);
}

static u32
pfr_audio_cgb_wave_step(u32 freq)
{
  u32 reg = freq & 0x7FFu;
  u32 denom = 2048u - reg;

  if (denom == 0) {
    return 0;
  }

  return (u32)(((65536.0 * 32.0) * 65536.0) / (double)denom /
               (double)PFR_DEFAULT_AUDIO_SAMPLE_RATE);
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
pfr_audio_cgb_square_sample(struct CgbChannel* chan)
{
  static const u8 sDutyPatterns[4][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 1 },
    { 1, 0, 0, 0, 0, 0, 0, 1 },
    { 1, 0, 0, 0, 0, 1, 1, 1 },
    { 0, 1, 1, 1, 1, 1, 1, 0 },
  };
  const u8* pattern = sDutyPatterns[(uintptr_t)chan->wavePointer & 0x3u];
  u32* phase_ptr = pfr_audio_cgb_phase_ptr(chan);
  double phase = (double)(*phase_ptr) / 65536.0;
  double step = (double)pfr_audio_cgb_square_step(chan->frequency) / 65536.0;
  double position = phase;
  double remaining = step;
  double highTime = 0.0;

  if (step <= 0.0) {
    return pattern[((u32)phase) & 0x7u] != 0 ? chan->envelopeVolume : 0;
  }

  while (remaining > 0.0) {
    double wrapped = fmod(position, 8.0);
    double chunk = ceil(wrapped) - wrapped;
    u32 dutyIndex;

    if (wrapped < 0.0) {
      wrapped += 8.0;
    }
    if (chunk <= 0.0) {
      chunk = 1.0;
    }
    if (chunk > remaining) {
      chunk = remaining;
    }

    dutyIndex = (u32)wrapped & 0x7u;
    if (pattern[dutyIndex] != 0) {
      highTime += chunk;
    }

    position += chunk;
    remaining -= chunk;
  }

  phase = fmod(phase + step, 8.0);
  if (phase < 0.0) {
    phase += 8.0;
  }
  *phase_ptr = (u32)(phase * 65536.0 + 0.5);
  return (u8)((highTime * chan->envelopeVolume) / step + 0.5);
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

static u8
pfr_audio_cgb_wave_sample_nibble(const u8* wave, u32 sampleIndex)
{
  u8 packed = wave[sampleIndex >> 1];
  u8 nibble;

  if ((sampleIndex & 1u) == 0) {
    nibble = packed >> 4;
  } else {
    nibble = packed & 0x0Fu;
  }

  return nibble;
}

static u8
pfr_audio_cgb_noise_advance(u32* noise_state, bool8 narrow)
{
  u32 lfsr = *noise_state;
  u32 coeff = narrow ? 0x4040u : 0x4000u;
  u32 high = (lfsr ^ (lfsr >> 1) ^ 1u) & 0x1u;

  lfsr >>= 1;
  if (high != 0) {
    lfsr |= coeff;
  } else {
    lfsr &= ~coeff;
  }
  lfsr &= 0x7FFFu;

  *noise_state = lfsr;
  return (u8)high;
}

static u8
pfr_audio_cgb_noise_sample(struct CgbChannel* chan)
{
  u32* phase_ptr = pfr_audio_cgb_phase_ptr(chan);
  u32* noise_state = pfr_audio_cgb_aux_ptr(chan);
  double phase = ((double)(*phase_ptr & 0xFFFFu) / 65536.0) +
                 pfr_audio_cgb_noise_step(chan->frequency);
  u32 steps = 0;
  u32 positive_steps = 0;
  u8 high = chan->dummy5 != 0;
  bool8 narrow = (((uintptr_t)chan->wavePointer & 0x1u) != 0);

  while (phase >= 1.0) {
    phase -= 1.0;
    high = pfr_audio_cgb_noise_advance(noise_state, narrow);
    positive_steps += high;
    steps++;
  }

  *phase_ptr = (u32)(phase * 65536.0);
  chan->dummy5 = high;

  if (steps == 0) {
    return high ? chan->envelopeVolume : 0;
  }

  return (u8)((positive_steps * chan->envelopeVolume + (steps / 2)) / steps);
}

static s32
pfr_audio_output_filter_sample(s32 sample, int32_t* cap)
{
  int64_t filtered = (int64_t)sample - (*cap >> 16);

  *cap = (int32_t)(((int64_t)sample << 16) - filtered * sPfrOutHpfQ16);
  return (s32)filtered;
}

static u32
pfr_audio_cgb_mix_gain(void)
{
  switch (REG_SOUNDCNT_H & 0x0003u) {
    case SOUND_CGB_MIX_HALF:
      return 2;
    case SOUND_CGB_MIX_FULL:
      return 4;
    case SOUND_CGB_MIX_QUARTER:
    default:
      return 1;
  }
}

static void
pfr_audio_mix_cgb_channels(struct SoundInfo* si,
                           int16_t* output,
                           u32 numSamples)
{
  u32 frame;
  u32 cgbMixGain = pfr_audio_cgb_mix_gain();

  if (si->cgbChans == NULL) {
    return;
  }

  for (frame = 0; frame < numSamples; frame++) {
    s32 mixL = output[frame * 2 + 0];
    s32 mixR = output[frame * 2 + 1];
    s32 psgL = 0;
    s32 psgR = 0;
    u32 ch;

    for (ch = 0; ch < 4; ch++) {
      struct CgbChannel* chan = &si->cgbChans[ch];
      u8 sample_level = 0;
      s32 contribution;
      bool enableRight;
      bool enableLeft;

      if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON)) {
        continue;
      }

      switch (chan->type & TONEDATA_TYPE_CGB) {
        case PFR_AUDIO_VOICE_SQUARE1:
        case PFR_AUDIO_VOICE_SQUARE2: {
          sample_level = pfr_audio_cgb_square_sample(chan);
          break;
        }
        case PFR_AUDIO_VOICE_PROGRAMMABLE_WAVE: {
          const u8* wave =
            (const u8*)(chan->currentPointer != NULL ? chan->currentPointer
                                                     : chan->wavePointer);
          u32 wave_length = 32;
          u32 phase = *pfr_audio_cgb_phase_ptr(chan);

          if (wave == NULL) {
            continue;
          }

          sample_level =
            (u8)((pfr_audio_cgb_wave_sample_nibble(
                    wave, (phase >> 16) & (wave_length - 1)) *
                    pfr_audio_cgb_wave_volume(chan->envelopeVolume) +
                  8) >>
                 4);
          phase += pfr_audio_cgb_wave_step(chan->frequency);
          phase %= (wave_length << 16);
          *pfr_audio_cgb_phase_ptr(chan) = phase;
          break;
        }
        case PFR_AUDIO_VOICE_NOISE:
          sample_level = pfr_audio_cgb_noise_sample(chan);
          break;
        default:
          continue;
      }

      enableRight = (chan->pan & (1u << ch)) != 0;
      enableLeft = (chan->pan & (0x10u << ch)) != 0;
      contribution =
        (s32)sample_level * PFR_AUDIO_CGB_LEVEL_SCALE * (s32)cgbMixGain;

      if (enableRight) {
        psgR += contribution;
      }
      if (enableLeft) {
        psgL += contribution;
      }
    }

    mixL += psgL;
    mixR += psgR;

    mixL = pfr_audio_output_filter_sample(mixL, &sPfrOutCapLeft);
    mixR = pfr_audio_output_filter_sample(mixR, &sPfrOutCapRight);

    output[frame * 2 + 0] = pfr_clamp16(mixL);
    output[frame * 2 + 1] = pfr_clamp16(mixR);
  }
}

static void
pfr_audio_expand_driver_mix(const int16_t* source,
                            u32 sourceFrames,
                            int16_t* output,
                            u32 outputFrames)
{
  u32 frame;

  if (source == NULL || output == NULL || sourceFrames == 0 ||
      outputFrames == 0) {
    return;
  }

  for (frame = 0; frame < outputFrames; frame++) {
    u32 sourceIndex = (u32)(((u64)frame * sourceFrames) / outputFrames);

    if (sourceIndex >= sourceFrames) {
      sourceIndex = sourceFrames - 1;
    }

    output[frame * 2 + 0] = source[sourceIndex * 2 + 0];
    output[frame * 2 + 1] = source[sourceIndex * 2 + 1];
  }
}

static u32
pfr_audio_driver_chunk_offset(const struct SoundInfo* soundInfo)
{
  if (soundInfo->pcmDmaCounter > 1) {
    return (u32)(soundInfo->pcmSamplesPerVBlank *
                 (soundInfo->pcmDmaPeriod - (soundInfo->pcmDmaCounter - 1)));
  }

  return 0;
}

void
SampleFreqSet(u32 freq)
{
  struct SoundInfo* soundInfo = &gSoundInfo;

  freq = (freq & SOUND_MODE_FREQ) >> SOUND_MODE_FREQ_SHIFT;
  if (freq == 0) {
    return;
  }

  soundInfo->freq = (u8)freq;
  soundInfo->pcmSamplesPerVBlank = (u8)gPcmSamplesPerVBlankTable[freq - 1];
  soundInfo->pcmDmaPeriod =
    (u8)(PCM_DMA_BUF_SIZE / soundInfo->pcmSamplesPerVBlank);
  soundInfo->pcmFreq = (597275 * soundInfo->pcmSamplesPerVBlank + 5000) / 10000;
  soundInfo->divFreq = (16777216 / soundInfo->pcmFreq + 1) >> 1;
}

void
m4aSoundMode(u32 mode)
{
  struct SoundInfo* soundInfo = &gSoundInfo;
  u32 temp;

  if (soundInfo->ident != ID_NUMBER) {
    return;
  }

  soundInfo->ident++;

  temp = mode & (SOUND_MODE_REVERB_SET | SOUND_MODE_REVERB_VAL);
  if (temp != 0) {
    soundInfo->reverb = (u8)(temp & SOUND_MODE_REVERB_VAL);
  }

  temp = mode & SOUND_MODE_MAXCHN;
  if (temp != 0) {
    struct SoundChannel* chan;

    soundInfo->maxChans = (u8)(temp >> SOUND_MODE_MAXCHN_SHIFT);

    temp = MAX_DIRECTSOUND_CHANNELS;
    chan = &soundInfo->chans[0];

    while (temp != 0) {
      chan->statusFlags = 0;
      temp--;
      chan++;
    }
  }

  temp = mode & SOUND_MODE_MASVOL;
  if (temp != 0) {
    soundInfo->masterVolume = (u8)(temp >> SOUND_MODE_MASVOL_SHIFT);
  }

  temp = mode & SOUND_MODE_DA_BIT;
  if (temp != 0) {
    temp = (temp & 0x300000) >> 14;
    REG_SOUNDBIAS_H = (u8)((REG_SOUNDBIAS_H & 0x3F) | temp);
  }

  temp = mode & SOUND_MODE_FREQ;
  if (temp != 0) {
    m4aSoundVSyncOff();
    SampleFreqSet(temp);
  }

  soundInfo->ident = ID_NUMBER;
}

void
m4aSoundInit(void)
{
  pfr_audio_reset();

  REG_SOUNDCNT_X =
    SOUND_MASTER_ENABLE | SOUND_4_ON | SOUND_3_ON | SOUND_2_ON | SOUND_1_ON;
  REG_SOUNDCNT_H = SOUND_B_FIFO_RESET | SOUND_B_TIMER_0 | SOUND_B_LEFT_OUTPUT |
                   SOUND_A_FIFO_RESET | SOUND_A_TIMER_0 | SOUND_A_RIGHT_OUTPUT |
                   SOUND_ALL_MIX_FULL;
  REG_SOUNDBIAS = 0x0200;
  REG_SOUNDBIAS_H = (u8)((REG_SOUNDBIAS_H & 0x3F) | 0x40);

  memset(&gSoundInfo, 0, sizeof(gSoundInfo));
  gPfrSoundInfoPtr = &gSoundInfo;
  gSoundInfo.maxChans = 8;
  gSoundInfo.masterVolume = 15;
  gSoundInfo.plynote = (PlyNoteFunc)ply_note;
  gSoundInfo.CgbSound = (CgbSoundFunc)DummyFunc;
  gSoundInfo.CgbOscOff = (CgbOscOffFunc)DummyFunc;
  gSoundInfo.MidiKeyToCgbFreq = (MidiKeyToCgbFreqFunc)DummyFunc;
  gSoundInfo.ExtVolPit = (ExtVolPitFunc)DummyFunc;
  gSoundInfo.MPlayJumpTable = gMPlayJumpTable;
  gSoundInfo.ident = ID_NUMBER;

  MPlayJumpTableCopy(gMPlayJumpTable);
  MPlayExtender(gCgbChans);
  m4aSoundMode(SOUND_MODE_DA_BIT_8 | SOUND_MODE_FREQ_13379 |
               (12 << SOUND_MODE_MASVOL_SHIFT) |
               (5 << SOUND_MODE_MAXCHN_SHIFT));

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

  for (u32 i = 0; i < MAX_POKEMON_CRIES; i++) {
    struct MusicPlayerInfo* mplayInfo = &gPokemonCryMusicPlayers[i];
    struct MusicPlayerTrack* track = &gPokemonCryTracks[i * 2];

    MPlayOpen(mplayInfo, track, 2);
    track->chan = 0;
  }

  memset((void*)sObservedSeHeaders, 0, sizeof(sObservedSeHeaders));
  memset(sObservedSeClocks, 0, sizeof(sObservedSeClocks));
  memset(sObservedSeActive, 0, sizeof(sObservedSeActive));
  memset(sObservedCryClocks, 0, sizeof(sObservedCryClocks));
  memset(sObservedCryActive, 0, sizeof(sObservedCryActive));
  sPfrOutCapLeft = 0;
  sPfrOutCapRight = 0;
  sPfrOutHpfQ16 = pfr_audio_output_hpf_q16(PFR_DEFAULT_AUDIO_SAMPLE_RATE);
}

void
m4aSoundMain(void)
{
  u32 driverFrames = gSoundInfo.pcmSamplesPerVBlank;
  u32 chunkOffset = pfr_audio_driver_chunk_offset(&gSoundInfo);
  int16_t driverOutput[528 * PFR_AUDIO_CHANNEL_COUNT];
  int16_t output[PFR_AUDIO_FRAMES_PER_GBA_FRAME * PFR_AUDIO_CHANNEL_COUNT];

  SoundMain();
  memset(output, 0, sizeof(output));

  pfr_audio_driver_pcm_to_output(
    &gSoundInfo, chunkOffset, driverOutput, driverFrames);
  pfr_audio_expand_driver_mix(
    driverOutput, driverFrames, output, PFR_AUDIO_FRAMES_PER_GBA_FRAME);
  pfr_audio_mix_cgb_channels(
    &gSoundInfo, output, PFR_AUDIO_FRAMES_PER_GBA_FRAME);

  pfr_audio_queue_source_frames(output, PFR_AUDIO_FRAMES_PER_GBA_FRAME);
}

void
m4aSoundVSync(void)
{
  const u32 dmaReload =
    (u32)(((u32)(DMA_ENABLE | DMA_START_NOW | DMA_32BIT | DMA_SRC_INC |
                 DMA_DEST_FIXED)
           << 16) |
          4u);

  if (gSoundInfo.ident < ID_NUMBER || gSoundInfo.ident > ID_NUMBER + 1) {
    return;
  }

  if (--gSoundInfo.pcmDmaCounter > 0) {
    return;
  }

  gSoundInfo.pcmDmaCounter = gSoundInfo.pcmDmaPeriod;

  if (REG_DMA1CNT & (DMA_REPEAT << 16)) {
    REG_DMA1CNT = dmaReload;
  }

  if (REG_DMA2CNT & (DMA_REPEAT << 16)) {
    REG_DMA2CNT = dmaReload;
  }

  REG_DMA1CNT_H = DMA_32BIT;
  REG_DMA2CNT_H = DMA_32BIT;
  REG_DMA1CNT_H = DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT;
  REG_DMA2CNT_H = DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT;
}

void
m4aSoundVSyncOn(void)
{
  u32 ident = gSoundInfo.ident;

  if (ident == ID_NUMBER) {
    return;
  }

  REG_DMA1CNT_H = DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT;
  REG_DMA2CNT_H = DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT;
  gSoundInfo.pcmDmaCounter = 0;
  gSoundInfo.ident = ident - 10;
}

void
m4aSoundVSyncOff(void)
{
  const u32 dmaReload =
    (u32)(((u32)(DMA_ENABLE | DMA_START_NOW | DMA_32BIT | DMA_SRC_INC |
                 DMA_DEST_FIXED)
           << 16) |
          4u);

  if (gSoundInfo.ident >= ID_NUMBER && gSoundInfo.ident <= ID_NUMBER + 1) {
    gSoundInfo.ident += 10;

    if (REG_DMA1CNT & (DMA_REPEAT << 16)) {
      REG_DMA1CNT = dmaReload;
    }

    if (REG_DMA2CNT & (DMA_REPEAT << 16)) {
      REG_DMA2CNT = dmaReload;
    }

    REG_DMA1CNT_H = DMA_32BIT;
    REG_DMA2CNT_H = DMA_32BIT;
    memset(gSoundInfo.pcmBuffer, 0, sizeof(gSoundInfo.pcmBuffer));
  }
}

u16
SpeciesToCryId(u16 species)
{
  return species;
}

void
ClearPokemonCrySongs(void)
{
  CpuFill16(
    0, gPokemonCrySongs, MAX_POKEMON_CRIES * sizeof(struct PokemonCrySong));
}

u16
pfr_stub_current_bgm(void)
{
  s32 song_id = pfr_audio_song_id_for_header(gMPlayInfo_BGM.songHeader);

  return song_id < 0 ? 0 : (u16)song_id;
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
      s32 song_id = pfr_audio_song_id_for_header(header);

      sObservedSeHeaders[i] = header;
      sObservedSeClocks[i] = mplay->clock;
      sObservedSeActive[i] = TRUE;
      if (song_id >= 0) {
        *songNum = (u16)song_id;
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
  u32 i;

  for (i = 0; i < MAX_POKEMON_CRIES; i++) {
    struct MusicPlayerInfo* mplay = &gPokemonCryMusicPlayers[i];
    bool32 active = FALSE;
    u32 trackIndex;

    for (trackIndex = 0; trackIndex < mplay->trackCount; trackIndex++) {
      struct MusicPlayerTrack* track = &mplay->tracks[trackIndex];

      if (track->chan != NULL && track->chan->track == track) {
        active = TRUE;
        break;
      }
    }

    if (!active) {
      sObservedCryClocks[i] = mplay->clock;
      sObservedCryActive[i] = FALSE;
      continue;
    }

    if (!sObservedCryActive[i] || mplay->clock < sObservedCryClocks[i]) {
      sObservedCryClocks[i] = mplay->clock;
      sObservedCryActive[i] = TRUE;
      return TRUE;
    }

    sObservedCryClocks[i] = mplay->clock;
  }

  return FALSE;
}
