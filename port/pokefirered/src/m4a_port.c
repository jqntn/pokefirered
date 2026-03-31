#include "global.h"
#include "gba/m4a_internal.h"
#include "pfr/audio_assets.h"

#include <string.h>

#define C_V 0x40
#define ID_NUMBER 0x68736D53

extern const u8 gClockTable[];
extern const u8 gScaleTable[];
extern const u32 gFreqTable[];

u32 umul3232H32(u32 multiplier, u32 multiplicand)
{
    return (u32)(((u64)multiplier * (u64)multiplicand) >> 32);
}

void SoundMainBTM(void)
{
}

extern u32 MidiKeyToFreq(struct WaveData *wav, u8 key, u8 fineAdjust);
extern const PfrAudioSongAsset *pfr_audio_song_asset_for_header(const struct SongHeader *song_header);

static const PfrAudioTrackAsset *
pfr_port_find_track_asset(struct MusicPlayerInfo *mplayInfo,
                          struct MusicPlayerTrack *track)
{
    const PfrAudioSongAsset *asset =
        pfr_audio_song_asset_for_header(mplayInfo->songHeader);
    u32 i;

    if (asset == NULL)
        return NULL;

    for (i = 0; i < asset->track_count; i++)
    {
        const PfrAudioTrackAsset *ta = &asset->tracks[i];
        if (track->cmdPtr >= ta->data && track->cmdPtr < ta->data + ta->length)
            return ta;
    }

    return NULL;
}

static u8 *
pfr_port_resolve_pointer(struct MusicPlayerInfo *mplayInfo,
                         struct MusicPlayerTrack *track)
{
    const PfrAudioTrackAsset *ta = pfr_port_find_track_asset(mplayInfo, track);
    u32 offset;
    u32 i;

    if (ta == NULL)
        return track->cmdPtr;

    offset = (u32)(track->cmdPtr - ta->data);

    for (i = 0; i < ta->relocation_count; i++)
    {
        if (ta->relocations[i].offset == offset)
            return (u8 *)(ta->data + ta->relocations[i].target_offset);
    }

    return track->cmdPtr;
}

static const PfrAudioVoice *
pfr_port_resolve_voice(struct MusicPlayerInfo *mplayInfo, u32 voiceIndex)
{
    const PfrAudioSongAsset *asset =
        pfr_audio_song_asset_for_header(mplayInfo->songHeader);

    if (asset == NULL || voiceIndex >= asset->voicegroup_count)
        return NULL;

    return &asset->voicegroup[voiceIndex];
}

void Clear64byte(void *x)
{
    memset(x, 0, 64);
}

void RealClearChain(struct SoundChannel *chan)
{
    struct MusicPlayerTrack *track = chan->track;

    if (track == NULL)
        return;

    if (chan->prevChannelPointer != NULL)
    {
        ((struct SoundChannel *)chan->prevChannelPointer)->nextChannelPointer =
            chan->nextChannelPointer;
    }
    else
    {
        track->chan = chan->nextChannelPointer;
    }

    if (chan->nextChannelPointer != NULL)
    {
        ((struct SoundChannel *)chan->nextChannelPointer)->prevChannelPointer =
            chan->prevChannelPointer;
    }

    chan->track = NULL;
}

void ClearChain(void *x)
{
    RealClearChain((struct SoundChannel *)x);
}

void TrackStop(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    struct SoundChannel *chan = track->chan;

    while (chan != NULL)
    {
        struct SoundChannel *next = chan->nextChannelPointer;
        u8 flags = chan->statusFlags;

        if (flags & SOUND_CHANNEL_SF_ON)
        {
            chan->statusFlags = flags | SOUND_CHANNEL_SF_STOP;
        }

        RealClearChain(chan);
        chan = next;
    }
}

void ChnVolSetAsm(struct SoundChannel *chan, struct MusicPlayerTrack *track)
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

void ply_fine(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    struct SoundChannel *chan = track->chan;

    while (chan != NULL)
    {
        struct SoundChannel *next = chan->nextChannelPointer;
        u8 flags = chan->statusFlags;

        if (flags & SOUND_CHANNEL_SF_ON)
        {
            chan->statusFlags = flags | SOUND_CHANNEL_SF_STOP;
        }

        RealClearChain(chan);
        chan = next;
    }

    track->flags = 0;
}

void ply_goto(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->cmdPtr = pfr_port_resolve_pointer(mplayInfo, track);
}

