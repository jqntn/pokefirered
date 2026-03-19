#include "gba/io_reg.h"
#include "gba/types.h"
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "pfr/core.h"
#include "pfr/demo.h"
#include "task.h"

enum
{
  PFR_BG_SCREENBLOCK = 31,
  PFR_BG_TILE_COUNT = 8,
  PFR_SPRITE_SIZE = 16,
};

static int sSpriteX = 112;
static int sSpriteY = 72;
static bool sSpriteVisible = true;

static void
pfr_demo_main_callback(void);
static void
pfr_demo_task(u8 taskId);

static u16
pfr_rgb555(u8 r, u8 g, u8 b)
{
  return (u16)((r & 31) | ((g & 31) << 5) | ((b & 31) << 10));
}

static void
pfr_set_tile_pixel_4bpp(uint8_t* tile, int x, int y, uint8_t value)
{
  size_t offset = (size_t)y * 4U + (size_t)x / 2U;

  if ((x & 1) != 0) {
    tile[offset] = (uint8_t)((tile[offset] & 0x0F) | (value << 4));
  } else {
    tile[offset] = (uint8_t)((tile[offset] & 0xF0) | (value & 0x0F));
  }
}

static void
pfr_build_bg_tiles(void)
{
  int tile_index;
  int x;
  int y;

  memset(gPfrVram, 0, PFR_BG_TILE_COUNT * 32);

  for (tile_index = 0; tile_index < PFR_BG_TILE_COUNT; tile_index++) {
    uint8_t* tile = gPfrVram + tile_index * 32;

    for (y = 0; y < 8; y++) {
      for (x = 0; x < 8; x++) {
        uint8_t color = (uint8_t)(((x + y + tile_index) % 6) + 1);

        if ((tile_index & 1) != 0 && ((x + y) & 1) == 0) {
          color = (uint8_t)((color + 2) % 15);
        }

        pfr_set_tile_pixel_4bpp(tile, x, y, color);
      }
    }
  }
}

static void
pfr_build_bg_map(void)
{
  u16* tilemap = (u16*)(gPfrVram + PFR_BG_SCREENBLOCK * 0x800);
  int x;
  int y;

  for (y = 0; y < 32; y++) {
    for (x = 0; x < 32; x++) {
      u16 tile = (u16)(((x + y) % (PFR_BG_TILE_COUNT - 1)) + 1);
      u16 palette = (u16)(((x / 4) + (y / 4)) & 3) << 12;

      tilemap[y * 32 + x] = (u16)(tile | palette);
    }
  }
}

static void
pfr_build_sprite_tiles(void)
{
  uint8_t* sprite_base = gPfrVram + 0x10000;
  int tile_index;
  int x;
  int y;

  memset(sprite_base, 0, 4 * 32);

  for (tile_index = 0; tile_index < 4; tile_index++) {
    uint8_t* tile = sprite_base + tile_index * 32;

    for (y = 0; y < 8; y++) {
      for (x = 0; x < 8; x++) {
        uint8_t color = (uint8_t)(((x ^ y) & 3) + 8);

        if ((x == 0) || (y == 0) || (x == 7) || (y == 7)) {
          color = 15;
        }

        pfr_set_tile_pixel_4bpp(tile, x, y, color);
      }
    }
  }
}

static void
pfr_refresh_palette(void)
{
  u16* palette = (u16*)gPfrPltt;
  uint8_t seed = gPfrRuntimeState.save[0];

  palette[0] = pfr_rgb555(2, 3, 5);
  palette[1] = pfr_rgb555((u8)(4 + (seed & 3)), 6, 12);
  palette[2] = pfr_rgb555((u8)(7 + ((seed >> 1) & 3)), 10, 18);
  palette[3] = pfr_rgb555((u8)(10 + ((seed >> 2) & 3)), 14, 22);
  palette[4] = pfr_rgb555(12, 20, 8);
  palette[5] = pfr_rgb555(18, 10, 7);
  palette[6] = pfr_rgb555(24, 16, 5);
  palette[7] = pfr_rgb555(30, 24, 8);

  palette[0x100 + 0] = pfr_rgb555(0, 0, 0);
  palette[0x100 + 8] = pfr_rgb555(31, 12, 8);
  palette[0x100 + 9] = pfr_rgb555(31, 18, 12);
  palette[0x100 + 10] = pfr_rgb555(31, 24, 16);
  palette[0x100 + 11] = pfr_rgb555(31, 28, 18);
  palette[0x100 + 12] = pfr_rgb555(26, 31, 18);
  palette[0x100 + 13] = pfr_rgb555(20, 31, 20);
  palette[0x100 + 14] = pfr_rgb555(12, 31, 24);
  palette[0x100 + 15] = pfr_rgb555(31, 31, 31);
}

