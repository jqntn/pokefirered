#include "pfr/renderer.h"
#include "gba/defines.h"
#include "gba/io_reg.h"
#include "gba/types.h"
#include "pfr/common.h"
#include <string.h>

typedef struct PfrPixelSample
{
  bool opaque;
  u8 priority;
  u8 order;
  u16 color;
  u8 layer;
} PfrPixelSample;

typedef struct PfrWindowState
{
  u8 visible_layers;
  bool color_effect_enabled;
} PfrWindowState;

typedef struct PfrLineRegs
{
  u16 dispcnt;
  u16 mosaic;
  u16 bgcnt[4];
  u16 bghofs[4];
  u16 bgvofs[4];
  s16 bgpa[2];
  s16 bgpb[2];
  s16 bgpc[2];
  s16 bgpd[2];
  s32 bgx[2];
  s32 bgy[2];
  u16 winin;
  u16 winout;
  u16 win0h;
  u16 win0v;
  u16 win1h;
  u16 win1v;
  u16 bldcnt;
  u16 bldalpha;
  u16 bldy;
  bool valid;
} PfrLineRegs;

static uint32_t sFramebuffer[PFR_SCREEN_WIDTH * PFR_SCREEN_HEIGHT];
static PfrLineRegs sLineRegs[DISPLAY_HEIGHT];
static u8 sScanlineSpriteCounts[DISPLAY_HEIGHT];
static u8 sScanlineSpriteIndices[DISPLAY_HEIGHT][32];

enum
{
  PFR_MAX_SPRITES_PER_SCANLINE = 32,
  PFR_LAYER_BG0 = 1 << 0,
  PFR_LAYER_BG1 = 1 << 1,
  PFR_LAYER_BG2 = 1 << 2,
  PFR_LAYER_BG3 = 1 << 3,
  PFR_LAYER_OBJ = 1 << 4,
  PFR_LAYER_BD = 1 << 5,
};

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

static bool
pfr_sample_precedes(PfrPixelSample lhs, PfrPixelSample rhs)
{
  bool lhs_is_obj = lhs.layer == PFR_LAYER_OBJ;
  bool rhs_is_obj = rhs.layer == PFR_LAYER_OBJ;

  if (!lhs.opaque) {
    return false;
  }

  if (!rhs.opaque) {
    return true;
  }

  if (lhs.priority != rhs.priority) {
    return lhs.priority < rhs.priority;
  }

  if (lhs_is_obj != rhs_is_obj) {
    return lhs_is_obj;
  }

  return lhs.order < rhs.order;
}

static void
pfr_consider_sample(PfrPixelSample* best,
                    PfrPixelSample* second,
                    PfrPixelSample sample)
{
  if (!sample.opaque) {
    return;
  }

  if (!best->opaque || pfr_sample_precedes(sample, *best)) {
    *second = *best;
    *best = sample;
  } else if (!second->opaque || pfr_sample_precedes(sample, *second)) {
    *second = sample;
  }
}

static u16
pfr_lighten_color(u16 color, int coeff)
{
  int r = color & 31;
  int g = (color >> 5) & 31;
  int b = (color >> 10) & 31;

  if (coeff < 0) {
    coeff = 0;
  } else if (coeff > 16) {
    coeff = 16;
  }

  r += ((31 - r) * coeff) / 16;
  g += ((31 - g) * coeff) / 16;
  b += ((31 - b) * coeff) / 16;
  return (u16)(r | (g << 5) | (b << 10));
}

static u16
pfr_darken_color(u16 color, int coeff)
{
  int r = color & 31;
  int g = (color >> 5) & 31;
  int b = (color >> 10) & 31;

  if (coeff < 0) {
    coeff = 0;
  } else if (coeff > 16) {
    coeff = 16;
  }

  r -= (r * coeff) / 16;
  g -= (g * coeff) / 16;
  b -= (b * coeff) / 16;
  return (u16)(r | (g << 5) | (b << 10));
}