void ply_patt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    if (track->patternLevel >= 3)
    {
        ply_fine(mplayInfo, track);
        return;
    }

    track->patternStack[track->patternLevel] = track->cmdPtr + 4;
    track->patternLevel++;

    track->cmdPtr = pfr_port_resolve_pointer(mplayInfo, track);
}

void ply_pend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track->patternLevel == 0)
        return;

    track->patternLevel--;
    track->cmdPtr = track->patternStack[track->patternLevel];
}

void ply_rept(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 repeatCount = *track->cmdPtr;

    if (repeatCount == 0)
    {
        track->cmdPtr++;
        track->cmdPtr = pfr_port_resolve_pointer(mplayInfo, track);
        return;
    }

    track->repN++;

    u8 target = *track->cmdPtr;
    track->cmdPtr++;

    if (track->repN < target)
    {
        track->cmdPtr = pfr_port_resolve_pointer(mplayInfo, track);
    }
    else
    {
        track->repN = 0;
        track->cmdPtr += 4;
    }
}

void ply_prio(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->priority = *track->cmdPtr;
    track->cmdPtr++;
}

void ply_tempo(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u16 bpm = *track->cmdPtr;
    track->cmdPtr++;
    mplayInfo->tempoD = (u16)(bpm * 2);
    mplayInfo->tempoI = (u16)((mplayInfo->tempoD * mplayInfo->tempoU) >> 8);
}

void ply_keysh(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->keyShift = (s8)*track->cmdPtr;
    track->cmdPtr++;
    track->flags |= MPT_FLG_PITCHG;
}

void ply_voice(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 voiceIndex = *track->cmdPtr;
    track->cmdPtr++;

    const PfrAudioVoice *voice = pfr_port_resolve_voice(mplayInfo, voiceIndex);

    if (voice == NULL)
        return;

    track->tone.type = voice->kind;
    track->tone.key = voice->base_key;
    track->tone.pan_sweep = (u8)((voice->pan & 0x80) ? voice->pan : 0);
    track->tone.attack = voice->attack;
    track->tone.decay = voice->decay;
    track->tone.sustain = voice->sustain;
    track->tone.release = voice->release;

    if (voice->sample != NULL && voice->sample->wave != NULL)
        track->tone.wav = (struct WaveData *)voice->sample->wave;
    else
        track->tone.wav = NULL;

    track->pfrVoice = (void *)voice;
}

void ply_vol(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->vol = *track->cmdPtr;
    track->cmdPtr++;
    track->flags |= MPT_FLG_VOLCHG;
}

void ply_pan(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->pan = (s8)(*track->cmdPtr - C_V);
    track->cmdPtr++;
    track->flags |= MPT_FLG_VOLCHG;
}

void ply_bend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->bend = (s8)(*track->cmdPtr - C_V);
    track->cmdPtr++;
    track->flags |= MPT_FLG_PITCHG;
}

void ply_bendr(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->bendRange = *track->cmdPtr;
    track->cmdPtr++;
    track->flags |= MPT_FLG_PITCHG;
}

void ply_lfodl(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->lfoDelay = *track->cmdPtr;
    track->cmdPtr++;
}

void ply_modt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    u8 newModT = *track->cmdPtr;
    track->cmdPtr++;

    if (track->modT != newModT)
    {
        track->modT = newModT;
        track->flags |= MPT_FLG_VOLCHG | MPT_FLG_PITCHG;
    }
}

void ply_tune(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->tune = (s8)(*track->cmdPtr - C_V);
    track->cmdPtr++;
    track->flags |= MPT_FLG_PITCHG;
}

void ply_port(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->cmdPtr += 2;
}

void ply_lfos(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    u8 speed = *track->cmdPtr;
    track->cmdPtr++;
    track->lfoSpeed = speed;

    if (speed == 0)
        ClearModM(track);
}

void ply_mod(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    u8 mod = *track->cmdPtr;
    track->cmdPtr++;
    track->mod = mod;

    if (mod == 0)
        ClearModM(track);
}

void ply_endtie(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    u8 key;

    if (*track->cmdPtr < 0x80)
    {
        key = *track->cmdPtr;
        track->cmdPtr++;
        track->key = key;
    }
    else
    {
        key = track->key;
    }

    struct SoundChannel *chan = track->chan;

    while (chan != NULL)
    {
        u8 flags = chan->statusFlags;

        if ((flags & (SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_ENV))
            && !(flags & SOUND_CHANNEL_SF_STOP))
        {
            if (chan->midiKey == key)
            {
                chan->statusFlags = flags | SOUND_CHANNEL_SF_STOP;
                return;
            }
        }

        chan = chan->nextChannelPointer;
    }
}

