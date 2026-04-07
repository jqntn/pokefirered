/*
 * Integration test — verifies that original game source files link correctly
 * and produce expected results when called through the port shim headers.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "constants/songs.h"
#include "global.h"

#include "bg.h"
#include "characters.h"
#include "dma3.h"
#include "malloc.h"
#include "menu.h"
#include "new_menu_helpers.h"
#include "pfr/audio_assets.h"
#include "pfr/core.h"
#include "pfr/dma.h"
#include "pfr/main_runtime.h"
#include "random.h"
#include "scanline_effect.h"
#include "task.h"
#include "trig.h"
#include "window.h"

static void
test_random(void)
{
  u16 a;
  u16 b;

  SeedRng(42);
  a = Random();
  b = Random();
  assert(a != 0 || b != 0);
  assert(a != b);

  SeedRng(42);
  assert(Random() == a);
  assert(Random() == b);
}

/* Provide the gHeap symbol expected by malloc.h */
static u8 sTestHeap[0x4000];

static void
test_malloc_alloc_free(void)
{
  void* p1;
  void* p2;

  InitHeap(sTestHeap, sizeof(sTestHeap));

  p1 = Alloc(128);
  assert(p1 != NULL);

  p2 = AllocZeroed(64);
  assert(p2 != NULL);
  assert(p1 != p2);

  /* AllocZeroed should produce zeros. */
  {
    u8* bytes = (u8*)p2;
    size_t i;

    for (i = 0; i < 64; i++) {
      assert(bytes[i] == 0);
    }
  }

  Free(p2);
  Free(p1);
}

#include "string_util.h"

static void
test_string_util(void)
{
  u8 buffer[32];
  const u8 testString[] = { 0xBB, 0xCC, 0xDD, 0xFF }; /* "Poké" */

  assert(StringLength(testString) == 3);

  StringCopy(buffer, testString);
  assert(buffer[0] == 0xBB);
  assert(buffer[3] == 0xFF);
}

static void
test_trig(void)
{
  assert(gSineTable[0] == 0);
  assert(gSineTable[64] == Q_8_8(1));
  assert(gSineTable[192] == Q_8_8(-1));
  assert(Sin(64, 256) == 256);
  assert(Cos(0, 256) == 256);
  assert(Sin2(90) == Q_4_12(1));
  assert(Cos2(180) == Q_4_12(-1));
}

static NOINLINE void
test_queue_fixed_hblank_dma(u16 value)
{
  u16 tmp = value;

  pfr_dma_set(0,
              &tmp,
              &REG_BG1HOFS,
              ((u32)(DMA_ENABLE | DMA_START_HBLANK | DMA_REPEAT | DMA_16BIT |
                     DMA_SRC_FIXED | DMA_DEST_FIXED)
               << 16) |
                1U);
}

static NOINLINE void
test_clobber_hblank_dma_stack(void)
{
  volatile u8 scratch[1024];
  size_t i;

  for (i = 0; i < sizeof(scratch); i++) {
    scratch[i] = (u8)i;
  }
}

static void
test_dma_fixed_source_lifetime(void)
{
  memset(gPfrIo, 0, PFR_IO_SIZE);
  pfr_main_init();

  test_queue_fixed_hblank_dma(0x1357);
  test_clobber_hblank_dma_stack();
  REG_VCOUNT = 0;
  pfr_main_on_hblank();
  assert(REG_BG1HOFS == 0x1357);

  test_clobber_hblank_dma_stack();
  REG_VCOUNT = 1;
  pfr_main_on_hblank();
  assert(REG_BG1HOFS == 0x1357);

  DmaStop(0);
  pfr_main_shutdown();
}

