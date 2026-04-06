#include "gba/m4a_internal.h"
#include "global.h"
#include "m4a_1_host.h"

#include <string.h>

#define C_V 0x40
#define ID_NUMBER 0x68736D53

extern const u8 gClockTable[];
extern const u8 gScaleTable[];
extern const u32 gFreqTable[];
extern void* const gMPlayJumpTableTemplate[];
extern const s8 gDeltaEncodingTable[];

u32
umul3232H32(u32 multiplier, u32 multiplicand)
{
  return (u32)(((u64)multiplier * (u64)multiplicand) >> 32);
}

void
SoundMainBTM(void* x)
{
  memset(x, 0, 64);
}

extern u32
MidiKeyToFreq(struct WaveData* wav, u8 key, u8 fineAdjust);
void
SoundMainRAM_EnvelopeStep(struct SoundInfo* soundInfo);

static u32*
pfr_port_cgb_phase_ptr(struct CgbChannel* chan)
{
  return (u32*)&chan->dummy4[0];
}

static u32*
pfr_port_cgb_noise_ptr(struct CgbChannel* chan)
{
  return (u32*)&chan->dummy4[4];
}

static s32
pfr_audio_decode_dpcm_sample(const struct WaveData* wav, u32 sampleIndex)
{
  const u8* encoded = (const u8*)wav->data;
  const u8* block = encoded + ((sampleIndex >> 6) * 0x21u);
  u8 sample = block[0];
  u32 offset = sampleIndex & 0x3Fu;
  u32 i;

  for (i = 1; i <= offset; i++) {
    u8 packed = block[1 + ((i - 1) >> 1)];
    u8 nibble = ((i - 1) & 1u) == 0 ? (packed & 0x0Fu) : (packed >> 4);

    sample = (u8)(sample + (s8)gDeltaEncodingTable[nibble]);
  }

  return (s8)sample;
}

static s32
pfr_audio_channel_sample_at(const struct SoundChannel* chan, u32 sampleIndex)
{
  if (chan->type & TONEDATA_TYPE_CMP) {
    return pfr_audio_decode_dpcm_sample(chan->wav, sampleIndex);
  }

  return ((const s8*)chan->wav->data)[sampleIndex];
}

static bool32
pfr_audio_channel_indices(const struct SoundChannel* chan,
                          u32* currentIndex,
                          u32* nextIndex)
{
  if (chan->count == 0 || chan->wav == NULL) {
    return FALSE;
  }

  if (chan->type & TONEDATA_TYPE_REV) {
    *currentIndex = chan->count - 1;
    *nextIndex = chan->count > 1 ? chan->count - 2 : *currentIndex;
  } else {
    *currentIndex = chan->wav->size - chan->count;
    *nextIndex = chan->count > 1 ? *currentIndex + 1 : *currentIndex;
  }

  return TRUE;
}

static void
pfr_audio_channel_refresh_pointer(struct SoundChannel* chan)
{
  if (chan->wav == NULL || chan->count == 0) {
    chan->currentPointer = NULL;
    return;
  }

  if (chan->type & TONEDATA_TYPE_CMP) {
    if (chan->type & TONEDATA_TYPE_REV) {
      chan->currentPointer = (s8*)(uintptr_t)chan->count;
    } else {
      chan->currentPointer = (s8*)(uintptr_t)(chan->wav->size - chan->count);
    }
    return;
  }

  if (chan->type & TONEDATA_TYPE_REV) {
    chan->currentPointer = (s8*)chan->wav->data + chan->count - 1;
  } else {
    chan->currentPointer =
      (s8*)chan->wav->data + (chan->wav->size - chan->count);
  }
}

