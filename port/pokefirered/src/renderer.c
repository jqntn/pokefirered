#include "gba/defines.h"
#include "gba/io_reg.h"
#include "gba/types.h"
#include <string.h>
#include "pfr/common.h"
#include "pfr/renderer.h"

typedef struct PfrPixelSample
{
  bool opaque;
  u8 priority;
  u8 order;
  u16 color;
} PfrPixelSample;

static uint32_t sFramebuffer[PFR_SCREEN_WIDTH * PFR_SCREEN_HEIGHT];

static const int sSpriteDimensions[3][4][2] = {
  { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
  { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
  { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
};

static uint32_t
pfr_rgb555_to_rgba8888(u16 color)
{
  uint32_t r = (uint32_t)(color & 31U) * 255U / 31U;
  uint32_t g = (uint32_t)((color >> 5) & 31U) * 255U / 31U;
  uint32_t b = (uint32_t)((color >> 10) & 31U) * 255U / 31U;

  return 0xFF000000U | (b << 16) | (g << 8) | r;
}

static size_t
pfr_text_screen_entry_offset(int map_width_tiles, int tile_x, int tile_y)
{
  int block_x = tile_x / 32;
  int block_y = tile_y / 32;
  int local_x = tile_x % 32;
  int local_y = tile_y % 32;
  int blocks_per_row = map_width_tiles / 32;

  return (size_t)(block_y * blocks_per_row + block_x) * 0x800U +
         (size_t)(local_y * 32 + local_x) * sizeof(u16);
}

static PfrPixelSample
pfr_sample_text_bg(int bg_index, int screen_x, int screen_y)
{
  static const u16 sBgCntOffsets[] = {
    REG_OFFSET_BG0CNT,
    REG_OFFSET_BG1CNT,
    REG_OFFSET_BG2CNT,
    REG_OFFSET_BG3CNT,
  };
  static const u16 sBgHofsOffsets[] = {
    REG_OFFSET_BG0HOFS,
    REG_OFFSET_BG1HOFS,
    REG_OFFSET_BG2HOFS,
    REG_OFFSET_BG3HOFS,
  };
  static const u16 sBgVofsOffsets[] = {
    REG_OFFSET_BG0VOFS,
    REG_OFFSET_BG1VOFS,
    REG_OFFSET_BG2VOFS,
    REG_OFFSET_BG3VOFS,
  };
  const u16* palette = (const u16*)gPfrPltt;
  u16 bgcnt = *(const vu16*)(gPfrIo + sBgCntOffsets[bg_index]);
  int screen_size = (bgcnt >> 14) & 3;
  int map_width_tiles = ((screen_size & 1) != 0) ? 64 : 32;
  int map_height_tiles = (screen_size >= 2) ? 64 : 32;
  int char_base = ((bgcnt >> 2) & 3) * BG_CHAR_SIZE;
  int screen_base = ((bgcnt >> 8) & 31) * BG_SCREEN_SIZE;
  int x = (screen_x + *(const vu16*)(gPfrIo + sBgHofsOffsets[bg_index])) &
          (map_width_tiles * 8 - 1);
  int y = (screen_y + *(const vu16*)(gPfrIo + sBgVofsOffsets[bg_index])) &
          (map_height_tiles * 8 - 1);
  int tile_x = x / 8;
  int tile_y = y / 8;
  size_t screen_offset =
    screen_base + pfr_text_screen_entry_offset(map_width_tiles, tile_x, tile_y);
  u16 entry;
  int tile_number;
  int palette_bank;
  int pixel_x;
  int pixel_y;
  size_t tile_offset;
  uint8_t color_index;
  PfrPixelSample sample = { false, (u8)(bgcnt & 3), (u8)bg_index, 0 };

  if (screen_offset + sizeof(u16) > BG_VRAM_SIZE) {
    return sample;
  }

  entry = *(const u16*)(gPfrVram + screen_offset);
  tile_number = entry & 0x03FF;
  palette_bank = (entry >> 12) & 0x0F;
  pixel_x = x & 7;
  pixel_y = y & 7;

  if ((entry & 0x0400) != 0) {
    pixel_x = 7 - pixel_x;
  }

  if ((entry & 0x0800) != 0) {
    pixel_y = 7 - pixel_y;
  }

  if ((bgcnt & 0x0080) != 0) {
    tile_offset = (size_t)char_base + (size_t)tile_number * 64U +
                  (size_t)pixel_y * 8U + (size_t)pixel_x;

    if (tile_offset >= BG_VRAM_SIZE) {
      return sample;
    }

    color_index = gPfrVram[tile_offset];
    if (color_index == 0) {
      return sample;
    }

    sample.opaque = true;
    sample.color = palette[color_index];
    return sample;
  }

  tile_offset = (size_t)char_base + (size_t)tile_number * 32U +
                (size_t)pixel_y * 4U + (size_t)pixel_x / 2U;

  if (tile_offset >= BG_VRAM_SIZE) {
    return sample;
  }

  color_index = gPfrVram[tile_offset];
  if ((pixel_x & 1) != 0) {
    color_index >>= 4;
  }

  color_index &= 0x0F;
  if (color_index == 0) {
    return sample;
  }

  sample.opaque = true;
  sample.color = palette[palette_bank * 16 + color_index];
  return sample;
}

static PfrPixelSample
pfr_sample_affine_bg(int bg_index, int screen_x, int screen_y)
{
  static const u16 sBgCntOffsets[] = {
    REG_OFFSET_BG2CNT,
    REG_OFFSET_BG3CNT,
  };
  const u16* palette = (const u16*)gPfrPltt;
  int slot = bg_index - 2;
  u16 bgcnt = *(const vu16*)(gPfrIo + sBgCntOffsets[slot]);
  int map_size = 128 << ((bgcnt >> 14) & 3);
  int map_tiles = map_size / 8;
  int char_base = ((bgcnt >> 2) & 3) * BG_CHAR_SIZE;
  int screen_base = ((bgcnt >> 8) & 31) * BG_SCREEN_SIZE;
  int wrap = (bgcnt >> 13) & 1;
  s32 pa = (bg_index == 2) ? REG_BG2PA : REG_BG3PA;
  s32 pb = (bg_index == 2) ? REG_BG2PB : REG_BG3PB;
  s32 pc = (bg_index == 2) ? REG_BG2PC : REG_BG3PC;
  s32 pd = (bg_index == 2) ? REG_BG2PD : REG_BG3PD;
  s32 ref_x = (bg_index == 2) ? REG_BG2X : REG_BG3X;
  s32 ref_y = (bg_index == 2) ? REG_BG2Y : REG_BG3Y;
  s32 tex_x = ref_x + pa * screen_x + pb * screen_y;
  s32 tex_y = ref_y + pc * screen_x + pd * screen_y;
  int src_x = tex_x >> 8;
  int src_y = tex_y >> 8;
  size_t map_offset;
  int tile_number;
  size_t tile_offset;
  uint8_t color_index;
  PfrPixelSample sample = { false, (u8)(bgcnt & 3), (u8)bg_index, 0 };

  if (wrap != 0) {
    src_x %= map_size;
    src_y %= map_size;

    if (src_x < 0) {
      src_x += map_size;
    }

    if (src_y < 0) {
      src_y += map_size;
    }
  } else if (src_x < 0 || src_y < 0 || src_x >= map_size || src_y >= map_size) {
    return sample;
  }

  map_offset = (size_t)screen_base + (size_t)(src_y / 8) * (size_t)map_tiles +
               (size_t)(src_x / 8);

  if (map_offset >= BG_VRAM_SIZE) {
    return sample;
  }

  tile_number = gPfrVram[map_offset];
  tile_offset = (size_t)char_base + (size_t)tile_number * 64U +
                (size_t)(src_y & 7) * 8U + (size_t)(src_x & 7);

  if (tile_offset >= BG_VRAM_SIZE) {
    return sample;
  }

  color_index = gPfrVram[tile_offset];
  sample.opaque = true;
  sample.color = palette[color_index];
  return sample;
}

static PfrPixelSample
pfr_sample_backgrounds(int screen_x, int screen_y)
{
  u16 dispcnt = REG_DISPCNT;
  u16 backdrop = ((const u16*)gPfrPltt)[0];
  PfrPixelSample best = { true, 4, 4, backdrop };
  int mode = dispcnt & 7;
  int bg_index;

  for (bg_index = 0; bg_index < 4; bg_index++) {
    u16 bg_bit = (u16)(DISPCNT_BG0_ON << bg_index);
    PfrPixelSample sample;
    bool uses_text = false;
    bool uses_affine = false;

    if ((dispcnt & bg_bit) == 0) {
      continue;
    }

    if (mode == 0) {
      uses_text = true;
    } else if (mode == 1) {
      uses_text = bg_index < 2;
    } else if (mode == 2) {
      uses_affine = bg_index >= 2;
    }

    if (mode == 1 && bg_index == 2) {
      uses_affine = true;
    }

    if (!uses_text && !uses_affine) {
      continue;
    }

    if (uses_text) {
      sample = pfr_sample_text_bg(bg_index, screen_x, screen_y);
    } else {
      sample = pfr_sample_affine_bg(bg_index, screen_x, screen_y);
    }

    if (!sample.opaque) {
      continue;
    }

    if (sample.priority < best.priority ||
        (sample.priority == best.priority && sample.order < best.order)) {
      best = sample;
    }
  }

  return best;
}

static PfrPixelSample
pfr_sample_sprite_pixel(int screen_x, int screen_y)
{
  const struct OamData* oam = (const struct OamData*)gPfrOam;
  const u16* palette = (const u16*)gPfrPltt;
  bool one_d_mapping = (REG_DISPCNT & DISPCNT_OBJ_1D_MAP) != 0;
  PfrPixelSample best = { false, 4, 0xFF, 0 };
  int sprite_index;

  for (sprite_index = 0; sprite_index < 128; sprite_index++) {
    const struct OamData* entry = &oam[sprite_index];
    int shape = entry->shape;
    int size = entry->size;
    int width;
    int height;
    int origin_x;
    int origin_y;
    int local_x;
    int local_y;
    int tile_num;
    int tile_stride;
    int tile_x;
    int tile_y;
    int pixel_x;
    int pixel_y;
    size_t tile_offset;
    uint8_t color_index;
    PfrPixelSample sample;

    if (shape > ST_OAM_V_RECTANGLE || size > ST_OAM_SIZE_3) {
      continue;
    }

    if (entry->objMode == ST_OAM_OBJ_WINDOW) {
      continue;
    }

    width = sSpriteDimensions[shape][size][0];
    height = sSpriteDimensions[shape][size][1];
    origin_x = (int)entry->x;
    origin_y = (int)entry->y;

    if (origin_x >= 256) {
      origin_x -= 512;
    }

    if (origin_y >= DISPLAY_HEIGHT) {
      origin_y -= 256;
    }

    if (screen_x < origin_x || screen_y < origin_y ||
        screen_x >= origin_x + width || screen_y >= origin_y + height) {
      continue;
    }

    local_x = screen_x - origin_x;
    local_y = screen_y - origin_y;

    if (entry->affineMode == ST_OAM_AFFINE_OFF) {
      if ((entry->matrixNum & ST_OAM_HFLIP) != 0) {
        local_x = width - 1 - local_x;
      }

      if ((entry->matrixNum & ST_OAM_VFLIP) != 0) {
        local_y = height - 1 - local_y;
      }
    }

    tile_num = entry->tileNum;
    tile_stride = (entry->bpp == ST_OAM_8BPP) ? 2 : 1;
    tile_x = local_x / 8;
    tile_y = local_y / 8;
    pixel_x = local_x & 7;
    pixel_y = local_y & 7;

    if (one_d_mapping) {
      tile_num += (tile_y * (width / 8) + tile_x) * tile_stride;
    } else {
      tile_num += (tile_y * 32 + tile_x) * tile_stride;
    }

    tile_offset = 0x10000U + (size_t)tile_num * 32U;

    if (entry->bpp == ST_OAM_8BPP) {
      tile_offset += (size_t)pixel_y * 8U + (size_t)pixel_x;

      if (tile_offset >= VRAM_SIZE) {
        continue;
      }

      color_index = gPfrVram[tile_offset];
      if (color_index == 0) {
        continue;
      }

      sample.opaque = true;
      sample.priority = (u8)entry->priority;
      sample.order = (u8)sprite_index;
      sample.color = palette[0x100 + color_index];
    } else {
      tile_offset += (size_t)pixel_y * 4U + (size_t)pixel_x / 2U;

      if (tile_offset >= VRAM_SIZE) {
        continue;
      }

      color_index = gPfrVram[tile_offset];
      if ((pixel_x & 1) != 0) {
        color_index >>= 4;
      }

      color_index &= 0x0F;
      if (color_index == 0) {
        continue;
      }

      sample.opaque = true;
      sample.priority = (u8)entry->priority;
      sample.order = (u8)sprite_index;
      sample.color = palette[0x100 + entry->paletteNum * 16 + color_index];
    }

    if (!best.opaque || sample.priority < best.priority ||
        (sample.priority == best.priority && sample.order < best.order)) {
      best = sample;
    }
  }

  return best;
}

void
pfr_renderer_init(void)
{
  memset(sFramebuffer, 0, sizeof(sFramebuffer));
}

void
pfr_renderer_shutdown(void)
{
}

void
pfr_renderer_render_frame(void)
{
  int x;
  int y;

  for (y = 0; y < DISPLAY_HEIGHT; y++) {
    for (x = 0; x < DISPLAY_WIDTH; x++) {
      PfrPixelSample bg_sample = pfr_sample_backgrounds(x, y);
      PfrPixelSample obj_sample = pfr_sample_sprite_pixel(x, y);
      u16 final_color = bg_sample.color;

      if (obj_sample.opaque && obj_sample.priority <= bg_sample.priority) {
        final_color = obj_sample.color;
      }

      sFramebuffer[y * DISPLAY_WIDTH + x] = pfr_rgb555_to_rgba8888(final_color);
    }
  }
}

const uint32_t*
pfr_renderer_framebuffer(void)
{
  return sFramebuffer;
}

uint32_t
pfr_renderer_checksum(void)
{
  uint32_t hash = 2166136261U;
  size_t i;

  for (i = 0; i < PFR_ARRAY_COUNT(sFramebuffer); i++) {
    hash ^= sFramebuffer[i];
    hash *= 16777619U;
  }

  return hash;
}