static void
test_scanline_effect(void)
{
  struct ScanlineEffectParams params;
  u8 waveTaskId;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  pfr_main_init();
  ResetTasks();
  ScanlineEffect_Clear();

  gScanlineEffectRegBuffers[0][0] = 10;
  gScanlineEffectRegBuffers[0][1] = 20;
  gScanlineEffectRegBuffers[0][2] = 30;

  params.dmaDest = (void*)REG_ADDR_BG1HOFS;
  params.dmaControl = (u32)SCANLINE_EFFECT_DMACNT_16BIT;
  params.initState = 1;
  params.unused9 = 0;
  ScanlineEffect_SetParams(params);
  ScanlineEffect_InitHBlankDmaTransfer();

  assert(REG_BG1HOFS == 10);

  REG_VCOUNT = 0;
  pfr_main_on_hblank();
  assert(REG_BG1HOFS == 20);

  REG_VCOUNT = 1;
  pfr_main_on_hblank();
  assert(REG_BG1HOFS == 30);

  REG_VCOUNT = DISPLAY_HEIGHT;
  pfr_main_on_hblank();
  assert(REG_BG1HOFS == 30);

  waveTaskId =
    ScanlineEffect_InitWave(5, 9, 4, 32, 0, SCANLINE_EFFECT_REG_BG1HOFS, FALSE);
  assert(waveTaskId != TASK_NONE);
  assert(gScanlineEffect.waveTaskId == waveTaskId);
  assert(gTasks[waveTaskId].isActive == TRUE);

  ScanlineEffect_Stop();
  assert(gScanlineEffect.state == 0);
  assert(gScanlineEffect.waveTaskId == 0xFF);
  assert(gTasks[waveTaskId].isActive == FALSE);

  pfr_main_shutdown();
}

static void
test_decompress_bg_tilemap_mode(void)
{
  static const struct BgTemplate sBgTemplate = {
    .bg = 0,
    .charBaseIndex = 0,
    .mapBaseIndex = 31,
    .screenSize = 0,
    .paletteMode = 0,
    .priority = 0,
    .baseTile = 0,
  };
  static const u8 sCompressedTilemap[] = { 0x10, 0x04, 0x00, 0x00, 0x00,
                                           0x34, 0x12, 0x00, 0x00 };
  size_t mapOffset = 31U * BG_SCREEN_SIZE;

  memset(gPfrVram, 0, BG_VRAM_SIZE);
  ResetBgsAndClearDma3BusyFlags(FALSE);
  InitBgsFromTemplates(0, &sBgTemplate, 1);
  ShowBg(0);
  ClearDma3Requests();

  DecompressAndCopyTileDataToVram(0, sCompressedTilemap, 0, 0, 1);
  ProcessDma3Requests();

  assert(*(u16*)(gPfrVram + mapOffset) == 0x1234);
  assert(*(u16*)gPfrVram == 0);
}

static void
test_decompress_tile_data_buffers_survive_dma(void)
{
  static const struct BgTemplate sBgTemplate = {
    .bg = 0,
    .charBaseIndex = 0,
    .mapBaseIndex = 31,
    .screenSize = 0,
    .paletteMode = 0,
    .priority = 0,
    .baseTile = 0,
  };
  static const u8 sCompressedTileA[] = {
    0x10, 0x20, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11
  };
  static const u8 sCompressedTileB[] = {
    0x10, 0x20, 0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x00, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x00, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x00, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22
  };

  memset(gPfrVram, 0, BG_VRAM_SIZE);
  ResetBgsAndClearDma3BusyFlags(FALSE);
  InitBgsFromTemplates(0, &sBgTemplate, 1);
  ShowBg(0);
  ClearDma3Requests();
  ResetTempTileDataBuffers();

  DecompressAndCopyTileDataToVram(0, sCompressedTileA, 0, 0, 0);
  DecompressAndCopyTileDataToVram(0, sCompressedTileB, 0, 1, 0);
  ProcessDma3Requests();

  assert(gPfrVram[0x0000] == 0x11);
  assert(gPfrVram[0x001F] == 0x11);
  assert(gPfrVram[0x0020] == 0x22);
  assert(gPfrVram[0x003F] == 0x22);
  assert(FreeTempTileDataBuffersIfPossible() == FALSE);
}

static void
test_text_printer_embeds_font_assets(void)
{
  static const struct BgTemplate sBgTemplate = {
    .bg = 0,
    .charBaseIndex = 0,
    .mapBaseIndex = 31,
    .screenSize = 0,
    .paletteMode = 0,
    .priority = 0,
    .baseTile = 0,
  };
  static const struct WindowTemplate sWindowTemplates[] = {
    {
      .bg = 0,
      .tilemapLeft = 0,
      .tilemapTop = 0,
      .width = 8,
      .height = 2,
      .paletteNum = 0,
      .baseBlock = 1,
    },
    DUMMY_WIN_TEMPLATE,
  };
  static const u8 sText[] = { CHAR_A, CHAR_B, CHAR_C, EOS };
  static const u8 sTextColors[] = { 0, 1, 2 };
  bool changed = false;
  size_t i;

  memset(gPfrVram, 0, BG_VRAM_SIZE);
  InitHeap(sTestHeap, sizeof(sTestHeap));
  ResetBgsAndClearDma3BusyFlags(FALSE);
  InitBgsFromTemplates(0, &sBgTemplate, 1);
  ShowBg(0);
  SetDefaultFontsPointer();
  InitWindows(sWindowTemplates);
  DeactivateAllTextPrinters();
  FillWindowPixelBuffer(0, PIXEL_FILL(0));

  AddTextPrinterParameterized3(
    0, FONT_NORMAL, 0, 0, sTextColors, TEXT_SPEED_INSTANT, sText);

  for (i = 0; i < 0x20U * 8U * 2U; i++) {
    if (gWindows[0].tileData[i] != 0) {
      changed = true;
      break;
    }
  }

  assert(changed);

  ClearDma3Requests();
  CopyWindowToVram(0, COPYWIN_GFX);
  ProcessDma3Requests();

  changed = false;
  for (i = 0; i < 0x20U * 8U * 2U; i++) {
    if (gPfrVram[i] != 0) {
      changed = true;
      break;
    }
  }

  assert(changed);
  FreeAllWindowBuffers();
}