static u16
pfr_blend_colors(u16 lhs, u16 rhs, int eva, int evb)
{
  int lhs_r = lhs & 31;
  int lhs_g = (lhs >> 5) & 31;
  int lhs_b = (lhs >> 10) & 31;
  int rhs_r = rhs & 31;
  int rhs_g = (rhs >> 5) & 31;
  int rhs_b = (rhs >> 10) & 31;
  int out_r;
  int out_g;
  int out_b;

  if (eva < 0) {
    eva = 0;
  } else if (eva > 16) {
    eva = 16;
  }

  if (evb < 0) {
    evb = 0;
  } else if (evb > 16) {
    evb = 16;
  }

  out_r = (lhs_r * eva + rhs_r * evb) / 16;
  out_g = (lhs_g * eva + rhs_g * evb) / 16;
  out_b = (lhs_b * eva + rhs_b * evb) / 16;

  if (out_r > 31) {
    out_r = 31;
  }

  if (out_g > 31) {
    out_g = 31;
  }

  if (out_b > 31) {
    out_b = 31;
  }

  return (u16)(out_r | (out_g << 5) | (out_b << 10));
}

static void
pfr_apply_bg_mosaic(int* screen_x, int* screen_y, u16 bgcnt, u16 mosaic)
{
  int h_size;
  int v_size;

  if ((bgcnt & BGCNT_MOSAIC) == 0) {
    return;
  }

  h_size = (mosaic & 0x000F) + 1;
  v_size = ((mosaic >> 4) & 0x000F) + 1;
  *screen_x -= *screen_x % h_size;
  *screen_y -= *screen_y % v_size;
}

