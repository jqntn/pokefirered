#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100)
#pragma warning(disable : 4113)
#pragma warning(disable : 4244)
#pragma warning(disable : 4245)
#pragma warning(disable : 4305)
#pragma warning(disable : 4311)
#pragma warning(disable : 4312)
#endif

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
#define MPlayStart pfr_unused_MPlayStart
#define MidiKeyToCgbFreq pfr_unused_MidiKeyToCgbFreq
#define CgbOscOff pfr_unused_CgbOscOff
#define CgbModVol pfr_unused_CgbModVol
#define CgbSound pfr_unused_CgbSound
#define SetPokemonCryTone pfr_unused_SetPokemonCryTone
#define SetPokemonCryVolume pfr_unused_SetPokemonCryVolume
#define SetPokemonCryPanpot pfr_unused_SetPokemonCryPanpot
#define SetPokemonCryPitch pfr_unused_SetPokemonCryPitch
#define SetPokemonCryLength pfr_unused_SetPokemonCryLength
#define SetPokemonCryRelease pfr_unused_SetPokemonCryRelease
#define SetPokemonCryProgress pfr_unused_SetPokemonCryProgress
#define IsPokemonCryPlaying pfr_unused_IsPokemonCryPlaying
#define SetPokemonCryChorus pfr_unused_SetPokemonCryChorus
#define SetPokemonCryStereo pfr_unused_SetPokemonCryStereo
#define SetPokemonCryPriority pfr_unused_SetPokemonCryPriority

#ifdef PFR_PORT
#undef PFR_PORT
#endif
#include "../../../src/m4a.c"

#undef SetPokemonCryPriority
#undef SetPokemonCryStereo
#undef SetPokemonCryChorus
#undef IsPokemonCryPlaying
#undef SetPokemonCryProgress
#undef SetPokemonCryRelease
#undef SetPokemonCryLength
#undef SetPokemonCryPitch
#undef SetPokemonCryPanpot
#undef SetPokemonCryVolume
#undef SetPokemonCryTone
#undef CgbSound
#undef CgbModVol
#undef CgbOscOff
#undef MidiKeyToCgbFreq
#undef MPlayStart
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

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