static void
pfr_audio_channel_advance(struct SoundChannel* chan, u32 advance)
{
  u32 loopLength;
  u32 overshoot;

  if (advance == 0 || chan->wav == NULL || chan->count == 0) {
    return;
  }

  if (advance < chan->count) {
    chan->count -= advance;
    pfr_audio_channel_refresh_pointer(chan);
    return;
  }

  if ((chan->statusFlags & SOUND_CHANNEL_SF_LOOP) &&
      !(chan->type & TONEDATA_TYPE_REV) &&
      chan->wav->loopStart < chan->wav->size) {
    loopLength = chan->wav->size - chan->wav->loopStart;
    if (loopLength == 0) {
      chan->statusFlags = 0;
      chan->count = 0;
      chan->currentPointer = NULL;
      return;
    }

    overshoot = advance - chan->count;
    overshoot %= loopLength;
    chan->count = loopLength - overshoot;
    if (chan->count == 0) {
      chan->count = loopLength;
    }
    pfr_audio_channel_refresh_pointer(chan);
    return;
  }

  chan->statusFlags = 0;
  chan->count = 0;
  chan->currentPointer = NULL;
}

static void
pfr_audio_prepare_driver_reverb(struct SoundInfo* si,
                                u32 chunkOffset,
                                u32 numSamples)
{
  s8* fifoA = &si->pcmBuffer[chunkOffset];
  s8* fifoB = &si->pcmBuffer[PCM_DMA_BUF_SIZE + chunkOffset];
  u32 frame;

  if (numSamples == 0) {
    return;
  }

  if (si->reverb == 0) {
    memset(fifoA, 0, numSamples);
    memset(fifoB, 0, numSamples);
    return;
  }

  {
    s8* prevA =
      si->pcmDmaCounter == 2 ? &si->pcmBuffer[0] : &fifoA[numSamples];
    s8* prevB = &si->pcmBuffer[PCM_DMA_BUF_SIZE + (prevA - si->pcmBuffer)];

    for (frame = 0; frame < numSamples; frame++) {
      s32 sample = fifoB[frame] + fifoA[frame] + prevB[frame] + prevA[frame];

      sample = (sample * si->reverb) >> 9;
      if ((sample & 0x80) != 0) {
        sample++;
      }

      fifoA[frame] = (s8)sample;
      fifoB[frame] = (s8)sample;
    }
  }
}

void
pfr_audio_render_driver_pcm_chunk(struct SoundInfo* si,
                                  u32 chunkOffset,
                                  u32 numSamples)
{
  s8* fifoA = &si->pcmBuffer[chunkOffset];
  s8* fifoB = &si->pcmBuffer[PCM_DMA_BUF_SIZE + chunkOffset];
  u32 frame;

  SoundMainRAM_EnvelopeStep(si);
  pfr_audio_prepare_driver_reverb(si, chunkOffset, numSamples);

  for (frame = 0; frame < numSamples; frame++) {
    s32 mixA = fifoA[frame];
    s32 mixB = fifoB[frame];
    u32 ch;

    for (ch = 0; ch < si->maxChans; ch++) {
      struct SoundChannel* chan = &si->chans[ch];
      s32 sample;
      s32 nextSample;
      u32 advance;
      u32 phase;
      u32 step;
      u32 currentIndex;
      u32 nextIndex;

      if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON)) {
        continue;
      }
      if (!pfr_audio_channel_indices(chan, &currentIndex, &nextIndex)) {
        continue;
      }

      sample = pfr_audio_channel_sample_at(chan, currentIndex);
      nextSample = pfr_audio_channel_sample_at(chan, nextIndex);
      phase = chan->fw;
      if (chan->type & TONEDATA_TYPE_FIX) {
        step = 0x800000u;
      } else {
        step = (u32)((u64)si->divFreq * (u64)chan->frequency);
        sample +=
          (s32)(((s64)phase * (s64)(nextSample - sample)) >> 23);
      }

      mixA += (sample * chan->envelopeVolumeRight) >> 8;
      mixB += (sample * chan->envelopeVolumeLeft) >> 8;
      phase += step;
      advance = phase >> 23;
      chan->fw = phase & 0x7FFFFFu;

      if (advance > 0) {
        pfr_audio_channel_advance(chan, advance);
      }
    }

    fifoA[frame] = (s8)mixA;
    fifoB[frame] = (s8)mixB;
  }
}

