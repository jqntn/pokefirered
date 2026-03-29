#ifndef GUARD_MAIN_H
#define GUARD_MAIN_H

typedef void (*MainCallback)(void);
typedef void (*IntrCallback)(void);
typedef void (*IntrFunc)(void);

#include "gba/gba.h"

extern IntrFunc gIntrTable[];

struct Main
{
  MainCallback callback1;
  MainCallback callback2;
  MainCallback savedCallback;
  IntrCallback vblankCallback;
  IntrCallback hblankCallback;
  IntrCallback vcountCallback;
  IntrCallback serialCallback;
  vu16 intrCheck;
  u32* vblankCounter1;
  u32 vblankCounter2;
  u16 heldKeysRaw;
  u16 newKeysRaw;
  u16 heldKeys;
  u16 newKeys;
  u16 newAndRepeatedKeys;
  u16 keyRepeatCounter;
  bool16 watchedKeysPressed;
  u16 watchedKeysMask;
  struct OamData oamBuffer[128];
  u8 state;
  u8 oamLoadDisabled : 1;
  u8 inBattle : 1;
  u8 field_439_x4 : 1;
};

extern struct Main gMain;
extern bool8 gSoftResetDisabled;
extern bool8 gLinkVSyncDisabled;
extern const u8 gGameVersion;
extern const u8 gGameLanguage;
extern u8 gLinkTransferringData;
extern u16 gKeyRepeatStartDelay;

void
AgbMain(void);
void
SetMainCallback2(MainCallback callback);
void
InitKeys(void);
void
SetVBlankCallback(IntrCallback callback);
void
SetHBlankCallback(IntrCallback callback);
void
SetVCountCallback(IntrCallback callback);
void
SetSerialCallback(IntrCallback callback);
void
InitFlashTimer(void);
void
DoSoftReset(void);
void
ClearPokemonCrySongs(void);
void
RestoreSerialTimer3IntrHandlers(void);
void
SetVBlankCounter1Ptr(u32* ptr);
void
DisableVBlankCounter1(void);
void
StartTimer1(void);
void
SeedRngAndSetTrainerId(void);
u16
GetGeneratedTrainerIdLower(void);

#define GAME_CODE_LENGTH 4
extern const char RomHeaderGameCode[GAME_CODE_LENGTH];
extern const char RomHeaderSoftwareVersion;

#endif
