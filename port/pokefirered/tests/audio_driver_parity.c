#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "constants/songs.h"
#include "gba/io_reg.h"
#define SoundMainBTM pfr_hidden_SoundMainBTM_decl
#include "gba/m4a_internal.h"
#undef SoundMainBTM
#include "m4a.h"
#include "pfr/audio.h"
#include "pfr/audio_assets.h"
#include "pfr/core.h"
#include "pfr/m4a_1_host.h"

extern void* const gMPlayJumpTableTemplate[];
extern void
SoundMainBTM(void* x);
extern void
SoundMainRAM_EnvelopeStep(struct SoundInfo* soundInfo);
extern void
pfr_audio_render_driver_pcm_chunk(struct SoundInfo* soundInfo,
                                  u32 chunkOffset,
                                  u32 numSamples);

typedef struct TestWaveData
{
  u16 type;
  u16 status;
  u32 freq;
  u32 loopStart;
  u32 size;
  s8 data[8];
} TestWaveData;

typedef struct TestRelocationFixture
{
  const struct SongHeader* header;
  const PfrAudioTrackAsset* track;
  const PfrAudioTrackRelocation* relocation;
} TestRelocationFixture;

static struct MusicPlayerInfo* sRecordedPlayer;
static int sRecordedPlayerCount;
static int sSoundMainHeadCount;
static int sSoundMainCgbCount;

static void
test_reset_audio_state(void)
{
  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(&gSoundInfo, 0, sizeof(gSoundInfo));
  memset(gMPlayJumpTable, 0, sizeof(MPlayFunc) * 36);
  memset(gCgbChans, 0, sizeof(struct CgbChannel) * 4);
  memset(
    gPokemonCrySongs, 0, sizeof(struct PokemonCrySong) * MAX_POKEMON_CRIES);
  gPfrSoundInfoPtr = &gSoundInfo;
  pfr_audio_reset();
  sRecordedPlayer = NULL;
  sRecordedPlayerCount = 0;
  sSoundMainHeadCount = 0;
  sSoundMainCgbCount = 0;
}

static void
test_record_music_player(struct MusicPlayerInfo* mplayInfo)
{
  sRecordedPlayer = mplayInfo;
  sRecordedPlayerCount++;
}

static void
test_soundmain_head(struct MusicPlayerInfo* mplayInfo)
{
  sRecordedPlayer = mplayInfo;
  sSoundMainHeadCount++;
}

static void
test_soundmain_cgb(void)
{
  sSoundMainCgbCount++;
}

static TestRelocationFixture
test_find_relocation_fixture(void)
{
  TestRelocationFixture fixture = { 0 };
  u32 songId;

  for (songId = 0; songId < gPfrAudioSongAssetCount; songId++) {
    const struct SongHeader* header = gSongTable[songId].header;
    const PfrAudioSongAsset* asset = pfr_audio_song_asset_for_id((u16)songId);
    u32 trackIndex;

    if (header == NULL || asset == NULL || asset->tracks == NULL) {
      continue;
    }

    for (trackIndex = 0; trackIndex < header->trackCount; trackIndex++) {
      const PfrAudioTrackAsset* track = &asset->tracks[trackIndex];

      if (track->relocation_count == 0) {
        continue;
      }

      fixture.header = header;
      fixture.track = track;
      fixture.relocation = &track->relocations[0];
      return fixture;
    }
  }

  assert(!"expected at least one relocated audio track");
  return fixture;
}

static void
test_soundmainbtm_and_jump_table_copy(void)
{
  u8 buffer[80];
  MPlayFunc localJumpTable[36];

  memset(buffer, 0xA5, sizeof(buffer));
  memset(localJumpTable, 0, sizeof(localJumpTable));

  SoundMainBTM(buffer + 8);
  assert(buffer[7] == 0xA5);
  assert(buffer[72] == 0xA5);
  for (size_t i = 8; i < 72; i++) {
    assert(buffer[i] == 0);
  }

  MPlayJumpTableCopy(localJumpTable);
  assert(memcmp(localJumpTable,
                gMPlayJumpTableTemplate,
                sizeof(localJumpTable)) == 0);

  test_reset_audio_state();
  MPlayJumpTableCopy(gMPlayJumpTable);
  memset(buffer, 0x3C, sizeof(buffer));
  Clear64byte(buffer + 4);
  assert(buffer[3] == 0x3C);
  assert(buffer[68] == 0x3C);
  for (size_t i = 4; i < 68; i++) {
    assert(buffer[i] == 0);
  }
}