void
RealClearChain(void* x)
{
  struct SoundChannel* chan = x;
  struct MusicPlayerTrack* track = chan->track;

  if (track == NULL) {
    return;
  }

  if (chan->prevChannelPointer != NULL) {
    ((struct SoundChannel*)chan->prevChannelPointer)->nextChannelPointer =
      chan->nextChannelPointer;
  } else {
    track->chan = chan->nextChannelPointer;
  }

  if (chan->nextChannelPointer != NULL) {
    ((struct SoundChannel*)chan->nextChannelPointer)->prevChannelPointer =
      chan->prevChannelPointer;
  }

  chan->track = NULL;
}

void
TrackStop(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  struct SoundChannel* chan;
  u8 zero = 0;

  (void)mplayInfo;
  if (!(track->flags & MPT_FLG_EXIST)) {
    return;
  }

  chan = track->chan;
  while (chan != NULL) {
    if (chan->statusFlags != 0) {
      if ((chan->type & TONEDATA_TYPE_CGB) != 0) {
        SOUND_INFO_PTR->CgbOscOff((u8)(chan->type & TONEDATA_TYPE_CGB));
      }

      chan->statusFlags = zero;
    }

    chan->track = NULL;
    chan = chan->nextChannelPointer;
  }

  track->chan = chan;
}

void
ChnVolSetAsm(struct SoundChannel* chan, struct MusicPlayerTrack* track)
{
  s32 velocity = chan->velocity;
  s32 rhythmPan = (s8)chan->rhythmPan;

  s32 panR = 128 + rhythmPan;
  s32 panL = 127 - rhythmPan;

  s32 volR = (track->volMR * panR * velocity) >> 14;
  s32 volL = (track->volML * panL * velocity) >> 14;

  chan->rightVolume = (u8)(volR > 255 ? 255 : volR);
  chan->leftVolume = (u8)(volL > 255 ? 255 : volL);
}

void
ply_fine(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  struct SoundChannel* chan = track->chan;

  while (chan != NULL) {
    struct SoundChannel* next = chan->nextChannelPointer;
    u8 flags = chan->statusFlags;

    if (flags & SOUND_CHANNEL_SF_ON) {
      chan->statusFlags = flags | SOUND_CHANNEL_SF_STOP;
    }

    RealClearChain(chan);
    chan = next;
  }

  track->flags = 0;
}

void
ply_goto(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  track->cmdPtr = pfr_m4a_host_resolve_pointer(mplayInfo, track);
}

void
ply_patt(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  if (track->patternLevel >= 3) {
    ply_fine(mplayInfo, track);
    return;
  }

  track->patternStack[track->patternLevel] = track->cmdPtr + 4;
  track->patternLevel++;

  track->cmdPtr = pfr_m4a_host_resolve_pointer(mplayInfo, track);
}

void
ply_pend(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  if (track->patternLevel == 0) {
    return;
  }

  track->patternLevel--;
  track->cmdPtr = track->patternStack[track->patternLevel];
}

void
ply_rept(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  u8 repeatCount = *track->cmdPtr;

  if (repeatCount == 0) {
    track->cmdPtr++;
    track->cmdPtr = pfr_m4a_host_resolve_pointer(mplayInfo, track);
    return;
  }

  track->repN++;

  u8 target = *track->cmdPtr;
  track->cmdPtr++;

  if (track->repN < target) {
    track->cmdPtr = pfr_m4a_host_resolve_pointer(mplayInfo, track);
  } else {
    track->repN = 0;
    track->cmdPtr += 4;
  }
}

void
ply_prio(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  track->priority = *track->cmdPtr;
  track->cmdPtr++;
}

void
ply_tempo(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  u16 bpm = *track->cmdPtr;
  track->cmdPtr++;
  mplayInfo->tempoD = (u16)(bpm * 2);
  mplayInfo->tempoI = (u16)((mplayInfo->tempoD * mplayInfo->tempoU) >> 8);
}

void
ply_keysh(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  track->keyShift = (s8)*track->cmdPtr;
  track->cmdPtr++;
  track->flags |= MPT_FLG_PITCHG;
}

