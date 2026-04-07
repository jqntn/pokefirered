#include "pfr/stubs.h"
#include "gba/gba.h"
#include "global.h"
#include <stdio.h>

#include "berry_fix_program.h"
#include "bg.h"
#include "characters.h"
#include "clear_save_data_screen.h"
#include "decompress.h"
#include "event_data.h"
#include "gba/types.h"
#include "gpu_regs.h"
#include "help_system.h"
#include "link.h"
#include "link_rfu.h"
#include "load_save.h"
#include "m4a.h"
#include "malloc.h"
#include "mystery_gift_menu.h"
#include "oak_speech.h"
#include "pfr/core.h"
#include "pokedex.h"
#include "save.h"
#include "scanline_effect.h"
#include "sprite.h"
#include "task.h"
#include "window.h"

static u16 sTempTileDataBufferCursor;
static void* sTempTileDataBuffers[32];
static const u8 sEmptyPlaceholder[] = { EOS };

static void
Task_SmoothBlendLayers(u8 taskId);

static u16
pfr_copy_decompressed_tile_data_to_vram(u8 bgId,
                                        const void* src,
                                        u16 size,
                                        u16 offset,
                                        u8 mode)
{
  switch (mode) {
    case 1:
      return LoadBgTilemap(bgId, src, size, offset);
    case 0:
    default:
      return LoadBgTiles(bgId, src, size, offset);
  }
}

u8 gQuestLogState = 0;
const struct OamData gOamData_AffineOff_ObjNormal_16x16 = { 0 };

void
GameCubeMultiBoot_Main(void)
{
}
void
GameCubeMultiBoot_ExecuteProgram(void)
{
}
void
GameCubeMultiBoot_Init(void)
{
}
void
GameCubeMultiBoot_HandleSerialInterrupt(void)
{
}
void
GameCubeMultiBoot_Quit(void)
{
}
void
ResetSerial(void)
{
}
void
SerialCB(void)
{
}

void
Save_ResetSaveCounters(void)
{
}
u8
LoadGameSave(u8 saveType)
{
  (void)saveType;
  memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
  memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
  gSaveBlock2Ptr->playerName[0] = 0xCC;
  gSaveBlock2Ptr->playerName[1] = 0xBF;
  gSaveBlock2Ptr->playerName[2] = 0xBE;
  gSaveBlock2Ptr->playerName[3] = EOS;
  gSaveBlock2Ptr->playTimeHours = 1;
  gSaveBlock2Ptr->playTimeMinutes = 23;
  gSaveBlock2Ptr->optionsWindowFrameType = 0;
  gSaveFileStatus = SAVE_STATUS_OK;
  return (u8)gSaveFileStatus;
}

void
ResetMenuAndMonGlobals(void)
{
}
void
Sav2_ClearSetDefault(void)
{
}

void
SetSaveBlocksPointers(void)
{
  gSaveBlock1Ptr = &gSaveBlock1;
  gSaveBlock2Ptr = &gSaveBlock2;
}

void
ResetBgPositions(void)
{
  ChangeBgX(0, 0, 0);
  ChangeBgX(1, 0, 0);
  ChangeBgX(2, 0, 0);
  ChangeBgX(3, 0, 0);
  ChangeBgY(0, 0, 0);
  ChangeBgY(1, 0, 0);
  ChangeBgY(2, 0, 0);
  ChangeBgY(3, 0, 0);
}

void
StartBlendTask(u8 eva_start,
               u8 evb_start,
               u8 eva_end,
               u8 evb_end,
               u8 ev_step,
               u8 priority)
{
  u8 taskId;

  if (ev_step == 0) {
    SetGpuReg(REG_OFFSET_BLDALPHA, (u16)((evb_end << 8) | eva_end));
    return;
  }

  taskId = CreateTask(Task_SmoothBlendLayers, priority);
  gTasks[taskId].data[0] = eva_start << 8;
  gTasks[taskId].data[1] = evb_start << 8;
  gTasks[taskId].data[2] = eva_end;
  gTasks[taskId].data[3] = evb_end;
  gTasks[taskId].data[4] = (s16)((eva_end - eva_start) * 256 / ev_step);
  gTasks[taskId].data[5] = (s16)((evb_end - evb_start) * 256 / ev_step);
  gTasks[taskId].data[8] = ev_step;
  SetGpuReg(REG_OFFSET_BLDALPHA, (u16)((evb_start << 8) | eva_start));
}

bool8
IsBlendTaskActive(void)
{
  return FuncIsActiveTask(Task_SmoothBlendLayers);
}

static void
Task_SmoothBlendLayers(u8 taskId)
{
  s16* data = gTasks[taskId].data;

  if (data[8] == 0) {
    DestroyTask(taskId);
    return;
  }

  if (data[6] == 0) {
    data[0] = (s16)(data[0] + data[4]);
    data[6] = 1;
  } else {
    if (--data[8] != 0) {
      data[1] = (s16)(data[1] + data[5]);
    } else {
      data[0] = (s16)(data[2] << 8);
      data[1] = (s16)(data[3] << 8);
    }

    data[6] = 0;
  }

  SetGpuReg(REG_OFFSET_BLDALPHA,
            (u16)((data[1] & (s16)~0x00FF) | ((u16)data[0] >> 8)));

  if (data[8] == 0) {
    DestroyTask(taskId);
  }
}