static void
test_realclearchain_and_clearchain(void)
{
  struct MusicPlayerTrack track = { 0 };
  struct SoundChannel chan0 = { 0 };
  struct SoundChannel chan1 = { 0 };
  struct SoundChannel chan2 = { 0 };

  test_reset_audio_state();
  MPlayJumpTableCopy(gMPlayJumpTable);

  track.chan = &chan0;
  chan0.track = &track;
  chan0.nextChannelPointer = &chan1;
  chan1.track = &track;
  chan1.prevChannelPointer = &chan0;

  ClearChain(&chan0);
  assert(track.chan == &chan1);
  assert(chan0.track == NULL);
  assert(chan1.prevChannelPointer == NULL);

  track.chan = &chan0;
  chan0.track = &track;
  chan0.nextChannelPointer = &chan1;
  chan1.track = &track;
  chan1.prevChannelPointer = &chan0;
  chan1.nextChannelPointer = &chan2;
  chan2.track = &track;
  chan2.prevChannelPointer = &chan1;

  RealClearChain(&chan1);
  assert(track.chan == &chan0);
  assert(chan0.nextChannelPointer == &chan2);
  assert(chan2.prevChannelPointer == &chan0);
  assert(chan1.track == NULL);
}

static void
test_mplay_extender_patches_expected_slots(void)
{
  test_reset_audio_state();
  gSoundInfo.ident = ID_NUMBER;
  MPlayJumpTableCopy(gMPlayJumpTable);
  MPlayExtender(gCgbChans);

  assert(gSoundInfo.ident == ID_NUMBER);
  assert(gSoundInfo.cgbChans == gCgbChans);
  assert(gSoundInfo.CgbSound == CgbSound);
  assert(gSoundInfo.CgbOscOff == CgbOscOff);
  assert(gSoundInfo.MidiKeyToCgbFreq == MidiKeyToCgbFreq);
  assert(gSoundInfo.maxLines == (u8)gMaxLines[0]);
  assert(gCgbChans[0].type == PFR_AUDIO_VOICE_SQUARE1);
  assert(gCgbChans[0].panMask == 0x11);
  assert(gCgbChans[1].type == PFR_AUDIO_VOICE_SQUARE2);
  assert(gCgbChans[1].panMask == 0x22);
  assert(gCgbChans[2].type == PFR_AUDIO_VOICE_PROGRAMMABLE_WAVE);
  assert(gCgbChans[2].panMask == 0x44);
  assert(gCgbChans[3].type == PFR_AUDIO_VOICE_NOISE);
  assert(gCgbChans[3].panMask == 0x88);
  assert(gMPlayJumpTable[8] == (MPlayFunc)ply_memacc);
  assert(gMPlayJumpTable[17] == (MPlayFunc)ply_lfos);
  assert(gMPlayJumpTable[19] == (MPlayFunc)ply_mod);
  assert(gMPlayJumpTable[28] == (MPlayFunc)ply_xcmd);
  assert(gMPlayJumpTable[29] == (MPlayFunc)ply_endtie);
  assert(gMPlayJumpTable[30] == (MPlayFunc)SampleFreqSet);
  assert(gMPlayJumpTable[31] == (MPlayFunc)TrackStop);
  assert(gMPlayJumpTable[32] == (MPlayFunc)FadeOutBody);
  assert(gMPlayJumpTable[33] == (MPlayFunc)TrkVolPitSet);
  assert(gMPlayJumpTable[34] == (MPlayFunc)RealClearChain);
  assert(gMPlayJumpTable[35] == (MPlayFunc)SoundMainBTM);
  assert(REG_SOUNDCNT_X == (SOUND_MASTER_ENABLE | SOUND_4_ON | SOUND_3_ON |
                            SOUND_2_ON | SOUND_1_ON));
  assert(REG_NR12 == 0x08);
  assert(REG_NR22 == 0x08);
  assert(REG_NR42 == 0x08);
  assert(REG_NR14 == 0x80);
  assert(REG_NR24 == 0x80);
  assert(REG_NR44 == 0x80);
  assert(REG_NR30 == 0x00);
  assert(REG_NR50 == 0x77);
}