void
ply_voice(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  u8 voiceIndex = *track->cmdPtr;
  track->cmdPtr++;

  if (mplayInfo->tone == NULL) {
    return;
  }

  track->tone = mplayInfo->tone[voiceIndex];
}

void
ply_vol(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  track->vol = *track->cmdPtr;
  track->cmdPtr++;
  track->flags |= MPT_FLG_VOLCHG;
}

void
ply_pan(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  track->pan = (s8)(*track->cmdPtr - C_V);
  track->cmdPtr++;
  track->flags |= MPT_FLG_VOLCHG;
}

void
ply_bend(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  track->bend = (s8)(*track->cmdPtr - C_V);
  track->cmdPtr++;
  track->flags |= MPT_FLG_PITCHG;
}

void
ply_bendr(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  track->bendRange = *track->cmdPtr;
  track->cmdPtr++;
  track->flags |= MPT_FLG_PITCHG;
}

void
ply_lfodl(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  track->lfoDelay = *track->cmdPtr;
  track->cmdPtr++;
}

void
ply_modt(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  u8 newModT = *track->cmdPtr;
  track->cmdPtr++;

  if (track->modT != newModT) {
    track->modT = newModT;
    track->flags |= MPT_FLG_VOLCHG | MPT_FLG_PITCHG;
  }
}

void
ply_tune(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  track->tune = (s8)(*track->cmdPtr - C_V);
  track->cmdPtr++;
  track->flags |= MPT_FLG_PITCHG;
}

void
ply_port(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  {
    u8* cmdPtr = track->cmdPtr;
    vu8* reg = (vu8*)REG_ADDR_SOUND1CNT_L + cmdPtr[0];

    *reg = cmdPtr[1];
    track->cmdPtr += 2;
  }
}

void
ply_lfos(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  u8 speed = *track->cmdPtr;
  track->cmdPtr++;
  track->lfoSpeed = speed;

  if (speed == 0) {
    ClearModM(track);
  }
}

void
ply_mod(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  u8 mod = *track->cmdPtr;
  track->cmdPtr++;
  track->mod = mod;

  if (mod == 0) {
    ClearModM(track);
  }
}

void
ply_endtie(struct MusicPlayerInfo* mplayInfo, struct MusicPlayerTrack* track)
{
  (void)mplayInfo;
  u8 key;

  if (*track->cmdPtr < 0x80) {
    key = *track->cmdPtr;
    track->cmdPtr++;
    track->key = key;
  } else {
    key = track->key;
  }

  struct SoundChannel* chan = track->chan;

  while (chan != NULL) {
    u8 flags = chan->statusFlags;

    if ((flags & (SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_ENV)) &&
        !(flags & SOUND_CHANNEL_SF_STOP)) {
      if (chan->midiKey == key) {
        chan->statusFlags = flags | SOUND_CHANNEL_SF_STOP;
        return;
      }
    }

    chan = chan->nextChannelPointer;
  }
}

void
ply_note(u32 note_cmd,
         struct MusicPlayerInfo* mplayInfo,
         struct MusicPlayerTrack* track)
{
  struct SoundInfo* soundInfo = SOUND_INFO_PTR;

  track->gateTime = gClockTable[note_cmd];

  if (*track->cmdPtr < 0x80) {
    track->key = *track->cmdPtr;
    track->cmdPtr++;

    if (*track->cmdPtr < 0x80) {
      track->velocity = *track->cmdPtr;
      track->cmdPtr++;

      if (*track->cmdPtr < 0x80) {
        track->gateTime += *track->cmdPtr;
        track->cmdPtr++;
      }
    }
  }

  s32 rhythmPan = 0;
  struct ToneData tone = track->tone;
  u8 type = tone.type;
  u8 midiKey = track->key;
  u8 key = track->key;

