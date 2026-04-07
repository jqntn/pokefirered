#include "gba/defines.h"
#include "gba/io_reg.h"
#include "gba/syscall.h"
#include <string.h>

#include "dma3.h"
#include "global.h"
#include "gpu_regs.h"
#include "m4a.h"
#include "main.h"
#include "pfr/core.h"
#include "pfr/dma.h"
#include "pfr/main_runtime.h"

#include "main_menu.h"
#include "title_screen.h"

void
SetDefaultFontsPointer(void);
void
InitMapMusic(void);

enum
{
  PFR_INTR_COUNT = 14,
  PFR_VCOUNT_DEFAULT_LINE = 150,
};

struct Main gMain = { 0 };
bool8 gSoftResetDisabled = FALSE;
bool8 gLinkVSyncDisabled = FALSE;
u8 gLinkTransferringData = FALSE;
u16 gKeyRepeatStartDelay = 40;
static u16 sKeyRepeatContinueDelay = 5;
static u16 sRawKeys;
static u16 sTrainerId;

const u8 gGameVersion = VERSION_FIRE_RED;
const u8 gGameLanguage = LANGUAGE_ENGLISH;
const char RomHeaderGameCode[GAME_CODE_LENGTH] = { 'B', 'P', 'R', 'E' };
const char RomHeaderSoftwareVersion = 0;

IntrFunc gIntrTable[PFR_INTR_COUNT] = { 0 };

static void
pfr_main_read_keys(void)
{
  u16 keyInput = sRawKeys;

  gMain.newKeysRaw = keyInput & (u16)~gMain.heldKeysRaw;
  gMain.newKeys = gMain.newKeysRaw;
  gMain.newAndRepeatedKeys = gMain.newKeysRaw;

  if (keyInput != 0 && gMain.heldKeys == keyInput) {
    if (gMain.keyRepeatCounter > 0) {
      gMain.keyRepeatCounter--;
    }

    if (gMain.keyRepeatCounter == 0) {
      gMain.newAndRepeatedKeys = keyInput;
      gMain.keyRepeatCounter = sKeyRepeatContinueDelay;
    }
  } else {
    gMain.keyRepeatCounter = gKeyRepeatStartDelay;
  }

  gMain.heldKeysRaw = keyInput;
  gMain.heldKeys = gMain.heldKeysRaw;

  if (gSaveBlock2Ptr->optionsButtonMode == OPTIONS_BUTTON_MODE_L_EQUALS_A) {
    if ((gMain.newKeysRaw & L_BUTTON) != 0) {
      gMain.newKeys |= A_BUTTON;
    }

    if ((gMain.heldKeysRaw & L_BUTTON) != 0) {
      gMain.heldKeys |= A_BUTTON;
    }
  }

  if ((gMain.newKeys & gMain.watchedKeysMask) != 0) {
    gMain.watchedKeysPressed = TRUE;
  }
}

void
pfr_main_init(void)
{
  memset(&gMain, 0, sizeof(gMain));
  memset(gIntrTable, 0, sizeof(gIntrTable));
  gSoftResetDisabled = FALSE;
  gLinkVSyncDisabled = FALSE;
  gLinkTransferringData = FALSE;
  gKeyRepeatStartDelay = 40;
  sKeyRepeatContinueDelay = 5;
  sRawKeys = 0;
  sTrainerId = 0;
  pfr_dma_reset();
  InitKeys();
  m4aSoundInit();
  m4aSoundVSyncOn();
  InitMapMusic();
  SetGpuReg(REG_OFFSET_DISPSTAT,
            (u16)((GetGpuReg(REG_OFFSET_DISPSTAT) & 0x00FF) |
                  (PFR_VCOUNT_DEFAULT_LINE << 8) | DISPSTAT_VCOUNT_INTR));
  EnableInterrupts(INTR_FLAG_VCOUNT);
  REG_KEYINPUT = KEYS_MASK;
}

void
pfr_main_shutdown(void)
{
}

void
AgbMain(void)
{
  pfr_core_request_quit();
}