void ply_note(u32 note_cmd, struct MusicPlayerInfo *mplayInfo,
              struct MusicPlayerTrack *track)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;

    track->gateTime = gClockTable[note_cmd];

    if (*track->cmdPtr < 0x80)
    {
        track->key = *track->cmdPtr;
        track->cmdPtr++;

        if (*track->cmdPtr < 0x80)
        {
            track->velocity = *track->cmdPtr;
            track->cmdPtr++;

            if (*track->cmdPtr < 0x80)
            {
                track->gateTime += *track->cmdPtr;
                track->cmdPtr++;
            }
        }
    }

    s32 rhythmPan = 0;
    const PfrAudioVoice *voice = (const PfrAudioVoice *)track->pfrVoice;

    if (voice == NULL)
        return;

    u8 type = voice->kind;
    u8 key = track->key;
    const PfrAudioVoice *resolved = voice;

    if (type == PFR_AUDIO_VOICE_KEYSPLIT || type == PFR_AUDIO_VOICE_RHYTHM)
    {
        u32 subIndex;

        if (type == PFR_AUDIO_VOICE_KEYSPLIT)
        {
            if (voice->keysplit_table != NULL)
                subIndex = voice->keysplit_table[key];
            else
                subIndex = 0;
        }
        else
        {
            subIndex = key;
        }

        if (voice->subgroup != NULL && subIndex < voice->subgroup_count)
        {
            resolved = &voice->subgroup[subIndex];

            if (resolved->kind == PFR_AUDIO_VOICE_KEYSPLIT
                || resolved->kind == PFR_AUDIO_VOICE_RHYTHM)
                return;

            if (type == PFR_AUDIO_VOICE_RHYTHM)
            {
                if (resolved->pan & 0x80)
                    rhythmPan = (s8)(resolved->pan - TONEDATA_P_S_PAN) * 2;
                key = resolved->base_key;
            }
        }
        else
        {
            return;
        }
    }

    type = resolved->kind;

    u8 priority = mplayInfo->priority + track->priority;
    if (priority > 255)
        priority = 255;

    u8 cgbType = type & TONEDATA_TYPE_CGB;

    struct SoundChannel *chan = NULL;

    if (cgbType != 0)
    {
        if (soundInfo->cgbChans == NULL)
            return;

        struct CgbChannel *cgbChan = &soundInfo->cgbChans[cgbType - 1];

        if (cgbChan->statusFlags & SOUND_CHANNEL_SF_ON)
        {
            if (!(cgbChan->statusFlags & SOUND_CHANNEL_SF_STOP))
            {
                if (cgbChan->priority > priority)
                    return;
                if (cgbChan->priority == priority)
                {
                    if (cgbChan->track != NULL
                        && (uintptr_t)cgbChan->track < (uintptr_t)track)
                        return;
                }
            }
        }

        chan = (struct SoundChannel *)cgbChan;
    }
    else
    {
        u8 bestPriority = priority;
        struct MusicPlayerTrack *bestTrack = track;
        struct SoundChannel *bestChan = NULL;
        u8 foundStopping = 0;

        struct SoundChannel *cur = &soundInfo->chans[0];
        u32 maxChans = soundInfo->maxChans;

        for (u32 i = 0; i < maxChans; i++, cur++)
        {
            if (!(cur->statusFlags & SOUND_CHANNEL_SF_ON))
            {
                chan = cur;
                break;
            }

            if (cur->statusFlags & SOUND_CHANNEL_SF_STOP)
            {
                if (!foundStopping)
                {
                    foundStopping = 1;
                    bestPriority = cur->priority;
                    bestTrack = cur->track;
                    bestChan = cur;
                }
                else if (cur->priority < bestPriority)
                {
                    bestPriority = cur->priority;
                    bestTrack = cur->track;
                    bestChan = cur;
                }
                else if (cur->priority == bestPriority
                         && cur->track > bestTrack)
                {
                    bestTrack = cur->track;
                    bestChan = cur;
                }
                continue;
            }

            if (!foundStopping)
            {
                if (cur->priority < bestPriority)
                {
                    bestPriority = cur->priority;
                    bestTrack = cur->track;
                    bestChan = cur;
                }
                else if (cur->priority == bestPriority)
                {
                    if (cur->track > bestTrack)
                    {
                        bestTrack = cur->track;
                        bestChan = cur;
                    }
                    else if (cur->track == bestTrack)
                    {
                        bestChan = cur;
                    }
                }
            }
        }

        if (chan == NULL)
        {
            chan = bestChan;
            if (chan == NULL)
                return;
        }
    }

    ClearChain(chan);

    chan->prevChannelPointer = NULL;
    chan->nextChannelPointer = track->chan;

    if (track->chan != NULL)
        ((struct SoundChannel *)track->chan)->prevChannelPointer = chan;

    track->chan = chan;
    chan->track = track;

    if (track->lfoDelay != 0)
    {
        track->lfoDelayC = track->lfoDelay;
        ClearModM(track);
    }

    TrkVolPitSet(mplayInfo, track);

    chan->gateTime = track->gateTime;
    chan->priority = (u8)priority;
    chan->midiKey = key;
    chan->rhythmPan = (u8)rhythmPan;
    chan->type = resolved->kind;

    if (resolved->sample != NULL && resolved->sample->wave != NULL)
        chan->wav = (struct WaveData *)resolved->sample->wave;
    else
        chan->wav = track->tone.wav;

    chan->attack = resolved->attack;
    chan->decay = resolved->decay;
    chan->sustain = resolved->sustain;
    chan->release = resolved->release;

    chan->pseudoEchoVolume = track->pseudoEchoVolume;
    chan->pseudoEchoLength = track->pseudoEchoLength;

    ChnVolSetAsm(chan, track);

    s32 adjustedKey = (s32)key + (s32)track->keyM;
    if (adjustedKey < 0)
        adjustedKey = 0;

    if (cgbType != 0)
    {
        struct CgbChannel *cgbChan = (struct CgbChannel *)chan;
        cgbChan->length = resolved->duty_or_period;

        u8 panSweep = resolved->pan;
        if (panSweep & 0x80)
            panSweep = 0x08;
        else if (!(panSweep & 0x70))
            panSweep = 0x08;
        cgbChan->sweep = panSweep;

        chan->frequency = soundInfo->MidiKeyToCgbFreq(cgbType, (u8)adjustedKey, track->pitM);
    }
    else
    {
        chan->count = track->unk_3C;
        chan->frequency = MidiKeyToFreq(chan->wav, (u8)adjustedKey, track->pitM);
    }

    chan->statusFlags = SOUND_CHANNEL_SF_START;
    track->flags &= 0xF0;
}

