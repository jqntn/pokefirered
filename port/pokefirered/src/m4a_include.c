#define UnusedDummyFunc pfr_unused_UnusedDummyFunc
#define m4aSoundInit pfr_unused_m4aSoundInit
#define m4aSoundMain pfr_unused_m4aSoundMain
#define MPlayExtender pfr_unused_MPlayExtender
#define MusicPlayerJumpTableCopy pfr_unused_MusicPlayerJumpTableCopy
#define SoundInit pfr_unused_SoundInit
#define SampleFreqSet pfr_unused_SampleFreqSet
#define m4aSoundMode pfr_unused_m4aSoundMode
#define m4aSoundVSyncOff pfr_unused_m4aSoundVSyncOff
#define m4aSoundVSyncOn pfr_unused_m4aSoundVSyncOn
#define MidiKeyToCgbFreq pfr_unused_MidiKeyToCgbFreq
#define CgbOscOff pfr_unused_CgbOscOff
#define CgbModVol pfr_unused_CgbModVol
#define CgbSound pfr_unused_CgbSound

#if !defined(_MSC_VER)
#define asm(...) ((void)0)
#endif

#include "../../../src/m4a.c"

#if !defined(_MSC_VER)
#undef asm
#endif

#undef CgbSound
#undef CgbModVol
#undef CgbOscOff
#undef MidiKeyToCgbFreq
#undef m4aSoundVSyncOn
#undef m4aSoundVSyncOff
#undef m4aSoundMode
#undef SampleFreqSet
#undef SoundInit
#undef MusicPlayerJumpTableCopy
#undef MPlayExtender
#undef m4aMPlayAllContinue
#undef m4aMPlayAllStop
#undef m4aSoundMain
#undef m4aSoundInit
#undef UnusedDummyFunc