static void
test_trackstop_requires_exist_and_clears_chain(void)
{
  struct MusicPlayerTrack track = { 0 };
  struct SoundChannel cgbChan = { 0 };
  struct SoundChannel dsChan = { 0 };

  test_reset_audio_state();
  gSoundInfo.CgbOscOff = CgbOscOff;

  track.chan = &cgbChan;
  cgbChan.track = &track;
  cgbChan.nextChannelPointer = &dsChan;
  dsChan.track = &track;
  dsChan.prevChannelPointer = &cgbChan;

  cgbChan.statusFlags = SOUND_CHANNEL_SF_ON;
  cgbChan.type = PFR_AUDIO_VOICE_SQUARE2;
  dsChan.statusFlags = SOUND_CHANNEL_SF_ON;

  TrackStop(NULL, &track);
  assert(track.chan == &cgbChan);
  assert(cgbChan.statusFlags == SOUND_CHANNEL_SF_ON);
  assert(dsChan.statusFlags == SOUND_CHANNEL_SF_ON);
  assert(cgbChan.track == &track);
  assert(dsChan.track == &track);

  track.flags = MPT_FLG_EXIST;
  TrackStop(NULL, &track);
  assert(track.chan == NULL);
  assert(cgbChan.statusFlags == 0);
  assert(dsChan.statusFlags == 0);
  assert(cgbChan.track == NULL);
  assert(dsChan.track == NULL);
  assert(REG_NR22 == 0x08);
  assert(REG_NR24 == 0x80);
}

static void
test_pointer_control_commands(void)
{
  struct PokemonCrySong* cry = &gPokemonCrySongs[0];
  struct MusicPlayerInfo mplayInfo = { 0 };
  struct MusicPlayerTrack track = { 0 };
  TestRelocationFixture relocationFixture = test_find_relocation_fixture();
  u8* gotoPointer = (u8*)&cry->gotoTarget;

  test_reset_audio_state();

  mplayInfo.songHeader = (struct SongHeader*)cry;
  track.cmdPtr = gotoPointer;
  ply_goto(&mplayInfo, &track);
  assert(track.cmdPtr == &cry->cont[0]);

  track.cmdPtr = gotoPointer;
  track.patternLevel = 0;
  ply_patt(&mplayInfo, &track);
  assert(track.patternLevel == 1);
  assert(track.patternStack[0] == gotoPointer + 4);
  assert(track.cmdPtr == &cry->cont[0]);

  ply_pend(&mplayInfo, &track);
  assert(track.patternLevel == 0);
  assert(track.cmdPtr == gotoPointer + 4);

  cry->gotoCmd = 2;
  track.cmdPtr = &cry->gotoCmd;
  track.repN = 0;
  ply_rept(&mplayInfo, &track);
  assert(track.repN == 1);
  assert(track.cmdPtr == &cry->cont[0]);

  track.cmdPtr = &cry->gotoCmd;
  track.repN = 1;
  ply_rept(&mplayInfo, &track);
  assert(track.repN == 0);
  assert(track.cmdPtr == gotoPointer + 4);

  cry->gotoCmd = 0;
  track.cmdPtr = &cry->gotoCmd;
  track.repN = 7;
  ply_rept(&mplayInfo, &track);
  assert(track.repN == 7);
  assert(track.cmdPtr == &cry->cont[0]);

  mplayInfo.songHeader = (struct SongHeader*)relocationFixture.header;
  track.cmdPtr =
    (u8*)(relocationFixture.track->data + relocationFixture.relocation->offset);
  ply_goto(&mplayInfo, &track);
  assert(track.cmdPtr == (u8*)(relocationFixture.track->data +
                               relocationFixture.relocation->target_offset));
}