  if ((type & (TONEDATA_TYPE_SPL | TONEDATA_TYPE_RHY)) != 0) {
    u32 subIndex;
    const struct ToneData* subgroup = (const struct ToneData*)tone.wav;

    if (subgroup == NULL) {
      return;
    }

    if ((type & TONEDATA_TYPE_SPL) != 0) {
      subIndex = pfr_m4a_host_keysplit_table(&tone)[key];
    } else {
      subIndex = key;
    }

    tone = subgroup[subIndex];

    if ((tone.type & (TONEDATA_TYPE_SPL | TONEDATA_TYPE_RHY)) != 0) {
      return;
    }

    if ((type & TONEDATA_TYPE_RHY) != 0) {
      if (tone.pan_sweep & 0x80) {
        rhythmPan = (s8)(tone.pan_sweep - TONEDATA_P_S_PAN) * 2;
      }
      key = tone.key;
    }
  }

  type = tone.type;

  u16 priority = mplayInfo->priority + track->priority;
  if (priority > 255) {
    priority = 255;
  }

  u8 cgbType = type & TONEDATA_TYPE_CGB;

  struct SoundChannel* chan = NULL;

  if (cgbType != 0) {
    if (soundInfo->cgbChans == NULL) {
      return;
    }

    struct CgbChannel* cgbChan = &soundInfo->cgbChans[cgbType - 1];

    if (cgbChan->statusFlags & SOUND_CHANNEL_SF_ON) {
      if (!(cgbChan->statusFlags & SOUND_CHANNEL_SF_STOP)) {
        if (cgbChan->priority > priority) {
          return;
        }
        if (cgbChan->priority == priority) {
          if (cgbChan->track != NULL &&
              (uintptr_t)cgbChan->track < (uintptr_t)track) {
            return;
          }
        }
      }
    }

    chan = (struct SoundChannel*)cgbChan;
  } else {
    u16 bestPriority = priority;
    struct MusicPlayerTrack* bestTrack = track;
    struct SoundChannel* bestChan = NULL;
    u8 foundStopping = 0;

    struct SoundChannel* cur = &soundInfo->chans[0];
    u32 maxChans = soundInfo->maxChans;

    for (u32 i = 0; i < maxChans; i++, cur++) {
      if (!(cur->statusFlags & SOUND_CHANNEL_SF_ON)) {
        chan = cur;
        break;
      }

      if (cur->statusFlags & SOUND_CHANNEL_SF_STOP) {
        if (!foundStopping) {
          foundStopping = 1;
          bestPriority = cur->priority;
          bestTrack = cur->track;
          bestChan = cur;
        } else if (cur->priority < bestPriority) {
          bestPriority = cur->priority;
          bestTrack = cur->track;
          bestChan = cur;
        } else if (cur->priority == bestPriority && cur->track > bestTrack) {
          bestTrack = cur->track;
          bestChan = cur;
        }
        continue;
      }

      if (!foundStopping) {
        if (cur->priority < bestPriority) {
          bestPriority = cur->priority;
          bestTrack = cur->track;
          bestChan = cur;
        } else if (cur->priority == bestPriority) {
          if (cur->track > bestTrack) {
            bestTrack = cur->track;
            bestChan = cur;
          } else if (cur->track == bestTrack) {
            bestChan = cur;
          }
        }
      }
    }

    if (chan == NULL) {
      chan = bestChan;
      if (chan == NULL) {
        return;
      }
    }
  }

  ClearChain(chan);

  chan->prevChannelPointer = NULL;
  chan->nextChannelPointer = track->chan;

  if (track->chan != NULL) {
    ((struct SoundChannel*)track->chan)->prevChannelPointer = chan;
  }

  track->chan = chan;
  chan->track = track;

  if (track->lfoDelay != 0) {
    track->lfoDelayC = track->lfoDelay;
    ClearModM(track);
  }

  TrkVolPitSet(mplayInfo, track);

  chan->gateTime = track->gateTime;
  chan->velocity = track->velocity;
  chan->priority = (u8)priority;
  chan->key = key;
  chan->midiKey = midiKey;
  chan->rhythmPan = (u8)rhythmPan;
  chan->type = tone.type;
  chan->wav = tone.wav;
  chan->attack = tone.attack;
  chan->decay = tone.decay;
  chan->sustain = tone.sustain;
  chan->release = tone.release;