static void
test_bg_tilemap_buffer_writes_use_native_heap_storage(void)
{
  static const struct BgTemplate sBgTemplate = {
    .bg = 0,
    .charBaseIndex = 0,
    .mapBaseIndex = 31,
    .screenSize = 0,
    .paletteMode = 0,
    .priority = 0,
    .baseTile = 0,
  };
  static const struct WindowTemplate sWindowTemplates[] = {
    {
      .bg = 0,
      .tilemapLeft = 2,
      .tilemapTop = 1,
      .width = 4,
      .height = 2,
      .paletteNum = 3,
      .baseBlock = 1,
    },
    DUMMY_WIN_TEMPLATE,
  };
  const u16* tilemap;

  InitHeap(sTestHeap, sizeof(sTestHeap));
  ResetBgsAndClearDma3BusyFlags(FALSE);
  InitBgsFromTemplates(0, &sBgTemplate, 1);
  ShowBg(0);
  InitWindows(sWindowTemplates);

  PutWindowTilemap(0);
  tilemap = (const u16*)GetBgTilemapBuffer(0);

  assert(tilemap != NULL);
  assert(tilemap[1U * 32U + 2U] != 0);

  FillBgTilemapBufferRect(0, 0x123, 0, 0, 1, 1, 5);
  assert(tilemap[0] == (0x123 | (5 << 12)));

  FreeAllWindowBuffers();
}

static void
test_audio_catalog_import(void)
{
  assert(gPfrAudioSongAssetCount == MUS_TEACHY_TV_MENU + 1);
  assert(gSongTable[MUS_DUMMY].header != NULL);
  assert(gSongTable[MUS_DUMMY].header->trackCount == 0);
  assert(gSongTable[SE_SELECT].header != NULL);
  assert(gSongTable[MUS_GAME_FREAK].header != NULL);
  assert(gPfrAudioSongAssets[SE_SELECT].tracks != NULL);
  assert(gPfrAudioSongAssets[MUS_GAME_FREAK].tracks != NULL);
  assert(pfr_audio_song_asset_for_id(SE_SELECT) ==
         &gPfrAudioSongAssets[SE_SELECT]);
  assert(pfr_audio_song_asset_for_header(gSongTable[SE_SELECT].header) ==
         &gPfrAudioSongAssets[SE_SELECT]);
  assert(gSongTable[SE_SELECT].ms == 2);
  assert(gSongTable[SE_SELECT].me == 2);
  assert(gSongTable[SE_LOW_HEALTH].ms == 3);
  assert(gSongTable[SE_LOW_HEALTH].me == 3);
  assert(gSongTable[MUS_GAME_FREAK].ms == 0);
  assert(gSongTable[MUS_GAME_FREAK].me == 0);
  assert(gSongTable[MUS_HEAL].ms == 2);
  assert(gSongTable[MUS_HEAL].me == 2);
  assert(gCryTable[0].wav != NULL);
  assert(gCryTable_Reverse[0].wav != NULL);
}

int
main(void)
{
  test_random();
  test_malloc_alloc_free();
  test_string_util();
  test_trig();
  test_dma_fixed_source_lifetime();
  test_scanline_effect();
  test_decompress_bg_tilemap_mode();
  test_decompress_tile_data_buffers_survive_dma();
  test_text_printer_embeds_font_assets();
  test_bg_tilemap_buffer_writes_use_native_heap_storage();
  test_audio_catalog_import();
  puts("pfr_integration: ok");
  return 0;
}