static void
test_ply_port_and_cgb_osc_off_register_writes(void)
{
  struct MusicPlayerTrack track = { 0 };
  u8 command[] = {
    (u8)(REG_ADDR_NR12 - REG_ADDR_SOUND1CNT_L),
    0x5A,
    0xFF,
  };

  test_reset_audio_state();

  track.cmdPtr = command;
  ply_port(NULL, &track);
  assert(REG_NR12 == 0x5A);
  assert(track.cmdPtr == &command[2]);

  memset(gPfrIo, 0, PFR_IO_SIZE);
  CgbOscOff(1);
  assert(REG_NR12 == 0x08);
  assert(REG_NR14 == 0x80);

  memset(gPfrIo, 0, PFR_IO_SIZE);
  CgbOscOff(2);
  assert(REG_NR22 == 0x08);
  assert(REG_NR24 == 0x80);

  memset(gPfrIo, 0, PFR_IO_SIZE);
  CgbOscOff(3);
  assert(REG_NR30 == 0x00);

  memset(gPfrIo, 0, PFR_IO_SIZE);
  CgbOscOff(4);
  assert(REG_NR42 == 0x08);
  assert(REG_NR44 == 0x80);
}

static void
test_mplaymain_uses_music_player_next(void)
{
  struct MusicPlayerInfo mplayInfo = { 0 };
  struct MusicPlayerInfo nextPlayer = { 0 };

  test_reset_audio_state();
  mplayInfo.ident = ID_NUMBER;
  mplayInfo.status = MUSICPLAYER_STATUS_PAUSE;
  mplayInfo.MPlayMainNext = test_record_music_player;
  mplayInfo.musicPlayerNext = &nextPlayer;

  MPlayMain(&mplayInfo);
  assert(sRecordedPlayerCount == 1);
  assert(sRecordedPlayer == &nextPlayer);
  assert(mplayInfo.ident == ID_NUMBER);
}

static void
test_soundmain_guard_and_chunk_selection(void)
{
  struct MusicPlayerInfo player = { 0 };
  u32 i;
  u32 chunkOffset = 8;

  test_reset_audio_state();
  gSoundInfo.ident = ID_NUMBER;
  gSoundInfo.MPlayMainHead = test_soundmain_head;
  gSoundInfo.musicPlayerHead = &player;
  gSoundInfo.CgbSound = test_soundmain_cgb;
  gSoundInfo.pcmSamplesPerVBlank = 4;
  gSoundInfo.pcmDmaPeriod = 3;
  gSoundInfo.pcmDmaCounter = 2;
  gSoundInfo.reverb = 0;
  gSoundInfo.maxChans = 0;
  memset(gSoundInfo.pcmBuffer, 0x5A, sizeof(gSoundInfo.pcmBuffer));

  SoundMain();
  assert(sSoundMainHeadCount == 1);
  assert(sSoundMainCgbCount == 1);
  assert(sRecordedPlayer == &player);
  assert(gSoundInfo.ident == ID_NUMBER);
  for (i = 0; i < 4; i++) {
    assert(gSoundInfo.pcmBuffer[chunkOffset + i] == 0);
    assert(gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + chunkOffset + i] == 0);
  }
  assert(gSoundInfo.pcmBuffer[0] == 0x5A);
  assert(gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] == 0x5A);

  test_reset_audio_state();
  gSoundInfo.ident = ID_NUMBER + 10;
  gSoundInfo.MPlayMainHead = test_soundmain_head;
  gSoundInfo.CgbSound = test_soundmain_cgb;
  memset(gSoundInfo.pcmBuffer, 0x66, sizeof(gSoundInfo.pcmBuffer));

  SoundMain();
  assert(sSoundMainHeadCount == 0);
  assert(sSoundMainCgbCount == 0);
  assert(gSoundInfo.pcmBuffer[0] == 0x66);
}