  chan->pseudoEchoVolume = track->pseudoEchoVolume;
  chan->pseudoEchoLength = track->pseudoEchoLength;

  ChnVolSetAsm(chan, track);

  s32 adjustedKey = (s32)key + (s32)track->keyM;
  if (adjustedKey < 0) {
    adjustedKey = 0;
  }

  if (cgbType != 0) {
    struct CgbChannel* cgbChan = (struct CgbChannel*)chan;

    memset(cgbChan->dummy4, 0, sizeof(cgbChan->dummy4));
    *pfr_port_cgb_phase_ptr(cgbChan) = 0;
    cgbChan->dummy5 = 0;
    cgbChan->length = tone.length;

    u8 panSweep = tone.pan_sweep;
    if (panSweep & 0x80) {
      panSweep = 0x08;
    } else if (!(panSweep & 0x70)) {
      panSweep = 0x08;
    }
    cgbChan->sweep = panSweep;

    if (cgbType == PFR_AUDIO_VOICE_NOISE) {
      *pfr_port_cgb_noise_ptr(cgbChan) = 0;
    }

    chan->frequency =
      soundInfo->MidiKeyToCgbFreq(cgbType, (u8)adjustedKey, track->pitM);
  } else {
    chan->count = track->unk_3C;
    chan->frequency = MidiKeyToFreq(chan->wav, (u8)adjustedKey, track->pitM);
  }

  chan->statusFlags = SOUND_CHANNEL_SF_START;
  track->flags &= 0xF0;
}

static void
pfr_port_lfo_update(struct MusicPlayerTrack* track)
{
  u8 speed = track->lfoSpeed;

  if (speed == 0 || track->mod == 0) {
    return;
  }

  if (track->lfoDelayC != 0) {
    track->lfoDelayC--;
    return;
  }

  track->lfoSpeedC += speed;

  s32 counter = track->lfoSpeedC;
  s32 modVal;

  if (counter < 64) {
    modVal = (s8)counter;
  } else {
    modVal = 128 - counter;
  }

  modVal = (track->mod * modVal) >> 6;

  if ((u8)(modVal ^ track->modM) == 0) {
    return;
  }

  track->modM = (s8)modVal;

  if (track->modT == 0) {
    track->flags |= MPT_FLG_PITCHG;
  } else {
    track->flags |= MPT_FLG_VOLCHG;
  }
}