static bool
pfr_coord_in_window_range(int coord, int limit, u16 range)
{
  int start = (range >> 8) & 0xFF;
  int end = range & 0xFF;

  if (start >= limit) {
    start = limit - 1;
  }

  if (end >= limit) {
    end = limit;
  }

  if (start <= end) {
    return coord >= start && coord < end;
  }

  return coord >= start || coord < end;
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
pfr_sample_text_bg(const PfrLineRegs* line_regs,
                   int bg_index,
                   int screen_x,
                   int screen_y)
{
  const u16* palette = (const u16*)gPfrPltt;
  u16 bgcnt = line_regs->bgcnt[bg_index];
  int screen_size = (bgcnt >> 14) & 3;
  int map_width_tiles = ((screen_size & 1) != 0) ? 64 : 32;
  int map_height_tiles = (screen_size >= 2) ? 64 : 32;
  int char_base = ((bgcnt >> 2) & 3) * BG_CHAR_SIZE;
  int screen_base = ((bgcnt >> 8) & 31) * BG_SCREEN_SIZE;
  int x;
  int y;
  int tile_x;
  int tile_y;
  size_t screen_offset;
  u16 entry;
  int tile_number;
  int palette_bank;
  int pixel_x;
  int pixel_y;
  size_t tile_offset;
  uint8_t color_index;
  PfrPixelSample sample = {
    .opaque = false,
    .priority = (u8)(bgcnt & 3),
    .order = (u8)bg_index,
    .color = 0,
    .layer = 0,
  };

  pfr_apply_bg_mosaic(&screen_x, &screen_y, bgcnt, line_regs->mosaic);
  x = (screen_x + line_regs->bghofs[bg_index]) & (map_width_tiles * 8 - 1);
  y = (screen_y + line_regs->bgvofs[bg_index]) & (map_height_tiles * 8 - 1);
  tile_x = x / 8;
  tile_y = y / 8;
  screen_offset =
    screen_base + pfr_text_screen_entry_offset(map_width_tiles, tile_x, tile_y);

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
pfr_sample_affine_bg(const PfrLineRegs* line_regs,
                     int bg_index,
                     int screen_x,
                     int screen_y)
{
  const u16* palette = (const u16*)gPfrPltt;
  int slot = bg_index - 2;
  u16 bgcnt = line_regs->bgcnt[bg_index];
  int map_size = 128 << ((bgcnt >> 14) & 3);
  int map_tiles = map_size / 8;
  int char_base = ((bgcnt >> 2) & 3) * BG_CHAR_SIZE;
  int screen_base = ((bgcnt >> 8) & 31) * BG_SCREEN_SIZE;
  int wrap = (bgcnt >> 13) & 1;
  s32 pa = line_regs->bgpa[slot];
  s32 pb = line_regs->bgpb[slot];
  s32 pc = line_regs->bgpc[slot];
  s32 pd = line_regs->bgpd[slot];
  s32 ref_x = line_regs->bgx[slot];
  s32 ref_y = line_regs->bgy[slot];
  s32 tex_x;
  s32 tex_y;
  int src_x;
  int src_y;
  size_t map_offset;
  int tile_number;
  size_t tile_offset;
  uint8_t color_index;
  PfrPixelSample sample = {
    .opaque = false,
    .priority = (u8)(bgcnt & 3),
    .order = (u8)bg_index,
    .color = 0,
    .layer = 0,
  };

  pfr_apply_bg_mosaic(&screen_x, &screen_y, bgcnt, line_regs->mosaic);
  tex_x = ref_x + pa * screen_x + pb * screen_y;
  tex_y = ref_y + pc * screen_x + pd * screen_y;
  src_x = tex_x >> 8;
  src_y = tex_y >> 8;
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
  if (color_index == 0) {
    return sample;
  }

  sample.opaque = true;
  sample.color = palette[color_index];
  return sample;
}

static void
pfr_sample_backgrounds(int screen_x,
                       int screen_y,
                       const PfrLineRegs* line_regs,
                       u16 dispcnt,
                       u8 visible_layers,
                       PfrPixelSample* best,
                       PfrPixelSample* second)
{
  int mode = dispcnt & 7;
  int bg_index;

  memset(best, 0, sizeof(*best));
  memset(second, 0, sizeof(*second));

  for (bg_index = 0; bg_index < 4; bg_index++) {
    u8 layer = (u8)(PFR_LAYER_BG0 << bg_index);
    u16 bg_bit = (u16)(DISPCNT_BG0_ON << bg_index);
    PfrPixelSample sample;
    bool uses_text = false;
    bool uses_affine = false;

    if ((visible_layers & layer) == 0) {
      continue;
    }

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
      sample = pfr_sample_text_bg(line_regs, bg_index, screen_x, screen_y);
    } else {
      sample = pfr_sample_affine_bg(line_regs, bg_index, screen_x, screen_y);
    }

    if (!sample.opaque) {
      continue;
    }

    sample.layer = layer;
    pfr_consider_sample(best, second, sample);
  }
}

static bool
pfr_get_sprite_bounds(const struct OamData* entry,
                      int* out_origin_x,
                      int* out_origin_y,
                      int* out_width,
                      int* out_height,
                      int* out_display_width,
                      int* out_display_height)
{
  int shape = entry->shape;
  int size = entry->size;
  int width;
  int height;
  int display_width;
  int display_height;
  int origin_x;
  int origin_y;

  if (shape > ST_OAM_V_RECTANGLE || size > ST_OAM_SIZE_3 ||
      entry->affineMode == ST_OAM_AFFINE_ERASE) {
    return false;
  }

  width = sSpriteDimensions[shape][size][0];
  height = sSpriteDimensions[shape][size][1];
  display_width = width;
  display_height = height;

  if ((entry->affineMode & ST_OAM_AFFINE_DOUBLE_MASK) != 0) {
    display_width *= 2;
    display_height *= 2;
  }

  origin_x = (int)entry->x;
  origin_y = (int)entry->y;

  if (origin_x >= 256) {
    origin_x -= 512;
  }

  if (origin_y >= DISPLAY_HEIGHT) {
    origin_y -= 256;
  }

  *out_origin_x = origin_x;
  *out_origin_y = origin_y;
  *out_width = width;
  *out_height = height;
  *out_display_width = display_width;
  *out_display_height = display_height;
  return true;
}

static void
pfr_build_scanline_sprite_lists(void)
{
  const struct OamData* oam = (const struct OamData*)gPfrOam;
  int sprite_index;

  memset(sScanlineSpriteCounts, 0, sizeof(sScanlineSpriteCounts));
  for (sprite_index = 0; sprite_index < 128; sprite_index++) {
    const struct OamData* entry = &oam[sprite_index];
    int origin_x;
    int origin_y;
    int width;
    int height;
    int display_width;
    int display_height;
    int start_y;
    int end_y;
    int y;

    if (!pfr_get_sprite_bounds(entry,
                               &origin_x,
                               &origin_y,
                               &width,
                               &height,
                               &display_width,
                               &display_height)) {
      continue;
    }

    if (origin_x >= DISPLAY_WIDTH || origin_x + display_width <= 0 ||
        origin_y >= DISPLAY_HEIGHT || origin_y + display_height <= 0) {
      continue;
    }

    start_y = origin_y < 0 ? 0 : origin_y;
    end_y = origin_y + display_height > DISPLAY_HEIGHT
              ? DISPLAY_HEIGHT
              : origin_y + display_height;

    for (y = start_y; y < end_y; y++) {
      u8 count = sScanlineSpriteCounts[y];

      if (count < PFR_MAX_SPRITES_PER_SCANLINE) {
        sScanlineSpriteIndices[y][count] = (u8)sprite_index;
        sScanlineSpriteCounts[y] = count + 1;
      }
    }
  }
}

static bool
pfr_get_sprite_matrix(const struct OamData* oam,
                      u8 matrix_num,
                      s16* out_pa,
                      s16* out_pb,
                      s16* out_pc,
                      s16* out_pd)
{
  size_t base_index;

  if (matrix_num >= 32) {
    return false;
  }

  base_index = (size_t)matrix_num * 4U;
  *out_pa = (s16)oam[base_index + 0].affineParam;
  *out_pb = (s16)oam[base_index + 1].affineParam;
  *out_pc = (s16)oam[base_index + 2].affineParam;
  *out_pd = (s16)oam[base_index + 3].affineParam;
  return true;
}

static bool
pfr_sample_sprite_texel(const struct OamData* entry,
                        const struct OamData* oam,
                        const u16* palette,
                        bool one_d_mapping,
                        int screen_x,
                        int screen_y,
                        u16* out_color)
{
  int width;
  int height;
  int display_width;
  int display_height;
  int origin_x;
  int origin_y;
  int local_x;
  int local_y;
  int source_x;
  int source_y;
  int tile_num;
  int tile_stride;
  int tile_x;
  int tile_y;
  int pixel_x;
  int pixel_y;
  s16 pa;
  s16 pb;
  s16 pc;
  s16 pd;
  size_t tile_offset;
  uint8_t color_index;

  if (!pfr_get_sprite_bounds(entry,
                             &origin_x,
                             &origin_y,
                             &width,
                             &height,
                             &display_width,
                             &display_height)) {
    return false;
  }

  if (screen_x < origin_x || screen_y < origin_y ||
      screen_x >= origin_x + display_width ||
      screen_y >= origin_y + display_height) {
    return false;
  }

  local_x = screen_x - origin_x;
  local_y = screen_y - origin_y;

  if ((entry->affineMode & ST_OAM_AFFINE_ON_MASK) != 0) {
    if (!pfr_get_sprite_matrix(oam, (u8)entry->matrixNum, &pa, &pb, &pc, &pd)) {
      return false;
    }

    local_x -= display_width / 2;
    local_y -= display_height / 2;
    source_x = ((int)pa * local_x + (int)pb * local_y) >> 8;
    source_y = ((int)pc * local_x + (int)pd * local_y) >> 8;
    local_x = source_x + width / 2;
    local_y = source_y + height / 2;
  } else {
    if ((entry->matrixNum & ST_OAM_HFLIP) != 0) {
      local_x = width - 1 - local_x;
    }

    if ((entry->matrixNum & ST_OAM_VFLIP) != 0) {
      local_y = height - 1 - local_y;
    }
  }

  if (local_x < 0 || local_y < 0 || local_x >= width || local_y >= height) {
    return false;
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
      return false;
    }

    color_index = gPfrVram[tile_offset];
    if (color_index == 0) {
      return false;
    }

    if (out_color != NULL) {
      *out_color = palette[0x100 + color_index];
    }
  } else {
    tile_offset += (size_t)pixel_y * 4U + (size_t)pixel_x / 2U;

    if (tile_offset >= VRAM_SIZE) {
      return false;
    }

    color_index = gPfrVram[tile_offset];
    if ((pixel_x & 1) != 0) {
      color_index >>= 4;
    }

    color_index &= 0x0F;
    if (color_index == 0) {
      return false;
    }

    if (out_color != NULL) {
      *out_color = palette[0x100 + entry->paletteNum * 16 + color_index];
    }
  }

  return true;
}

