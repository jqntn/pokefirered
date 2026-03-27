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

enum
{
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

static void
pfr_sample_backgrounds(int screen_x,
                       int screen_y,
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
      sample = pfr_sample_text_bg(bg_index, screen_x, screen_y);
    } else {
      sample = pfr_sample_affine_bg(bg_index, screen_x, screen_y);
    }

    if (!sample.opaque) {
      continue;
    }

    sample.layer = layer;
    pfr_consider_sample(best, second, sample);
  }
}

static bool
pfr_sample_sprite_texel(const struct OamData* entry,
                        const u16* palette,
                        bool one_d_mapping,
                        int screen_x,
                        int screen_y,
                        u16* out_color)
{
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

  if (shape > ST_OAM_V_RECTANGLE || size > ST_OAM_SIZE_3 ||
      entry->affineMode == ST_OAM_AFFINE_ERASE) {
    return false;
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
    return false;
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
  int sprite_index;

  if ((dispcnt & (DISPCNT_OBJ_ON | DISPCNT_OBJWIN_ON)) !=
      (DISPCNT_OBJ_ON | DISPCNT_OBJWIN_ON)) {
    return false;
  }

  for (sprite_index = 0; sprite_index < 128; sprite_index++) {
    const struct OamData* entry = &oam[sprite_index];

    if (entry->objMode != ST_OAM_OBJ_WINDOW) {
      continue;
    }

    if (pfr_sample_sprite_texel(entry,
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
  PfrPixelSample best = { false, 4, 0xFF, 0 };
  int sprite_index;

  if ((dispcnt & DISPCNT_OBJ_ON) == 0 ||
      (visible_layers & PFR_LAYER_OBJ) == 0) {
    return best;
  }

  for (sprite_index = 0; sprite_index < 128; sprite_index++) {
    const struct OamData* entry = &oam[sprite_index];
    PfrPixelSample sample;

    if (entry->objMode == ST_OAM_OBJ_WINDOW ||
        entry->affineMode == ST_OAM_AFFINE_ERASE) {
      continue;
    }

    if (!pfr_sample_sprite_texel(
          entry, palette, one_d_mapping, screen_x, screen_y, &sample.color)) {
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

  if ((REG_DISPCNT & DISPCNT_FORCED_BLANK) != 0) {
    memset(sFramebuffer, 0xFF, sizeof(sFramebuffer));
    return;
  }

  for (y = 0; y < DISPLAY_HEIGHT; y++) {
    PfrLineRegs fallback_line = { REG_DISPCNT, REG_WININ,  REG_WINOUT,
                                  REG_WIN0H,   REG_WIN0V,  REG_WIN1H,
                                  REG_WIN1V,   REG_BLDCNT, REG_BLDALPHA,
                                  REG_BLDY,    true };
    const PfrLineRegs* line_regs =
      sLineRegs[y].valid ? &sLineRegs[y] : &fallback_line;

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