void
MPlayMain(struct MusicPlayerInfo* mplayInfo)
{
  struct SoundInfo* soundInfo;

  if (mplayInfo->ident != ID_NUMBER) {
    return;
  }

  mplayInfo->ident++;

  if (mplayInfo->MPlayMainNext != NULL) {
    mplayInfo->MPlayMainNext(mplayInfo->musicPlayerNext);
  }

  if ((s32)mplayInfo->status < 0) {
    goto done;
  }

  soundInfo = SOUND_INFO_PTR;

  FadeOutBody(mplayInfo);

  if ((s32)mplayInfo->status < 0) {
    goto done;
  }

  u16 tempoC = mplayInfo->tempoC + mplayInfo->tempoI;

  while (tempoC >= 150) {
    u8 trackCount = mplayInfo->trackCount;
    struct MusicPlayerTrack* track = mplayInfo->tracks;
    u32 trackBits = 0;
    u32 bit = 1;

    for (u32 i = 0; i < trackCount; i++, track++, bit <<= 1) {
      if (!(track->flags & MPT_FLG_EXIST)) {
        continue;
      }

      trackBits |= bit;

      struct SoundChannel* chan = track->chan;
      while (chan != NULL) {
        struct SoundChannel* next = chan->nextChannelPointer;

        if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON)) {
          ClearChain(chan);
        } else {
          if (chan->gateTime != 0) {
            chan->gateTime--;
            if (chan->gateTime == 0) {
              chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
            }
          }
        }

        chan = next;
      }

      if (track->flags & MPT_FLG_START) {
        Clear64byte(track);
        track->flags = MPT_FLG_EXIST;
        track->bendRange = 2;
        track->volX = 64;
        track->lfoSpeed = 22;
        track->tone.type = 1;
      }

      while (track->wait == 0) {
        u8 cmd = *track->cmdPtr;
        u8 status;

        if (cmd < 0x80) {
          status = track->runningStatus;
        } else {
          track->cmdPtr++;
          status = cmd;
          if (status >= 0xBD) {
            track->runningStatus = status;
          }
        }

        if (status >= 0xCF) {
          soundInfo->plynote(status - 0xCF, mplayInfo, track);
        } else if (status > 0xB0) {
          mplayInfo->cmd = status - 0xB1;
          void (*func)(struct MusicPlayerInfo*, struct MusicPlayerTrack*) =
            (void (*)(struct MusicPlayerInfo*, struct MusicPlayerTrack*))
              soundInfo->MPlayJumpTable[status - 0xB1];
          func(mplayInfo, track);

          if (track->flags == 0) {
            goto track_done;
          }
        } else {
          track->wait = gClockTable[status - 0x80];
        }
      }
      if (track->wait > 0) {
        track->wait--;
      }

      pfr_port_lfo_update(track);

    track_done:;
    }

    mplayInfo->clock++;

    if (trackBits == 0) {
      mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
      goto done;
    }

    mplayInfo->status = trackBits;
    tempoC -= 150;
  }

  mplayInfo->tempoC = tempoC;

  {
    u8 trackCount = mplayInfo->trackCount;
    struct MusicPlayerTrack* track = mplayInfo->tracks;

    for (u32 i = 0; i < trackCount; i++, track++) {
      if (!(track->flags & MPT_FLG_EXIST)) {
        continue;
      }

      if (!(track->flags & (MPT_FLG_VOLCHG | MPT_FLG_PITCHG))) {
        continue;
      }

      TrkVolPitSet(mplayInfo, track);

      struct SoundChannel* chan = track->chan;

      while (chan != NULL) {
        struct SoundChannel* next = chan->nextChannelPointer;

        if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON)) {
          ClearChain(chan);
          chan = next;
          continue;
        }

        u8 chanType = chan->type & TONEDATA_TYPE_CGB;

        if (track->flags & MPT_FLG_VOLCHG) {
          ChnVolSetAsm(chan, track);

          if (chanType != 0) {
            struct CgbChannel* cgb = (struct CgbChannel*)chan;
            cgb->modify |= CGB_CHANNEL_MO_VOL;
          }
        }

        if (track->flags & MPT_FLG_PITCHG) {
          s32 adjustedKey = (s32)chan->key + (s32)track->keyM;
          if (adjustedKey < 0) {
            adjustedKey = 0;
          }

          if (chanType != 0) {
            struct CgbChannel* cgb = (struct CgbChannel*)chan;
            cgb->frequency = soundInfo->MidiKeyToCgbFreq(
              chanType, (u8)adjustedKey, track->pitM);
            cgb->modify |= CGB_CHANNEL_MO_PIT;
          } else {
            chan->frequency =
              MidiKeyToFreq(chan->wav, (u8)adjustedKey, track->pitM);
          }
        }

        chan = next;
      }

      track->flags &= 0xF0;
    }
  }

done:
  mplayInfo->ident = ID_NUMBER;
}