static void
pfr_update_sprite_oam(void)
{
  struct OamData* oam = gMain.oamBuffer;

  memset(oam, 0, sizeof(struct OamData) * 128);
  oam[0].y = (u32)(sSpriteVisible ? sSpriteY : 200);
  oam[0].affineMode = ST_OAM_AFFINE_OFF;
  oam[0].objMode = ST_OAM_OBJ_NORMAL;
  oam[0].mosaic = FALSE;
  oam[0].bpp = ST_OAM_4BPP;
  oam[0].shape = ST_OAM_SQUARE;
  oam[0].x = (u32)sSpriteX;
  oam[0].matrixNum = 0;
  oam[0].size = ST_OAM_SIZE_1;
  oam[0].tileNum = 0;
  oam[0].priority = 0;
  oam[0].paletteNum = 0;
}

static void
pfr_update_status(void)
{
  snprintf(gPfrRuntimeState.status_line,
           sizeof(gPfrRuntimeState.status_line),
           "PAL demo | frame=%lu | save=%u | sprite=(%d,%d) | keys=%03X",
           (unsigned long)gPfrRuntimeState.frame_counter,
           (unsigned int)gPfrRuntimeState.save[0],
           sSpriteX,
           sSpriteY,
           (unsigned int)gPfrRuntimeState.keys_held);
}

void
pfr_demo_boot(void)
{
  sSpriteX = 112;
  sSpriteY = 72;
  sSpriteVisible = true;

  pfr_build_bg_tiles();
  pfr_build_bg_map();
  pfr_build_sprite_tiles();
  pfr_refresh_palette();

  REG_DISPCNT =
    DISPCNT_MODE_0 | DISPCNT_BG0_ON | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP;
  REG_BG0CNT = (u16)(PFR_BG_SCREENBLOCK << 8);
  REG_BG0HOFS = 0;
  REG_BG0VOFS = 0;

  gPfrRuntimeState.title_visible = true;
  CreateTask(pfr_demo_task, 0);
  SetMainCallback2(pfr_demo_main_callback);
  pfr_update_sprite_oam();
  pfr_update_status();
}

void
pfr_demo_run_frame(void)
{
  if ((gMain.heldKeys & DPAD_LEFT) != 0 && sSpriteX > 0) {
    sSpriteX--;
  }

  if ((gMain.heldKeys & DPAD_RIGHT) != 0 &&
      sSpriteX < DISPLAY_WIDTH - PFR_SPRITE_SIZE) {
    sSpriteX++;
  }

  if ((gMain.heldKeys & DPAD_UP) != 0 && sSpriteY > 0) {
    sSpriteY--;
  }

  if ((gMain.heldKeys & DPAD_DOWN) != 0 &&
      sSpriteY < DISPLAY_HEIGHT - PFR_SPRITE_SIZE) {
    sSpriteY++;
  }

  if ((gMain.newKeys & A_BUTTON) != 0) {
    gPfrRuntimeState.save[0]++;
    gPfrRuntimeState.save_dirty = true;
    pfr_refresh_palette();
  }

  if ((gMain.newKeys & B_BUTTON) != 0) {
    sSpriteVisible = !sSpriteVisible;
  }

  if ((gMain.newKeys & START_BUTTON) != 0) {
    gPfrRuntimeState.save[1]++;
    gPfrRuntimeState.save_dirty = true;
  }

  REG_BG0HOFS = (u16)(gPfrRuntimeState.frame_counter & 0xFF);
  REG_BG0VOFS = (u16)((gPfrRuntimeState.frame_counter >> 1) & 0xFF);

  pfr_update_sprite_oam();
  pfr_update_status();
}

static void
pfr_demo_main_callback(void)
{
  RunTasks();
}

static void
pfr_demo_task(u8 taskId)
{
  (void)taskId;
  pfr_demo_run_frame();
}
