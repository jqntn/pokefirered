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
#include "gba/m4a_internal.h"
#include "gba/types.h"
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
#include "sound.h"
#include "sprite.h"
#include "window.h"

static bool8 sBgmPlaying;
static bool8 sSePlaying;
static bool8 sCryPlaying;
static u16 sCurrentBgm;
static u16 sCurrentSe;
static u16 sTempTileDataBufferCursor;
static void* sTempTileDataBuffers[32];
static const u8 sEmptyPlaceholder[] = { EOS };

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

struct MusicPlayerInfo gMPlayInfo_BGM = { 0 };
struct MusicPlayerInfo gMPlayInfo_SE1 = { 0 };
struct MusicPlayerInfo gMPlayInfo_SE2 = { 0 };
struct MusicPlayerInfo gMPlayInfo_SE3 = { 0 };
u8 gQuestLogState = 0;
const struct OamData gOamData_AffineOff_ObjNormal_16x16 = { 0 };

void
PlayCry_ByMode(u16 species, s8 pan, u8 mode)
{
  (void)species;
  (void)pan;
  (void)mode;
  sCryPlaying = TRUE;
}

void
PlayCry_Normal(u16 species, s8 pan)
{
  PlayCry_ByMode(species, pan, 0);
}

void
PlaySE(u16 songNum)
{
  sCurrentSe = songNum;
  sSePlaying = TRUE;
}

void
SetPokemonCryStereo(u32 val)
{
  (void)val;
}

void
m4aSongNumStart(u16 n)
{
  sCurrentBgm = n;
  sBgmPlaying = TRUE;
}

void
m4aMPlayAllStop(void)
{
  sBgmPlaying = FALSE;
  sSePlaying = FALSE;
  sCryPlaying = FALSE;
}

void
m4aMPlayStop(struct MusicPlayerInfo* mplayInfo)
{
  if (mplayInfo == &gMPlayInfo_BGM) {
    sBgmPlaying = FALSE;
  }
}

void
m4aMPlayContinue(struct MusicPlayerInfo* mplayInfo)
{
  if (mplayInfo == &gMPlayInfo_BGM && sCurrentBgm != 0) {
    sBgmPlaying = TRUE;
  }
}

bool8
IsNotWaitingForBGMStop(void)
{
  return TRUE;
}

void
FadeOutBGM(u8 speed)
{
  (void)speed;
  sBgmPlaying = FALSE;
}

void
FadeOutMapMusic(u8 speed)
{
  FadeOutBGM(speed);
}

void
PlayBGM(u16 songNum)
{
  m4aSongNumStart(songNum);
}

bool8
IsBGMPlaying(void)
{
  return sBgmPlaying;
}

bool8
IsSEPlaying(void)
{
  bool8 result = sSePlaying;

  sSePlaying = FALSE;
  return result;
}

bool8
IsCryPlaying(void)
{
  bool8 result = sCryPlaying;

  sCryPlaying = FALSE;
  return result;
}

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
  (void)eva_start;
  (void)evb_start;
  (void)eva_end;
  (void)evb_end;
  (void)ev_step;
  (void)priority;
}

bool8
IsBlendTaskActive(void)
{
  return FALSE;
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

u8 gHeap[0x1C000]; // 112KB buffer as per original game
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
BlendPalette(u16 palOffset, u16 numEntries, u8 coeff, u16 blendColor)
{
  (void)palOffset;
  (void)numEntries;
  (void)coeff;
  (void)blendColor;
}

struct Bitmap;
void
BlitBitmapRect4Bit(const struct Bitmap* src,
                   struct Bitmap* dest,
                   u16 srcX,
                   u16 srcY,
                   u16 destX,
                   u16 destY,
                   u16 width,
                   u16 height,
                   u8 colorKey)
{
  (void)src;
  (void)dest;
  (void)srcX;
  (void)srcY;
  (void)destX;
  (void)destY;
  (void)width;
  (void)height;
  (void)colorKey;
}
void
FillBitmapRect4Bit(struct Bitmap* dest,
                   u16 x,
                   u16 y,
                   u16 width,
                   u16 height,
                   u8 colorIndex)
{
  (void)dest;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)colorIndex;
}
void
DrawSpindaSpots(u16 species, u32 personality, u8* dest, bool8 isFrontPic)
{
  (void)species;
  (void)personality;
  (void)dest;
  (void)isFrontPic;
}

u8 gDecompressionBuffer[0x4000]; // 16KB buffer
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

u16
pfr_stub_current_bgm(void)
{
  return sCurrentBgm;
}

bool8
pfr_stub_is_bgm_playing(void)
{
  return sBgmPlaying;
}

bool8
pfr_stub_take_se(u16* songNum)
{
  if (!sSePlaying) {
    return FALSE;
  }

  *songNum = sCurrentSe;
  sSePlaying = FALSE;
  return TRUE;
}

bool8
pfr_stub_take_cry(void)
{
  bool8 active = sCryPlaying;

  sCryPlaying = FALSE;
  return active;
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