static bool
pfr_pixel_in_obj_window(int screen_x, int screen_y, u16 dispcnt)
{
  const struct OamData* oam = (const struct OamData*)gPfrOam;
  bool one_d_mapping = (dispcnt & DISPCNT_OBJ_1D_MAP) != 0;
  u8 sprite_count;
  u8 sprite_order;

  if ((dispcnt & (DISPCNT_OBJ_ON | DISPCNT_OBJWIN_ON)) !=
      (DISPCNT_OBJ_ON | DISPCNT_OBJWIN_ON)) {
    return false;
  }

  sprite_count = sScanlineSpriteCounts[screen_y];
  for (sprite_order = 0; sprite_order < sprite_count; sprite_order++) {
    int sprite_index = sScanlineSpriteIndices[screen_y][sprite_order];
    const struct OamData* entry = &oam[sprite_index];

    if (entry->objMode != ST_OAM_OBJ_WINDOW) {
      continue;
    }

    if (pfr_sample_sprite_texel(entry,
                                oam,
                                (const u16*)gPfrPltt,
                                one_d_mapping,
                                screen_x,
                                screen_y,
                                NULL)) {
      return true;
    }
  }

  return false;
}

static PfrWindowState
pfr_window_state_for_pixel(int screen_x,
                           int screen_y,
                           const PfrLineRegs* line_regs)
{
  PfrWindowState state = { PFR_LAYER_BG0 | PFR_LAYER_BG1 | PFR_LAYER_BG2 |
                             PFR_LAYER_BG3 | PFR_LAYER_OBJ,
                           true };
  u16 control;
  u16 dispcnt = line_regs->dispcnt;

  if ((dispcnt & (DISPCNT_WIN0_ON | DISPCNT_WIN1_ON | DISPCNT_OBJWIN_ON)) ==
      0) {
    return state;
  }

  if ((dispcnt & DISPCNT_WIN0_ON) != 0 &&
      pfr_coord_in_window_range(screen_x, DISPLAY_WIDTH, line_regs->win0h) &&
      pfr_coord_in_window_range(screen_y, DISPLAY_HEIGHT, line_regs->win0v)) {
    control = line_regs->winin & 0x3FU;
  } else if ((dispcnt & DISPCNT_WIN1_ON) != 0 &&
             pfr_coord_in_window_range(
               screen_x, DISPLAY_WIDTH, line_regs->win1h) &&
             pfr_coord_in_window_range(
               screen_y, DISPLAY_HEIGHT, line_regs->win1v)) {
    control = (line_regs->winin >> 8) & 0x3FU;
  } else if ((dispcnt & DISPCNT_OBJWIN_ON) != 0 &&
             pfr_pixel_in_obj_window(screen_x, screen_y, dispcnt)) {
    control = (line_regs->winout >> 8) & 0x3FU;
  } else {
    control = line_regs->winout & 0x3FU;
  }

  state.visible_layers =
    (u8)(control & (PFR_LAYER_BG0 | PFR_LAYER_BG1 | PFR_LAYER_BG2 |
                    PFR_LAYER_BG3 | PFR_LAYER_OBJ));
  state.color_effect_enabled = (control & 0x20U) != 0;
  return state;
}