void
SetMainCallback2(MainCallback callback)
{
  gPfrRuntimeState.title_visible = false;
  gPfrRuntimeState.main_menu_visible = false;

  if (callback == CB2_InitTitleScreen) {
    gPfrRuntimeState.title_visible = true;
  } else if (callback == CB2_InitMainMenu) {
    gPfrRuntimeState.main_menu_visible = true;
  }

  gMain.callback2 = callback;
  gMain.state = 0;
}

void
InitKeys(void)
{
  gMain.heldKeys = 0;
  gMain.newKeys = 0;
  gMain.newAndRepeatedKeys = 0;
  gMain.heldKeysRaw = 0;
  gMain.newKeysRaw = 0;
  gMain.keyRepeatCounter = gKeyRepeatStartDelay;
  gMain.watchedKeysPressed = FALSE;
}

void
SetVBlankCallback(IntrCallback callback)
{
  gMain.vblankCallback = callback;
}

void
SetHBlankCallback(IntrCallback callback)
{
  gMain.hblankCallback = callback;
}

void
SetVCountCallback(IntrCallback callback)
{
  gMain.vcountCallback = callback;
}

void
SetSerialCallback(IntrCallback callback)
{
  gMain.serialCallback = callback;
}

void
InitFlashTimer(void)
{
}

void
DoSoftReset(void)
{
  SoftReset(RESET_ALL);
  pfr_core_request_quit();
}

void
RestoreSerialTimer3IntrHandlers(void)
{
}

void
SetVBlankCounter1Ptr(u32* ptr)
{
  gMain.vblankCounter1 = ptr;
}

void
DisableVBlankCounter1(void)
{
  gMain.vblankCounter1 = NULL;
}

void
StartTimer1(void)
{
}

void
SeedRngAndSetTrainerId(void)
{
  sTrainerId = (u16)(gPfrRuntimeState.frame_counter & 0xFFFF);
}

u16
GetGeneratedTrainerIdLower(void)
{
  return sTrainerId;
}

void
pfr_main_set_raw_keys(u16 keys)
{
  sRawKeys = keys;
  REG_KEYINPUT = (u16)(KEYS_MASK & (u16)~keys);
}

void
pfr_main_run_callbacks(void)
{
  pfr_main_read_keys();

  if (gMain.callback1 != NULL) {
    gMain.callback1();
  }

  if (gMain.callback2 != NULL) {
    gMain.callback2();
  }
}

void
pfr_main_on_hblank(void)
{
  pfr_dma_on_hblank();

  if (gMain.hblankCallback != NULL) {
    gMain.hblankCallback();
  }

  REG_IF |= INTR_FLAG_HBLANK;
  INTR_CHECK |= INTR_FLAG_HBLANK;
  gMain.intrCheck |= INTR_FLAG_HBLANK;
}

void
pfr_main_on_vcount(void)
{
  m4aSoundVSync();

  if (gMain.vcountCallback != NULL) {
    gMain.vcountCallback();
  }

  REG_IF |= INTR_FLAG_VCOUNT;
  INTR_CHECK |= INTR_FLAG_VCOUNT;
  gMain.intrCheck |= INTR_FLAG_VCOUNT;
}

void
pfr_main_on_vblank(void)
{
  if (gMain.vblankCounter1 != NULL) {
    (*gMain.vblankCounter1)++;
  }

  if (gMain.vblankCallback != NULL) {
    gMain.vblankCallback();
  }

  CopyBufferedValuesToGpuRegs();
  ProcessDma3Requests();
  m4aSoundMain();

  if (!gMain.oamLoadDisabled) {
    memcpy(gPfrOam, gMain.oamBuffer, PFR_OAM_SIZE);
  }

  gMain.vblankCounter2++;
  REG_IF |= INTR_FLAG_VBLANK;
  INTR_CHECK |= INTR_FLAG_VBLANK;
  gMain.intrCheck |= INTR_FLAG_VBLANK;
}

extern void
CB2_InitCopyrightScreenAfterBootup(void);

void
pfr_game_boot(void)
{
  SetDefaultFontsPointer();
  gMain.vblankCounter1 = 0;
  gMain.vblankCounter2 = 0;
  gMain.callback1 = NULL;
  gMain.callback2 = CB2_InitCopyrightScreenAfterBootup;
  gMain.state = 0;
  gPfrRuntimeState.title_visible = false;
  gPfrRuntimeState.main_menu_visible = false;
}