static void
test_soundmainram_envelope_step(void)
{
  struct SoundInfo soundInfo = { 0 };
  struct SoundChannel* chan = &soundInfo.chans[0];
  TestWaveData wave = {
    0, 0xC000u, 0, 1, 4, { 10, 20, 30, 40 },
  };

  soundInfo.maxChans = 1;
  soundInfo.masterVolume = 15;
  chan->statusFlags = SOUND_CHANNEL_SF_START;
  chan->wav = (struct WaveData*)&wave;
  chan->count = 1;
  chan->attack = 128;
  chan->rightVolume = 255;
  chan->leftVolume = 64;

  SoundMainRAM_EnvelopeStep(&soundInfo);
  assert(chan->statusFlags ==
         (SOUND_CHANNEL_SF_ENV_ATTACK | SOUND_CHANNEL_SF_LOOP));
  assert(chan->currentPointer == &wave.data[1]);
  assert(chan->count == 3);
  assert(chan->envelopeVolume == 128);
  assert(chan->envelopeVolumeRight == 127);
  assert(chan->envelopeVolumeLeft == 32);

  memset(&soundInfo, 0, sizeof(soundInfo));
  chan = &soundInfo.chans[0];
  soundInfo.maxChans = 1;
  soundInfo.masterVolume = 15;
  chan->statusFlags = SOUND_CHANNEL_SF_STOP | SOUND_CHANNEL_SF_ENV_ATTACK;
  chan->envelopeVolume = 8;
  chan->release = 128;
  chan->pseudoEchoVolume = 4;
  chan->pseudoEchoLength = 1;

  SoundMainRAM_EnvelopeStep(&soundInfo);
  assert((chan->statusFlags & SOUND_CHANNEL_SF_IEC) != 0);
  assert(chan->envelopeVolume == 4);

  chan->statusFlags = SOUND_CHANNEL_SF_IEC;
  chan->pseudoEchoLength = 1;
  SoundMainRAM_EnvelopeStep(&soundInfo);
  assert(chan->statusFlags == 0);
}

static void
test_driver_pcm_reverb_paths(void)
{
  struct SoundInfo soundInfo = { 0 };
  u32 i;

  memset(&soundInfo, 0, sizeof(soundInfo));
  soundInfo.maxChans = 0;
  soundInfo.reverb = 0;
  memset(soundInfo.pcmBuffer, 0x7B, sizeof(soundInfo.pcmBuffer));
  pfr_audio_render_driver_pcm_chunk(&soundInfo, 4, 4);
  for (i = 0; i < 4; i++) {
    assert(soundInfo.pcmBuffer[4 + i] == 0);
    assert(soundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + 4 + i] == 0);
  }

  memset(&soundInfo, 0, sizeof(soundInfo));
  soundInfo.maxChans = 0;
  soundInfo.reverb = 128;
  soundInfo.pcmDmaCounter = 2;
  for (i = 0; i < 4; i++) {
    soundInfo.pcmBuffer[4 + i] = 4;
    soundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + 4 + i] = 4;
  }

  pfr_audio_render_driver_pcm_chunk(&soundInfo, 4, 4);
  for (i = 0; i < 4; i++) {
    assert(soundInfo.pcmBuffer[4 + i] == 2);
    assert(soundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + 4 + i] == 2);
  }
}

static void
test_driver_pcm_channel_loop_mix(void)
{
  struct SoundInfo soundInfo = { 0 };
  struct SoundChannel* chan = &soundInfo.chans[0];
  TestWaveData wave = {
    0, 0, 0, 1, 4, { 32, 64, 96, 127 },
  };
  const s8 expected[] = { 31, 63, 95, 126, 63, 95 };
  u32 i;

  soundInfo.maxChans = 1;
  soundInfo.masterVolume = 15;
  soundInfo.divFreq = 0x800000;
  chan->statusFlags = SOUND_CHANNEL_SF_ENV_SUSTAIN | SOUND_CHANNEL_SF_LOOP;
  chan->wav = (struct WaveData*)&wave;
  chan->count = wave.size;
  chan->frequency = 1;
  chan->envelopeVolume = 255;
  chan->rightVolume = 255;
  chan->leftVolume = 255;

  pfr_audio_render_driver_pcm_chunk(&soundInfo, 0, 6);
  for (i = 0; i < 6; i++) {
    assert(soundInfo.pcmBuffer[i] == expected[i]);
    assert(soundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + i] == expected[i]);
  }
  assert(chan->count == 1);
  assert(chan->statusFlags ==
         (SOUND_CHANNEL_SF_ENV_SUSTAIN | SOUND_CHANNEL_SF_LOOP));
}