static u16
pfr_apply_color_effect(PfrPixelSample top,
                       PfrPixelSample second,
                       bool color_effect_enabled,
                       const PfrLineRegs* line_regs)
{
  u16 bldcnt = line_regs->bldcnt;
  u16 top_targets = bldcnt & 0x3FU;
  u16 second_targets = (bldcnt >> 8) & 0x3FU;
  int effect = bldcnt & (3 << 6);

  if (!color_effect_enabled || (top.layer & top_targets) == 0) {
    return top.color;
  }

  switch (effect) {
    case BLDCNT_EFFECT_BLEND:
      if (second.opaque && (second.layer & second_targets) != 0) {
        u16 bldalpha = line_regs->bldalpha;
        int eva = bldalpha & 0x1F;
        int evb = (bldalpha >> 8) & 0x1F;
        return pfr_blend_colors(top.color, second.color, eva, evb);
      }
      break;
    case BLDCNT_EFFECT_LIGHTEN:
      return pfr_lighten_color(top.color, line_regs->bldy & 0x1F);
    case BLDCNT_EFFECT_DARKEN:
      return pfr_darken_color(top.color, line_regs->bldy & 0x1F);
    default:
      break;
  }

  return top.color;
}

static PfrPixelSample
pfr_sample_sprite_pixel(int screen_x,
                        int screen_y,
                        u8 visible_layers,
                        u16 dispcnt)
{
  const struct OamData* oam = (const struct OamData*)gPfrOam;
  const u16* palette = (const u16*)gPfrPltt;
  bool one_d_mapping = (dispcnt & DISPCNT_OBJ_1D_MAP) != 0;
  u8 sprite_count;
  u8 sprite_order;
  PfrPixelSample best = {
    .opaque = false,
    .priority = 4,
    .order = 0xFF,
    .color = 0,
    .layer = 0,
  };

  if ((dispcnt & DISPCNT_OBJ_ON) == 0 ||
      (visible_layers & PFR_LAYER_OBJ) == 0) {
    return best;
  }

  sprite_count = sScanlineSpriteCounts[screen_y];
  for (sprite_order = 0; sprite_order < sprite_count; sprite_order++) {
    int sprite_index = sScanlineSpriteIndices[screen_y][sprite_order];
    const struct OamData* entry = &oam[sprite_index];
    PfrPixelSample sample;

    if (entry->objMode == ST_OAM_OBJ_WINDOW ||
        entry->affineMode == ST_OAM_AFFINE_ERASE) {
      continue;
    }

    if (!pfr_sample_sprite_texel(entry,
                                 oam,
                                 palette,
                                 one_d_mapping,
                                 screen_x,
                                 screen_y,
                                 &sample.color)) {
      continue;
    }

    sample.opaque = true;
    sample.priority = (u8)entry->priority;
    sample.order = (u8)sprite_index;
    sample.layer = PFR_LAYER_OBJ;

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
  memset(sLineRegs, 0, sizeof(sLineRegs));
}

void
pfr_renderer_shutdown(void)
{
}

void
pfr_renderer_begin_frame_capture(void)
{
  memset(sLineRegs, 0, sizeof(sLineRegs));
}

void
pfr_renderer_capture_scanline(int scanline)
{
  PfrLineRegs* line_regs;

  if (scanline < 0 || scanline >= DISPLAY_HEIGHT) {
    return;
  }

  line_regs = &sLineRegs[scanline];
  line_regs->dispcnt = REG_DISPCNT;
  line_regs->mosaic = REG_MOSAIC;
  line_regs->bgcnt[0] = REG_BG0CNT;
  line_regs->bgcnt[1] = REG_BG1CNT;
  line_regs->bgcnt[2] = REG_BG2CNT;
  line_regs->bgcnt[3] = REG_BG3CNT;
  line_regs->bghofs[0] = REG_BG0HOFS;
  line_regs->bghofs[1] = REG_BG1HOFS;
  line_regs->bghofs[2] = REG_BG2HOFS;
  line_regs->bghofs[3] = REG_BG3HOFS;
  line_regs->bgvofs[0] = REG_BG0VOFS;
  line_regs->bgvofs[1] = REG_BG1VOFS;
  line_regs->bgvofs[2] = REG_BG2VOFS;
  line_regs->bgvofs[3] = REG_BG3VOFS;
  line_regs->bgpa[0] = (s16)REG_BG2PA;
  line_regs->bgpb[0] = (s16)REG_BG2PB;
  line_regs->bgpc[0] = (s16)REG_BG2PC;
  line_regs->bgpd[0] = (s16)REG_BG2PD;
  line_regs->bgx[0] = (s32)REG_BG2X;
  line_regs->bgy[0] = (s32)REG_BG2Y;
  line_regs->bgpa[1] = (s16)REG_BG3PA;
  line_regs->bgpb[1] = (s16)REG_BG3PB;
  line_regs->bgpc[1] = (s16)REG_BG3PC;
  line_regs->bgpd[1] = (s16)REG_BG3PD;
  line_regs->bgx[1] = (s32)REG_BG3X;
  line_regs->bgy[1] = (s32)REG_BG3Y;
  line_regs->winin = REG_WININ;
  line_regs->winout = REG_WINOUT;
  line_regs->win0h = REG_WIN0H;
  line_regs->win0v = REG_WIN0V;
  line_regs->win1h = REG_WIN1H;
  line_regs->win1v = REG_WIN1V;
  line_regs->bldcnt = REG_BLDCNT;
  line_regs->bldalpha = REG_BLDALPHA;
  line_regs->bldy = REG_BLDY;
  line_regs->valid = true;
}

void
pfr_renderer_render_frame(void)
{
  int x;
  int y;

  pfr_build_scanline_sprite_lists();

  for (y = 0; y < DISPLAY_HEIGHT; y++) {
    PfrLineRegs fallback_line = { 0 };
    const PfrLineRegs* line_regs =
      sLineRegs[y].valid ? &sLineRegs[y] : &fallback_line;

    if (!sLineRegs[y].valid) {
      fallback_line.dispcnt = REG_DISPCNT;
      fallback_line.mosaic = REG_MOSAIC;
      fallback_line.bgcnt[0] = REG_BG0CNT;
      fallback_line.bgcnt[1] = REG_BG1CNT;
      fallback_line.bgcnt[2] = REG_BG2CNT;
      fallback_line.bgcnt[3] = REG_BG3CNT;
      fallback_line.bghofs[0] = REG_BG0HOFS;
      fallback_line.bghofs[1] = REG_BG1HOFS;
      fallback_line.bghofs[2] = REG_BG2HOFS;
      fallback_line.bghofs[3] = REG_BG3HOFS;
      fallback_line.bgvofs[0] = REG_BG0VOFS;
      fallback_line.bgvofs[1] = REG_BG1VOFS;
      fallback_line.bgvofs[2] = REG_BG2VOFS;
      fallback_line.bgvofs[3] = REG_BG3VOFS;
      fallback_line.bgpa[0] = (s16)REG_BG2PA;
      fallback_line.bgpb[0] = (s16)REG_BG2PB;
      fallback_line.bgpc[0] = (s16)REG_BG2PC;
      fallback_line.bgpd[0] = (s16)REG_BG2PD;
      fallback_line.bgx[0] = (s32)REG_BG2X;
      fallback_line.bgy[0] = (s32)REG_BG2Y;
      fallback_line.bgpa[1] = (s16)REG_BG3PA;
      fallback_line.bgpb[1] = (s16)REG_BG3PB;
      fallback_line.bgpc[1] = (s16)REG_BG3PC;
      fallback_line.bgpd[1] = (s16)REG_BG3PD;
      fallback_line.bgx[1] = (s32)REG_BG3X;
      fallback_line.bgy[1] = (s32)REG_BG3Y;
      fallback_line.winin = REG_WININ;
      fallback_line.winout = REG_WINOUT;
      fallback_line.win0h = REG_WIN0H;
      fallback_line.win0v = REG_WIN0V;
      fallback_line.win1h = REG_WIN1H;
      fallback_line.win1v = REG_WIN1V;
      fallback_line.bldcnt = REG_BLDCNT;
      fallback_line.bldalpha = REG_BLDALPHA;
      fallback_line.bldy = REG_BLDY;
      fallback_line.valid = true;
    }

    if ((line_regs->dispcnt & DISPCNT_FORCED_BLANK) != 0) {
      for (x = 0; x < DISPLAY_WIDTH; x++) {
        sFramebuffer[y * DISPLAY_WIDTH + x] = 0xFFFFFFFFU;
      }

      continue;
    }

    for (x = 0; x < DISPLAY_WIDTH; x++) {
      PfrWindowState window_state = pfr_window_state_for_pixel(x, y, line_regs);
      PfrPixelSample bg_top;
      PfrPixelSample bg_second;
      PfrPixelSample obj_sample = pfr_sample_sprite_pixel(
        x, y, window_state.visible_layers, line_regs->dispcnt);
      PfrPixelSample top = { 0 };
      PfrPixelSample second = { 0 };
      PfrPixelSample backdrop = {
        true, 4, 0xFF, ((const u16*)gPfrPltt)[0], PFR_LAYER_BD
      };
      u16 final_color;

      pfr_sample_backgrounds(x,
                             y,
                             line_regs,
                             line_regs->dispcnt,
                             window_state.visible_layers,
                             &bg_top,
                             &bg_second);
      pfr_consider_sample(&top, &second, bg_top);
      pfr_consider_sample(&top, &second, bg_second);
      pfr_consider_sample(&top, &second, obj_sample);
      pfr_consider_sample(&top, &second, backdrop);

      final_color = pfr_apply_color_effect(
        top, second, window_state.color_effect_enabled, line_regs);

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