void
ResetTempTileDataBuffers(void)
{
  int i;

  for (i = 0; i < (int)NELEMS(sTempTileDataBuffers); i++) {
    sTempTileDataBuffers[i] = NULL;
  }

  sTempTileDataBufferCursor = 0;
}

bool8
FreeTempTileDataBuffersIfPossible(void)
{
  int i;

  if (IsDma3ManagerBusyWithBgCopy()) {
    return TRUE;
  }

  for (i = 0; i < sTempTileDataBufferCursor; i++) {
    Free(sTempTileDataBuffers[i]);
    sTempTileDataBuffers[i] = NULL;
  }

  sTempTileDataBufferCursor = 0;
  return FALSE;
}

void*
DecompressAndCopyTileDataToVram(u8 bgId,
                                const void* src,
                                u32 size,
                                u16 offset,
                                u8 mode)
{
  u32 sizeOut = 0;
  u8* sizeAsBytes = (u8*)&sizeOut;
  const u8* srcAsBytes = (const u8*)src;
  void* ptr;

  if (sTempTileDataBufferCursor >= NELEMS(sTempTileDataBuffers)) {
    return NULL;
  }

  sizeAsBytes[0] = srcAsBytes[1];
  sizeAsBytes[1] = srcAsBytes[2];
  sizeAsBytes[2] = srcAsBytes[3];
  sizeAsBytes[3] = 0;

  ptr = Alloc(sizeOut);
  if (ptr == NULL) {
    return NULL;
  }

  LZ77UnCompWram(src, ptr);

  if (size == 0) {
    size = sizeOut;
  }

  pfr_copy_decompressed_tile_data_to_vram(bgId, ptr, (u16)size, offset, mode);
  sTempTileDataBuffers[sTempTileDataBufferCursor++] = ptr;
  return ptr;
}

void
StoreWordInTwoHalfwords(u16* dest, u32 data)
{
  dest[0] = (u16)data;
  dest[1] = (u16)(data >> 16);
}

void
LoadWordFromTwoHalfwords(u16* src, u32* dest)
{
  *dest = src[0] | ((s16)src[1] << 16);
}

u16 gSaveFileStatus = 0;
u16 gBattle_BG0_X = 0;
u16 gBattle_BG0_Y = 0;
u16 gBattle_BG1_X = 0;
u16 gBattle_BG1_Y = 0;
u16 gBattle_BG2_X = 0;
u16 gBattle_BG2_Y = 0;
u16 gBattle_BG3_X = 0;
u16 gBattle_BG3_Y = 0;
const u32 gMultiBootProgram_PokemonColosseum_Start[1] = { 0 };
bool8 gDifferentSaveFile = FALSE;
u8 gExitStairsMovementDisabled = FALSE;

void
DrawSpindaSpots(u16 species, u32 personality, u8* dest, bool8 isFrontPic)
{
  (void)species;
  (void)personality;
  (void)dest;
  (void)isFrontPic;
}

u8 gDecompressionBuffer[0x4000];
const struct CompressedSpriteSheet gMonFrontPicTable[1] = { 0 };
const struct CompressedSpriteSheet gMonBackPicTable[1] = { 0 };

bool32
IsMysteryGiftEnabled(void)
{
  return FALSE;
}

bool32
IsNationalPokedexEnabled(void)
{
  return FALSE;
}

bool8
FlagGet(u16 id)
{
  (void)id;
  return FALSE;
}

u16
GetNationalPokedexCount(u8 caseId)
{
  (void)caseId;
  return 0;
}

u16
GetKantoPokedexCount(u8 caseId)
{
  (void)caseId;
  return 0;
}

bool8
IsWirelessAdapterConnected(void)
{
  return FALSE;
}

void
HelpSystem_Enable(void)
{
}

void
HelpSystem_Disable(void)
{
}

void
SetHelpContext(u8 context)
{
  (void)context;
}

const u8*
DynamicPlaceholderTextUtil_GetPlaceholderPtr(u8 idx)
{
  (void)idx;
  return sEmptyPlaceholder;
}

static void
pfr_stub_unavailable(const char* message)
{
  snprintf(gPfrRuntimeState.status_line,
           sizeof(gPfrRuntimeState.status_line),
           "%s",
           message);
  pfr_core_request_quit();
}

void
StartNewGameScene(void)
{
  pfr_stub_unavailable("NEW GAME is not implemented in the native port yet.");
}

void
TryStartQuestLogPlayback(u8 taskId)
{
  (void)taskId;
  pfr_stub_unavailable("CONTINUE is not implemented in the native port yet.");
}

void
CB2_InitMysteryGift(void)
{
  pfr_stub_unavailable("Mystery Gift is unavailable in the native port.");
}

void
CB2_SaveClearScreen_Init(void)
{
  pfr_stub_unavailable("Save clear is unavailable in the native port.");
}

void
CB2_InitBerryFixProgram(void)
{
  pfr_stub_unavailable("Berry Fix is unavailable in the native port.");
}