void
SoundMainRAM_EnvelopeStep(struct SoundInfo* soundInfo)
{
  u32 maxChans = soundInfo->maxChans;
  struct SoundChannel* chan = &soundInfo->chans[0];

  for (u32 i = 0; i < maxChans; i++, chan++) {
    u8 flags = chan->statusFlags;

    if (!(flags & SOUND_CHANNEL_SF_ON)) {
      continue;
    }

    u8 envVol = 0;

    if (flags & SOUND_CHANNEL_SF_START) {
      if (flags & SOUND_CHANNEL_SF_STOP) {
        chan->statusFlags = 0;
        continue;
      }

      flags = SOUND_CHANNEL_SF_ENV_ATTACK;
      chan->statusFlags = flags;

      struct WaveData* wav = chan->wav;
      u32 startOffset = chan->count;

      if (startOffset >= wav->size) {
        startOffset = 0;
      }

      if (chan->type & TONEDATA_TYPE_REV) {
        chan->currentPointer = (s8*)wav->data + (wav->size - startOffset - 1);
      } else {
        chan->currentPointer = (s8*)wav->data + startOffset;
      }

      chan->count = wav->size - startOffset;

      chan->envelopeVolume = 0;
      chan->fw = 0;

      if (wav->status & 0xC000) {
        chan->statusFlags = flags | SOUND_CHANNEL_SF_LOOP;
      }

      goto do_attack;
    }

    envVol = chan->envelopeVolume;

    if (flags & SOUND_CHANNEL_SF_IEC) {
      u8 echoLen = chan->pseudoEchoLength;
      echoLen--;
      chan->pseudoEchoLength = echoLen;

      if (echoLen > 0) {
        goto apply_volume;
      }

      chan->statusFlags = 0;
      continue;
    }

    if (flags & SOUND_CHANNEL_SF_STOP) {
      envVol = (envVol * chan->release) >> 8;

      if (envVol <= chan->pseudoEchoVolume) {
        if (chan->pseudoEchoVolume == 0) {
          chan->statusFlags = 0;
          continue;
        }

        envVol = chan->pseudoEchoVolume;
        chan->statusFlags = flags | SOUND_CHANNEL_SF_IEC;
        goto apply_volume;
      }

      goto apply_volume;
    }

    u8 envPhase = flags & SOUND_CHANNEL_SF_ENV;

    if (envPhase == SOUND_CHANNEL_SF_ENV_DECAY) {
      envVol = (envVol * chan->decay) >> 8;

      if (envVol <= chan->sustain) {
        envVol = chan->sustain;
        if (envVol == 0) {
          if (chan->pseudoEchoVolume == 0) {
            chan->statusFlags = 0;
            continue;
          }

          envVol = chan->pseudoEchoVolume;
          chan->statusFlags = flags | SOUND_CHANNEL_SF_IEC;
          goto apply_volume;
        }

        chan->statusFlags = flags - 1;
      }

      goto apply_volume;
    }

    if (envPhase != SOUND_CHANNEL_SF_ENV_ATTACK) {
      goto apply_volume;
    }

  do_attack:
    envVol += chan->attack;

    if (envVol >= 255) {
      envVol = 255;
      chan->statusFlags = flags - 1;
    }

  apply_volume:
    chan->envelopeVolume = envVol;

    u32 masterVol = (soundInfo->masterVolume + 1) * (u32)envVol;
    masterVol >>= 4;

    u32 volR = (chan->rightVolume * masterVol) >> 8;
    u32 volL = (chan->leftVolume * masterVol) >> 8;

    chan->envelopeVolumeRight = (u8)(volR > 255 ? 255 : volR);
    chan->envelopeVolumeLeft = (u8)(volL > 255 ? 255 : volL);
  }
}

void
MPlayJumpTableCopy(MPlayFunc* dest)
{
  u32 i;

  for (i = 0; i < 36; i++) {
    dest[i] = (MPlayFunc)gMPlayJumpTableTemplate[i];
  }
}

void
SoundMain(void)
{
  struct SoundInfo* soundInfo = SOUND_INFO_PTR;
  u32 chunkOffset = 0;

  if (soundInfo->ident != ID_NUMBER) {
    return;
  }

  soundInfo->ident++;

  if (soundInfo->MPlayMainHead != NULL) {
    soundInfo->MPlayMainHead(soundInfo->musicPlayerHead);
  }

  soundInfo->CgbSound();

  if (soundInfo->pcmSamplesPerVBlank > 0) {
    if (soundInfo->pcmDmaCounter > 1) {
      chunkOffset = (u32)(soundInfo->pcmSamplesPerVBlank *
                          (soundInfo->pcmDmaPeriod -
                           (soundInfo->pcmDmaCounter - 1)));
    }

    pfr_audio_render_driver_pcm_chunk(
      soundInfo, chunkOffset, (u32)soundInfo->pcmSamplesPerVBlank);
  }

  soundInfo->ident = ID_NUMBER;
}