static void
test_m4asoundmain_cgb_queue_output(void)
{
  int16_t firstFrame[2] = { 0, 0 };

  test_reset_audio_state();
  gSoundInfo.ident = ID_NUMBER;
  gSoundInfo.CgbSound = (CgbSoundFunc)DummyFunc;
  gSoundInfo.cgbChans = gCgbChans;
  REG_SOUNDCNT_H = SOUND_CGB_MIX_FULL;

  gCgbChans[0].statusFlags = SOUND_CHANNEL_SF_ENV_SUSTAIN;
  gCgbChans[0].type = PFR_AUDIO_VOICE_SQUARE1;
  gCgbChans[0].pan = 0x01;
  gCgbChans[0].panMask = 0x11;
  gCgbChans[0].envelopeVolume = 15;
  gCgbChans[0].wavePointer = (u32*)(uintptr_t)0;
  *((u32*)&gCgbChans[0].dummy4[0]) = 7u << 16;

  m4aSoundMain();
  assert(pfr_audio_available_frames() == PFR_AUDIO_FRAMES_PER_GBA_FRAME);
  assert(pfr_audio_drain_source_frames(firstFrame, 1) == 1);
  assert(firstFrame[0] == 0);
  assert(firstFrame[1] > 0);
}

static void
test_vsync_paths(void)
{
  u16 repeatMode = DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT;

  test_reset_audio_state();
  gSoundInfo.ident = ID_NUMBER;
  gSoundInfo.pcmDmaCounter = 0;
  gSoundInfo.pcmDmaPeriod = 3;
  REG_DMA1CNT = (u32)DMA_REPEAT << 16;
  REG_DMA2CNT = (u32)DMA_REPEAT << 16;

  m4aSoundVSync();
  assert(gSoundInfo.pcmDmaCounter == 3);
  assert(REG_DMA1CNT_L == 4);
  assert(REG_DMA2CNT_L == 4);
  assert(REG_DMA1CNT_H == repeatMode);
  assert(REG_DMA2CNT_H == repeatMode);

  test_reset_audio_state();
  gSoundInfo.ident = ID_NUMBER;
  gSoundInfo.pcmDmaCounter = 1;
  gSoundInfo.pcmDmaPeriod = 3;
  REG_DMA1CNT = (u32)DMA_REPEAT << 16;
  REG_DMA2CNT = (u32)DMA_REPEAT << 16;

  m4aSoundVSync();
  assert(gSoundInfo.pcmDmaCounter == 3);
  assert(REG_DMA1CNT_L == 4);
  assert(REG_DMA2CNT_L == 4);
  assert(REG_DMA1CNT_H == repeatMode);
  assert(REG_DMA2CNT_H == repeatMode);

  gSoundInfo.ident = ID_NUMBER + 10;
  gSoundInfo.pcmDmaCounter = 2;
  REG_DMA1CNT_H = 0x1234;
  m4aSoundVSync();
  assert(gSoundInfo.pcmDmaCounter == 2);
  assert(REG_DMA1CNT_H == 0x1234);

  test_reset_audio_state();
  gSoundInfo.ident = ID_NUMBER;
  memset(gSoundInfo.pcmBuffer, 0x4D, sizeof(gSoundInfo.pcmBuffer));
  REG_DMA1CNT = (u32)DMA_REPEAT << 16;
  REG_DMA2CNT = (u32)DMA_REPEAT << 16;
  m4aSoundVSyncOff();
  assert(gSoundInfo.ident == ID_NUMBER + 10);
  assert(REG_DMA1CNT_H == DMA_32BIT);
  assert(REG_DMA2CNT_H == DMA_32BIT);
  for (size_t i = 0; i < sizeof(gSoundInfo.pcmBuffer); i++) {
    assert(gSoundInfo.pcmBuffer[i] == 0);
  }

  m4aSoundVSyncOn();
  assert(gSoundInfo.ident == ID_NUMBER);
  assert(gSoundInfo.pcmDmaCounter == 0);
  assert(REG_DMA1CNT_H == repeatMode);
  assert(REG_DMA2CNT_H == repeatMode);
}

int
main(void)
{
  test_soundmainbtm_and_jump_table_copy();
  test_realclearchain_and_clearchain();
  test_mplay_extender_patches_expected_slots();
  test_trackstop_requires_exist_and_clears_chain();
  test_pointer_control_commands();
  test_ply_port_and_cgb_osc_off_register_writes();
  test_mplaymain_uses_music_player_next();
  test_soundmain_guard_and_chunk_selection();
  test_soundmainram_envelope_step();
  test_driver_pcm_reverb_paths();
  test_driver_pcm_channel_loop_mix();
  test_m4asoundmain_cgb_queue_output();
  test_vsync_paths();
  puts("pfr_audio_driver_parity: ok");
  return 0;
}