static void
pfr_port_lfo_update(struct MusicPlayerTrack *track)
{
    u8 speed = track->lfoSpeed;

    if (speed == 0 || track->mod == 0)
        return;

    if (track->lfoDelayC != 0)
    {
        track->lfoDelayC--;
        return;
    }

    track->lfoSpeedC += speed;

    s32 counter = track->lfoSpeedC;
    s32 modVal;

    if (counter < 64)
    {
        modVal = (s8)counter;
    }
    else
    {
        modVal = 128 - counter;
    }

    modVal = (track->mod * modVal) >> 6;

    if ((u8)(modVal ^ track->modM) == 0)
        return;

    track->modM = (s8)modVal;

    if (track->modT == 0)
        track->flags |= MPT_FLG_PITCHG;
    else
        track->flags |= MPT_FLG_VOLCHG;
}

void MPlayMain(struct MusicPlayerInfo *mplayInfo)
{
    struct SoundInfo *soundInfo;

    if (mplayInfo->ident != ID_NUMBER)
        return;

    mplayInfo->ident++;

    if (mplayInfo->MPlayMainNext != NULL)
    {
        mplayInfo->MPlayMainNext(mplayInfo);
    }

    if ((s32)mplayInfo->status < 0)
        goto done;

    soundInfo = SOUND_INFO_PTR;

    FadeOutBody(mplayInfo);

    if ((s32)mplayInfo->status < 0)
        goto done;

    u16 tempoC = mplayInfo->tempoC + mplayInfo->tempoI;

    while (tempoC >= 150)
    {
        u8 trackCount = mplayInfo->trackCount;
        struct MusicPlayerTrack *track = mplayInfo->tracks;
        u32 trackBits = 0;
        u32 bit = 1;

        for (u32 i = 0; i < trackCount; i++, track++, bit <<= 1)
        {
            if (!(track->flags & MPT_FLG_EXIST))
                continue;

            trackBits |= bit;

            struct SoundChannel *chan = track->chan;
            while (chan != NULL)
            {
                struct SoundChannel *next = chan->nextChannelPointer;

                if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
                {
                    ClearChain(chan);
                }
                else
                {
                    if (chan->gateTime != 0)
                    {
                        chan->gateTime--;
                        if (chan->gateTime == 0)
                        {
                            chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
                        }
                    }
                }

                chan = next;
            }

            if (track->flags & MPT_FLG_START)
            {
                Clear64byte(track);
                track->flags = MPT_FLG_EXIST;
                track->bendRange = 2;
                track->volX = 64;
                track->lfoSpeed = 22;
                track->tone.type = 1;
                goto next_command;
            }

            while (track->wait == 0)
            {
                u8 cmd = *track->cmdPtr;
                u8 status;

                if (cmd < 0x80)
                {
                    status = track->runningStatus;
                }
                else
                {
                    track->cmdPtr++;
                    status = cmd;
                    if (status >= 0xBD)
                        track->runningStatus = status;
                }

                if (status >= 0xCF)
                {
                    soundInfo->plynote(status - 0xCF, mplayInfo, track);
                    goto next_command;
                }
                else if (status > 0xB0)
                {
                    mplayInfo->cmd = status - 0xB1;
                    MPlayFunc func = soundInfo->MPlayJumpTable[status - 0xB1];
                    func(mplayInfo, track);

                    if (track->flags == 0)
                        goto track_done;

                    goto next_command;
                }
                else
                {
                    track->wait = gClockTable[status - 0x80];
                }
            }

        next_command:
            if (track->wait > 0)
                track->wait--;

            pfr_port_lfo_update(track);

        track_done:;
        }

        mplayInfo->clock++;

        if (trackBits == 0)
        {
            mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
            goto done;
        }

        mplayInfo->status = trackBits;
        tempoC -= 150;
    }

    mplayInfo->tempoC = tempoC;

    {
        u8 trackCount = mplayInfo->trackCount;
        struct MusicPlayerTrack *track = mplayInfo->tracks;

        for (u32 i = 0; i < trackCount; i++, track++)
        {
            if (!(track->flags & MPT_FLG_EXIST))
                continue;

            if (!(track->flags & (MPT_FLG_VOLCHG | MPT_FLG_PITCHG)))
                continue;

            TrkVolPitSet(mplayInfo, track);

            struct SoundChannel *chan = track->chan;

            while (chan != NULL)
            {
                struct SoundChannel *next = chan->nextChannelPointer;

                if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
                {
                    ClearChain(chan);
                    chan = next;
                    continue;
                }

                u8 chanType = chan->type & TONEDATA_TYPE_CGB;

                if (track->flags & MPT_FLG_VOLCHG)
                {
                    ChnVolSetAsm(chan, track);

                    if (chanType != 0)
                    {
                        struct CgbChannel *cgb = (struct CgbChannel *)chan;
                        cgb->modify |= CGB_CHANNEL_MO_VOL;
                    }
                }

                if (track->flags & MPT_FLG_PITCHG)
                {
                    s32 adjustedKey = (s32)chan->midiKey + (s32)track->keyM;
                    if (adjustedKey < 0)
                        adjustedKey = 0;

                    if (chanType != 0)
                    {
                        struct CgbChannel *cgb = (struct CgbChannel *)chan;
                        cgb->frequency =
                            soundInfo->MidiKeyToCgbFreq(chanType,
                                                        (u8)adjustedKey,
                                                        track->pitM);
                        cgb->modify |= CGB_CHANNEL_MO_PIT;
                    }
                    else
                    {
                        chan->frequency =
                            MidiKeyToFreq(chan->wav,
                                          (u8)adjustedKey,
                                          track->pitM);
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

void SoundMainRAM_EnvelopeStep(struct SoundInfo *soundInfo)
{
    u32 maxChans = soundInfo->maxChans;
    struct SoundChannel *chan = &soundInfo->chans[0];

    for (u32 i = 0; i < maxChans; i++, chan++)
    {
        u8 flags = chan->statusFlags;

        if (!(flags & SOUND_CHANNEL_SF_ON))
            continue;

        u8 envVol = 0;

        if (flags & SOUND_CHANNEL_SF_START)
        {
            if (flags & SOUND_CHANNEL_SF_STOP)
            {
                chan->statusFlags = 0;
                continue;
            }

            flags = SOUND_CHANNEL_SF_ENV_ATTACK;
            chan->statusFlags = flags;

            struct WaveData *wav = chan->wav;
            chan->currentPointer = (s8 *)wav->data + chan->count;
            chan->count = wav->size - chan->count;

            chan->envelopeVolume = 0;
            chan->fw = 0;

            if (wav->status & 0xC000)
            {
                chan->statusFlags = flags | SOUND_CHANNEL_SF_LOOP;
            }

            goto do_attack;
        }

        envVol = chan->envelopeVolume;

        if (flags & SOUND_CHANNEL_SF_IEC)
        {
            u8 echoLen = chan->pseudoEchoLength;
            echoLen--;
            chan->pseudoEchoLength = echoLen;

            if (echoLen > 0)
                goto apply_volume;

            chan->statusFlags = 0;
            continue;
        }

        if (flags & SOUND_CHANNEL_SF_STOP)
        {
            envVol = (envVol * chan->release) >> 8;

            if (envVol <= chan->pseudoEchoVolume)
            {
                if (chan->pseudoEchoVolume == 0)
                {
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

        if (envPhase == SOUND_CHANNEL_SF_ENV_DECAY)
        {
            envVol = (envVol * chan->decay) >> 8;

            if (envVol <= chan->sustain)
            {
                envVol = chan->sustain;
                if (envVol == 0)
                {
                    if (chan->pseudoEchoVolume == 0)
                    {
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

        if (envPhase != SOUND_CHANNEL_SF_ENV_ATTACK)
            goto apply_volume;

    do_attack:
        envVol += chan->attack;

        if (envVol >= 255)
        {
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

void MPlayJumpTableCopy(MPlayFunc *dest)
{
    u32 i;

    for (i = 0; i < 36; i++)
        dest[i] = (MPlayFunc)ply_fine;

    dest[0] = (MPlayFunc)ply_fine;     /* 0xB1 */
    dest[1] = (MPlayFunc)ply_goto;     /* 0xB2 */
    dest[2] = (MPlayFunc)ply_patt;     /* 0xB3 */
    dest[3] = (MPlayFunc)ply_pend;     /* 0xB4 */
    dest[4] = (MPlayFunc)ply_rept;     /* 0xB5 */
    dest[5] = (MPlayFunc)ply_prio;     /* 0xB6 */
    dest[8] = (MPlayFunc)ply_memacc;   /* 0xB9 */
    dest[10] = (MPlayFunc)ply_tempo;   /* 0xBB */
    dest[11] = (MPlayFunc)ply_keysh;   /* 0xBC */
    dest[12] = (MPlayFunc)ply_voice;   /* 0xBD */
    dest[13] = (MPlayFunc)ply_vol;     /* 0xBE */
    dest[14] = (MPlayFunc)ply_pan;     /* 0xBF */
    dest[15] = (MPlayFunc)ply_bend;    /* 0xC0 */
    dest[16] = (MPlayFunc)ply_bendr;   /* 0xC1 */
    dest[17] = (MPlayFunc)ply_lfos;    /* 0xC2 */
    dest[18] = (MPlayFunc)ply_lfodl;   /* 0xC3 */
    dest[19] = (MPlayFunc)ply_mod;     /* 0xC4 */
    dest[20] = (MPlayFunc)ply_modt;    /* 0xC5 */
    dest[23] = (MPlayFunc)ply_tune;    /* 0xC8 */
    dest[27] = (MPlayFunc)ply_port;    /* 0xCC */
    dest[28] = (MPlayFunc)ply_xcmd;    /* 0xCD */
    dest[29] = (MPlayFunc)ply_endtie;  /* 0xCE */
    dest[30] = (MPlayFunc)SampleFreqSet;
    dest[31] = (MPlayFunc)TrackStop;
    dest[32] = (MPlayFunc)FadeOutBody;
    dest[33] = (MPlayFunc)TrkVolPitSet;
    dest[34] = (MPlayFunc)ClearChain;
    dest[35] = (MPlayFunc)Clear64byte;
}

void SoundMain(void)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    u32 ident = soundInfo->ident;

    if (ident != ID_NUMBER && ident != ID_NUMBER + 1)
        return;

    soundInfo->ident++;

    SoundMainRAM_EnvelopeStep(soundInfo);

    {
        struct MusicPlayerInfo *player = soundInfo->musicPlayerHead;

        while (player != NULL)
        {
            MPlayMain(player);
            player = player->musicPlayerNext;
        }
    }

    soundInfo->CgbSound();

    soundInfo->ident = ID_NUMBER;
}
