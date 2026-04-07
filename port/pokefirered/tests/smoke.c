#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "constants/songs.h"
#include "dma3.h"
#include "gba/io_reg.h"
#include "gba/syscall.h"
#include "global.h"
#include "gpu_regs.h"
#include "main.h"
#include "pfr/audio.h"
#include "pfr/core.h"
#include "pfr/main_runtime.h"
#include "pfr/renderer.h"
#include "pfr/storage.h"
#include "pfr/stubs.h"
#include "raylib.h"
#include "task.h"

static int sTaskLog[8];
static size_t sTaskLogCount;
static int sFollowupState;
static int sMainLog[4];
static size_t sMainLogCount;
static int sVBlankCount;

typedef struct PfrAudioWindow
{
  const char* name;
  int remaining_frames;
  bool active;
  bool done;
  uint32_t absmax;
  uint64_t sumsq;
  size_t sample_count;
} PfrAudioWindow;

static void
test_audio_window_begin(PfrAudioWindow* window,
                        const char* name,
                        int frame_count)
{
  window->name = name;
  window->remaining_frames = frame_count;
  window->active = true;
  window->done = false;
  window->absmax = 0;
  window->sumsq = 0;
  window->sample_count = 0;
}

static void
test_audio_window_feed(PfrAudioWindow* window,
                       const int16_t* samples,
                       size_t sample_count)
{
  size_t i;

  if (!window->active) {
    return;
  }

  for (i = 0; i < sample_count; i++) {
    int32_t sample = samples[i];
    uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);

    if (magnitude > window->absmax) {
      window->absmax = magnitude;
    }
    window->sumsq += (uint64_t)(sample * sample);
    window->sample_count++;
  }

  if (--window->remaining_frames == 0) {
    window->active = false;
    window->done = true;
  }
}

static double
test_audio_window_rms(const PfrAudioWindow* window)
{
  if (window->sample_count == 0) {
    return 0.0;
  }

  return sqrt((double)window->sumsq / (double)window->sample_count);
}

static void
test_audio_resampler(void)
{
  static const int16_t source_frames[] = {
    1000, 1000, 3000, 3000, 5000, 5000,
  };
  int16_t output[4 * PFR_AUDIO_CHANNEL_COUNT] = { 0 };

  pfr_audio_reset();
  pfr_audio_queue_source_frames(source_frames, 3);
  assert(pfr_audio_drain_resampled_frames(
           output, 4, PFR_DEFAULT_AUDIO_SAMPLE_RATE * 2) == 4);

  assert(output[0] == 1000);
  assert(output[1] == 1000);
  assert(output[2] == 2000);
  assert(output[3] == 2000);
  assert(output[4] == 3000);
  assert(output[5] == 3000);
  assert(output[6] == 4000);
  assert(output[7] == 4000);
  assert(pfr_audio_available_frames() == 1);
  pfr_audio_reset();
}

static void
test_dma3_trace(const char* step)
{
  printf("pfr_smoke: test_dma3 %s\n", step);
  fflush(stdout);
}

static void
pfr_hide_all_test_sprites(struct OamData* oam)
{
  int i;

  for (i = 0; i < 128; i++) {
    oam[i].affineMode = ST_OAM_AFFINE_ERASE;
  }
}

static uint32_t
test_rgb555_to_rgba8888(u16 color)
{
  uint32_t r = (uint32_t)(color & 31U) * 255U / 31U;
  uint32_t g = (uint32_t)((color >> 5) & 31U) * 255U / 31U;
  uint32_t b = (uint32_t)((color >> 10) & 31U) * 255U / 31U;

  return 0xFF000000U | (b << 16) | (g << 8) | r;
}

static u16
test_blend_color(u16 lhs, u16 rhs, int eva, int evb)
{
  int lhs_r = lhs & 31;
  int lhs_g = (lhs >> 5) & 31;
  int lhs_b = (lhs >> 10) & 31;
  int rhs_r = rhs & 31;
  int rhs_g = (rhs >> 5) & 31;
  int rhs_b = (rhs >> 10) & 31;
  int out_r = (lhs_r * eva + rhs_r * evb) / 16;
  int out_g = (lhs_g * eva + rhs_g * evb) / 16;
  int out_b = (lhs_b * eva + rhs_b * evb) / 16;

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

static u16
test_lighten_color(u16 color, int coeff)
{
  int r = color & 31;
  int g = (color >> 5) & 31;
  int b = (color >> 10) & 31;

  r += ((31 - r) * coeff) / 16;
  g += ((31 - g) * coeff) / 16;
  b += ((31 - b) * coeff) / 16;
  return (u16)(r | (g << 5) | (b << 10));
}

static void
test_oam_layout(void)
{
  assert(sizeof(struct OamData) == 8);
  assert(offsetof(struct OamData, affineParam) == 6);
}

static void
test_task_priority_high(u8 taskId)
{
  sTaskLog[sTaskLogCount++] = 1;
  DestroyTask(taskId);
}

static void
test_task_priority_mid(u8 taskId)
{
  sTaskLog[sTaskLogCount++] = 2;
  DestroyTask(taskId);
}

static void
test_task_priority_low(u8 taskId)
{
  sTaskLog[sTaskLogCount++] = 3;
  DestroyTask(taskId);
}

static void
test_task_followup(u8 taskId)
{
  sFollowupState = 2;
  DestroyTask(taskId);
}

static void
test_task_switcher(u8 taskId)
{
  sFollowupState = 1;
  SwitchTaskToFollowupFunc(taskId);
}

static void
test_main_callback1(void)
{
  sMainLog[sMainLogCount++] = 1;
}

static void
test_main_callback2(void)
{
  sMainLog[sMainLogCount++] = 2;
}

static void
test_main_vblank(void)
{
  sVBlankCount++;
}

static void
test_dma3(void)
{
  const u16 src16[] = { 1, 2, 3, 4, 5, 6 };
  u16 copy16[PFR_ARRAY_COUNT(src16)] = { 0 };
  u16 fill16[6] = { 0 };
  u32 fill32[4] = { 0 };
  s16 request;

  test_dma3_trace("clear");
  ClearDma3Requests();
  test_dma3_trace("after clear");
  printf("pfr_smoke: test_dma3 gPfrIo=%p\n", (void*)gPfrIo);
  fflush(stdout);
  test_dma3_trace("set vcount");
  REG_VCOUNT = 225;
  test_dma3_trace("after vcount");

  test_dma3_trace("queue copy16");
  request = RequestDma3Copy(src16, copy16, sizeof(src16), DMA3_16BIT);
  assert(request >= 0);
  test_dma3_trace("check pending copy16");
  assert(WaitDma3Request(request) == -1);
  test_dma3_trace("process copy16 outside vblank");
  ProcessDma3Requests();
  test_dma3_trace("confirm copy16 still pending");
  assert(WaitDma3Request(request) == -1);
  assert(copy16[0] == 0);

  REG_VCOUNT = 160;
  test_dma3_trace("process copy16 in vblank");
  ProcessDma3Requests();
  assert(memcmp(src16, copy16, sizeof(src16)) == 0);
  assert(WaitDma3Request(request) == 0);

  test_dma3_trace("queue fill16");
  request = RequestDma3Fill(0x1357, fill16, sizeof(fill16), DMA3_16BIT);
  assert(request >= 0);
  test_dma3_trace("process fill16");
  ProcessDma3Requests();
  assert(fill16[0] == 0x1357);
  assert(fill16[5] == 0x1357);
  assert(WaitDma3Request(request) == 0);

  test_dma3_trace("queue fill32");
  request = RequestDma3Fill(0x11223344, fill32, sizeof(fill32), DMA3_32BIT);
  assert(request >= 0);
  test_dma3_trace("process fill32");
  ProcessDma3Requests();
  assert(fill32[0] == 0x11223344U);
  assert(fill32[3] == 0x11223344U);
  assert(WaitDma3Request(-1) == 0);

  memset(copy16, 0, sizeof(copy16));
  test_dma3_trace("direct copy16");
  Dma3CopyLarge16_(src16, copy16, sizeof(src16));
  assert(memcmp(src16, copy16, sizeof(src16)) == 0);
  test_dma3_trace("done");
}

static void
test_gpu_regs(void)
{
  memset(gPfrIo, 0, PFR_IO_SIZE);
  InitGpuRegManager();

  REG_VCOUNT = 0;
  REG_DISPCNT = 0;
  SetGpuReg(REG_OFFSET_BG0HOFS, 123);
  assert(GetGpuReg(REG_OFFSET_BG0HOFS) == 123);
  assert(REG_BG0HOFS == 0);
  CopyBufferedValuesToGpuRegs();
  assert(REG_BG0HOFS == 123);

  SetGpuRegBits(REG_OFFSET_DISPCNT, DISPCNT_BG0_ON | DISPCNT_OBJ_ON);
  CopyBufferedValuesToGpuRegs();
  assert((GetGpuReg(REG_OFFSET_DISPCNT) & (DISPCNT_BG0_ON | DISPCNT_OBJ_ON)) ==
         (DISPCNT_BG0_ON | DISPCNT_OBJ_ON));
  assert((REG_DISPCNT & (DISPCNT_BG0_ON | DISPCNT_OBJ_ON)) ==
         (DISPCNT_BG0_ON | DISPCNT_OBJ_ON));

  ClearGpuRegBits(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON);
  CopyBufferedValuesToGpuRegs();
  assert((GetGpuReg(REG_OFFSET_DISPCNT) & DISPCNT_OBJ_ON) == 0);

  REG_VCOUNT = 161;
  SetGpuReg(REG_OFFSET_BG0VOFS, 77);
  assert(REG_BG0VOFS == 77);

  REG_VCOUNT = 0;
  REG_DISPCNT = 0;
  SetGpuReg_ForcedBlank(REG_OFFSET_BG1VOFS, 55);
  assert(REG_BG1VOFS == 55);

  EnableInterrupts(INTR_FLAG_VBLANK | INTR_FLAG_HBLANK);
  assert(REG_IE == (INTR_FLAG_VBLANK | INTR_FLAG_HBLANK));
  CopyBufferedValuesToGpuRegs();
  assert((REG_DISPSTAT & (DISPSTAT_VBLANK_INTR | DISPSTAT_HBLANK_INTR)) ==
         (DISPSTAT_VBLANK_INTR | DISPSTAT_HBLANK_INTR));

  DisableInterrupts(INTR_FLAG_HBLANK);
  assert(REG_IE == INTR_FLAG_VBLANK);
  CopyBufferedValuesToGpuRegs();
  assert((REG_DISPSTAT & (DISPSTAT_VBLANK_INTR | DISPSTAT_HBLANK_INTR)) ==
         DISPSTAT_VBLANK_INTR);
}

static void
test_renderer_respects_obj_disable(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  uint32_t expectedBackdrop;
  uint32_t expectedSprite;
  const uint32_t* framebuffer;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0400;
  ((u16*)gPfrPltt)[0x100 + 1] = 0x001F;
  gPfrVram[0x10000] = 0x11;

  oam[0].affineMode = ST_OAM_AFFINE_OFF;
  oam[0].objMode = ST_OAM_OBJ_NORMAL;
  oam[0].mosaic = FALSE;
  oam[0].bpp = ST_OAM_4BPP;
  oam[0].shape = ST_OAM_SQUARE;
  oam[0].x = 0;
  oam[0].y = 0;
  oam[0].size = ST_OAM_SIZE_0;
  oam[0].tileNum = 0;
  oam[0].priority = 0;
  oam[0].paletteNum = 0;

  pfr_renderer_init();

  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP;
  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expectedBackdrop = 0xFF080000U;
  expectedSprite = 0xFF0000FFU;
  assert(framebuffer[0] == expectedSprite);

  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_1D_MAP;
  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  assert(framebuffer[0] == expectedBackdrop);
}

static void
test_renderer_applies_obj_mosaic(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_block0;
  uint32_t expected_block1;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0400;
  ((u16*)gPfrPltt)[0x100 + 1] = 0x001F;
  ((u16*)gPfrPltt)[0x100 + 2] = 0x03E0;
  ((u16*)gPfrPltt)[0x100 + 3] = 0x7C00;
  ((u16*)gPfrPltt)[0x100 + 4] = 0x03FF;
  gPfrVram[0x10000 + 0] = 0x21;
  gPfrVram[0x10000 + 1] = 0x43;
  gPfrVram[0x10000 + 4] = 0x43;
  gPfrVram[0x10000 + 5] = 0x21;

  oam[0].affineMode = ST_OAM_AFFINE_OFF;
  oam[0].objMode = ST_OAM_OBJ_NORMAL;
  oam[0].mosaic = TRUE;
  oam[0].bpp = ST_OAM_4BPP;
  oam[0].shape = ST_OAM_SQUARE;
  oam[0].x = 0;
  oam[0].y = 0;
  oam[0].size = ST_OAM_SIZE_0;
  oam[0].tileNum = 0;
  oam[0].priority = 0;
  oam[0].paletteNum = 0;

  pfr_renderer_init();

  REG_MOSAIC = 0x1100;
  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP;
  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_block0 = test_rgb555_to_rgba8888(0x001F);
  expected_block1 = test_rgb555_to_rgba8888(0x7C00);
  assert(framebuffer[0] == expected_block0);
  assert(framebuffer[1] == expected_block0);
  assert(framebuffer[DISPLAY_WIDTH] == expected_block0);
  assert(framebuffer[DISPLAY_WIDTH + 1] == expected_block0);
  assert(framebuffer[2] == expected_block1);
  assert(framebuffer[3] == expected_block1);
  assert(framebuffer[DISPLAY_WIDTH + 2] == expected_block1);
  assert(framebuffer[DISPLAY_WIDTH + 3] == expected_block1);
}

static void
test_renderer_anchors_obj_mosaic_to_sprite_origin(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  int row = 5;
  int col = 10;
  uint32_t expected_block0;
  uint32_t expected_block1;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0400;
  ((u16*)gPfrPltt)[0x100 + 1] = 0x001F;
  ((u16*)gPfrPltt)[0x100 + 2] = 0x03E0;
  ((u16*)gPfrPltt)[0x100 + 3] = 0x7C00;
  ((u16*)gPfrPltt)[0x100 + 4] = 0x03FF;
  ((u16*)gPfrPltt)[0x100 + 5] = 0x7FE0;
  ((u16*)gPfrPltt)[0x100 + 6] = 0x4210;
  ((u16*)gPfrPltt)[0x100 + 7] = 0x56B5;
  ((u16*)gPfrPltt)[0x100 + 8] = 0x7FFF;
  gPfrVram[0x10000 + 0] = 0x21;
  gPfrVram[0x10000 + 1] = 0x43;
  gPfrVram[0x10000 + 2] = 0x65;
  gPfrVram[0x10000 + 3] = 0x87;

  oam[0].affineMode = ST_OAM_AFFINE_OFF;
  oam[0].objMode = ST_OAM_OBJ_NORMAL;
  oam[0].mosaic = TRUE;
  oam[0].bpp = ST_OAM_4BPP;
  oam[0].shape = ST_OAM_SQUARE;
  oam[0].x = col;
  oam[0].y = row;
  oam[0].size = ST_OAM_SIZE_0;
  oam[0].tileNum = 0;
  oam[0].priority = 0;
  oam[0].paletteNum = 0;

  pfr_renderer_init();

  REG_MOSAIC = 0x0300;
  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP;
  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_block0 = test_rgb555_to_rgba8888(0x001F);
  expected_block1 = test_rgb555_to_rgba8888(0x7FE0);
  assert(framebuffer[row * DISPLAY_WIDTH + col + 0] == expected_block0);
  assert(framebuffer[row * DISPLAY_WIDTH + col + 1] == expected_block0);
  assert(framebuffer[row * DISPLAY_WIDTH + col + 2] == expected_block0);
  assert(framebuffer[row * DISPLAY_WIDTH + col + 3] == expected_block0);
  assert(framebuffer[row * DISPLAY_WIDTH + col + 4] == expected_block1);
  assert(framebuffer[row * DISPLAY_WIDTH + col + 5] == expected_block1);
  assert(framebuffer[row * DISPLAY_WIDTH + col + 6] == expected_block1);
  assert(framebuffer[row * DISPLAY_WIDTH + col + 7] == expected_block1);
}

static void
test_renderer_blends_semi_transparent_obj_even_when_bldcnt_is_not_blend(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_blend;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0400;
  ((u16*)gPfrPltt)[1] = 0x001F;
  ((u16*)gPfrPltt)[0x100 + 1] = 0x7C00;
  gPfrVram[0] = 0x11;
  gPfrVram[4] = 0x11;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31) = 0;
  gPfrVram[0x10000] = 0x11;

  oam[0].affineMode = ST_OAM_AFFINE_OFF;
  oam[0].objMode = ST_OAM_OBJ_BLEND;
  oam[0].mosaic = FALSE;
  oam[0].bpp = ST_OAM_4BPP;
  oam[0].shape = ST_OAM_SQUARE;
  oam[0].x = 0;
  oam[0].y = 0;
  oam[0].size = ST_OAM_SIZE_0;
  oam[0].tileNum = 0;
  oam[0].priority = 0;
  oam[0].paletteNum = 0;

  pfr_renderer_init();

  REG_BG0CNT = BGCNT_SCREENBASE(31) | BGCNT_16COLOR | BGCNT_PRIORITY(1);
  REG_BLDALPHA = 8 | (8 << 8);
  REG_BLDCNT = BLDCNT_TGT2_BG0 | BLDCNT_EFFECT_LIGHTEN;
  REG_DISPCNT =
    DISPCNT_MODE_0 | DISPCNT_BG0_ON | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP;

  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_blend =
    test_rgb555_to_rgba8888(test_blend_color(0x7C00, 0x001F, 8, 8));
  assert(framebuffer[0] == expected_blend);
}

static void
test_renderer_uses_fixed_2d_8bpp_obj_row_stride(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_top_left;
  uint32_t expected_top_right;
  uint32_t expected_bottom_left;
  uint32_t expected_bottom_right;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0400;
  ((u16*)gPfrPltt)[0x100 + 1] = 0x001F;
  ((u16*)gPfrPltt)[0x100 + 2] = 0x03E0;
  ((u16*)gPfrPltt)[0x100 + 3] = 0x7C00;
  ((u16*)gPfrPltt)[0x100 + 4] = 0x03FF;
  memset(gPfrVram + 0x10000, 1, 64);
  memset(gPfrVram + 0x10000 + 64, 2, 64);
  memset(gPfrVram + 0x10000 + 32 * 32, 3, 64);
  memset(gPfrVram + 0x10000 + 34 * 32, 4, 64);

  oam[0].affineMode = ST_OAM_AFFINE_OFF;
  oam[0].objMode = ST_OAM_OBJ_NORMAL;
  oam[0].mosaic = FALSE;
  oam[0].bpp = ST_OAM_8BPP;
  oam[0].shape = ST_OAM_SQUARE;
  oam[0].x = 0;
  oam[0].y = 0;
  oam[0].size = ST_OAM_SIZE_1;
  oam[0].tileNum = 0;
  oam[0].priority = 0;
  oam[0].paletteNum = 0;

  pfr_renderer_init();

  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_ON;
  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_top_left = test_rgb555_to_rgba8888(0x001F);
  expected_top_right = test_rgb555_to_rgba8888(0x03E0);
  expected_bottom_left = test_rgb555_to_rgba8888(0x7C00);
  expected_bottom_right = test_rgb555_to_rgba8888(0x03FF);
  assert(framebuffer[0] == expected_top_left);
  assert(framebuffer[8] == expected_top_right);
  assert(framebuffer[8 * DISPLAY_WIDTH] == expected_bottom_left);
  assert(framebuffer[8 * DISPLAY_WIDTH + 8] == expected_bottom_right);
}

static void
test_renderer_skips_affine_erase_sprites(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  uint32_t expectedBackdrop;
  const uint32_t* framebuffer;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0400;
  ((u16*)gPfrPltt)[0x100 + 1] = 0x001F;
  gPfrVram[0x10000] = 0x11;

  oam[0].affineMode = ST_OAM_AFFINE_ERASE;
  oam[0].objMode = ST_OAM_OBJ_NORMAL;
  oam[0].mosaic = FALSE;
  oam[0].bpp = ST_OAM_4BPP;
  oam[0].shape = ST_OAM_SQUARE;
  oam[0].x = 0;
  oam[0].y = 0;
  oam[0].size = ST_OAM_SIZE_0;
  oam[0].tileNum = 0;
  oam[0].priority = 0;
  oam[0].paletteNum = 0;

  pfr_renderer_init();

  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP;
  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expectedBackdrop = 0xFF080000U;
  assert(framebuffer[0] == expectedBackdrop);
}

static void
test_renderer_samples_affine_double_sprites(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_backdrop;
  uint32_t expected_left;
  uint32_t expected_right;
  int row;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0400;
  ((u16*)gPfrPltt)[0x100 + 1] = 0x001F;
  ((u16*)gPfrPltt)[0x100 + 2] = 0x03E0;
  for (row = 0; row < 8; row++) {
    gPfrVram[0x10000 + row * 4 + 0] = 0x11;
    gPfrVram[0x10000 + row * 4 + 1] = 0x11;
    gPfrVram[0x10000 + row * 4 + 2] = 0x22;
    gPfrVram[0x10000 + row * 4 + 3] = 0x22;
  }

  oam[0].affineMode = ST_OAM_AFFINE_DOUBLE;
  oam[0].objMode = ST_OAM_OBJ_NORMAL;
  oam[0].mosaic = FALSE;
  oam[0].bpp = ST_OAM_4BPP;
  oam[0].shape = ST_OAM_SQUARE;
  oam[0].x = 0;
  oam[0].y = 0;
  oam[0].matrixNum = 0;
  oam[0].size = ST_OAM_SIZE_0;
  oam[0].tileNum = 0;
  oam[0].priority = 0;
  oam[0].paletteNum = 0;
  oam[0].affineParam = 0x0080;
  oam[1].affineParam = 0x0000;
  oam[2].affineParam = 0x0000;
  oam[3].affineParam = 0x0080;

  pfr_renderer_init();

  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP;
  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_backdrop = test_rgb555_to_rgba8888(0x0400);
  expected_left = test_rgb555_to_rgba8888(0x001F);
  expected_right = test_rgb555_to_rgba8888(0x03E0);
  assert(framebuffer[8 * DISPLAY_WIDTH + 2] == expected_left);
  assert(framebuffer[8 * DISPLAY_WIDTH + 13] == expected_right);
  assert(framebuffer[8 * DISPLAY_WIDTH + 16] == expected_backdrop);
}

static void
test_renderer_respects_forced_blank(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0001;
  ((u16*)gPfrPltt)[0x100 + 1] = 0x001F;
  gPfrVram[0x10000] = 0x11;

  oam[0].affineMode = ST_OAM_AFFINE_OFF;
  oam[0].objMode = ST_OAM_OBJ_NORMAL;
  oam[0].mosaic = FALSE;
  oam[0].bpp = ST_OAM_4BPP;
  oam[0].shape = ST_OAM_SQUARE;
  oam[0].x = 0;
  oam[0].y = 0;
  oam[0].size = ST_OAM_SIZE_0;
  oam[0].tileNum = 0;
  oam[0].priority = 0;
  oam[0].paletteNum = 0;

  pfr_renderer_init();

  REG_DISPCNT =
    DISPCNT_MODE_0 | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP | DISPCNT_FORCED_BLANK;
  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  assert(framebuffer[0] == 0xFFFFFFFFU);
}

static void
test_renderer_limits_scanline_sprites_to_32_objs(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_visible;
  int i;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0400;
  ((u16*)gPfrPltt)[0x100 + 1] = 0x001F;
  ((u16*)gPfrPltt)[0x100 + 2] = 0x03E0;
  gPfrVram[0x10000] = 0x11;
  gPfrVram[0x10000 + 32] = 0x22;

  for (i = 0; i < 32; i++) {
    oam[i].affineMode = ST_OAM_AFFINE_OFF;
    oam[i].objMode = ST_OAM_OBJ_NORMAL;
    oam[i].mosaic = FALSE;
    oam[i].bpp = ST_OAM_4BPP;
    oam[i].shape = ST_OAM_SQUARE;
    oam[i].x = 0;
    oam[i].y = 0;
    oam[i].size = ST_OAM_SIZE_0;
    oam[i].tileNum = 0;
    oam[i].priority = 3;
    oam[i].paletteNum = 0;
  }

  oam[32].affineMode = ST_OAM_AFFINE_OFF;
  oam[32].objMode = ST_OAM_OBJ_NORMAL;
  oam[32].mosaic = FALSE;
  oam[32].bpp = ST_OAM_4BPP;
  oam[32].shape = ST_OAM_SQUARE;
  oam[32].x = 0;
  oam[32].y = 0;
  oam[32].size = ST_OAM_SIZE_0;
  oam[32].tileNum = 1;
  oam[32].priority = 0;
  oam[32].paletteNum = 0;

  pfr_renderer_init();

  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP;
  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_visible = test_rgb555_to_rgba8888(0x001F);
  assert(framebuffer[0] == expected_visible);
}

static void
test_renderer_obj_window_lightens_bg0(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_base;
  uint32_t expected_lightened;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[1] = 0x001F;
  gPfrVram[0] = 0x11;
  gPfrVram[4] = 0x11;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31) = 0;
  gPfrVram[0x10000] = 0x11;

  oam[0].affineMode = ST_OAM_AFFINE_OFF;
  oam[0].objMode = ST_OAM_OBJ_WINDOW;
  oam[0].mosaic = FALSE;
  oam[0].bpp = ST_OAM_4BPP;
  oam[0].shape = ST_OAM_SQUARE;
  oam[0].x = 0;
  oam[0].y = 0;
  oam[0].size = ST_OAM_SIZE_0;
  oam[0].tileNum = 0;
  oam[0].priority = 0;
  oam[0].paletteNum = 0;

  pfr_renderer_init();

  REG_BG0CNT = BGCNT_SCREENBASE(31) | BGCNT_16COLOR | BGCNT_PRIORITY(0);
  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON | DISPCNT_OBJ_ON |
                DISPCNT_OBJ_1D_MAP | DISPCNT_OBJWIN_ON;
  REG_WINOUT = WINOUT_WIN01_BG0 | WINOUT_WINOBJ_BG0 | WINOUT_WINOBJ_CLR;
  REG_BLDCNT = BLDCNT_TGT1_BG0 | BLDCNT_EFFECT_LIGHTEN;
  REG_BLDY = 8;

  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_base = test_rgb555_to_rgba8888(0x001F);
  expected_lightened = test_rgb555_to_rgba8888(test_lighten_color(0x001F, 8));
  assert(framebuffer[0] == expected_lightened);
  assert(framebuffer[8] == expected_base);
}

static void
test_renderer_uses_captured_scanline_bldy(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_base;
  uint32_t expected_lightened;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[1] = 0x001F;
  gPfrVram[0] = 0x11;
  gPfrVram[4] = 0x11;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31) = 0;

  pfr_renderer_init();

  REG_BG0CNT = BGCNT_SCREENBASE(31) | BGCNT_16COLOR | BGCNT_PRIORITY(0);
  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
  REG_BLDCNT = BLDCNT_TGT1_BG0 | BLDCNT_EFFECT_LIGHTEN;

  pfr_renderer_begin_frame_capture();
  REG_BLDY = 0;
  pfr_renderer_capture_scanline(0);
  REG_BLDY = 8;
  pfr_renderer_capture_scanline(1);

  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_base = test_rgb555_to_rgba8888(0x001F);
  expected_lightened = test_rgb555_to_rgba8888(test_lighten_color(0x001F, 8));
  assert(framebuffer[0] == expected_base);
  assert(framebuffer[DISPLAY_WIDTH] == expected_lightened);
}

static void
test_renderer_affine_bg_color_zero_is_transparent(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_bg0;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0400;
  ((u16*)gPfrPltt)[1] = 0x001F;
  gPfrVram[0] = 0x11;
  gPfrVram[4] = 0x11;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31) = 0;

  pfr_renderer_init();

  REG_BG0CNT = BGCNT_SCREENBASE(31) | BGCNT_16COLOR | BGCNT_PRIORITY(1);
  REG_BG2CNT = BGCNT_CHARBASE(1) | BGCNT_SCREENBASE(30) | BGCNT_256COLOR |
               BGCNT_AFF128x128 | BGCNT_PRIORITY(0);
  REG_BG2PA = 0x0100;
  REG_BG2PB = 0;
  REG_BG2PC = 0;
  REG_BG2PD = 0x0100;
  REG_BG2X = 0;
  REG_BG2Y = 0;
  REG_DISPCNT = DISPCNT_MODE_1 | DISPCNT_BG0_ON | DISPCNT_BG2_ON;

  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_bg0 = test_rgb555_to_rgba8888(0x001F);
  assert(framebuffer[0] == expected_bg0);
}

static void
test_renderer_applies_bg_mosaic(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_top_left;
  uint32_t expected_top_right;
  uint32_t expected_bottom_left;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  memset(gPfrVram + 0, 0x11, 32);
  memset(gPfrVram + 32, 0x22, 32);
  memset(gPfrVram + 64, 0x33, 32);
  memset(gPfrVram + 96, 0x44, 32);
  ((u16*)gPfrPltt)[1] = 0x001F;
  ((u16*)gPfrPltt)[2] = 0x03E0;
  ((u16*)gPfrPltt)[3] = 0x7C00;
  ((u16*)gPfrPltt)[4] = 0x03FF;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31 + 0 * sizeof(u16)) = 0;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31 + 1 * sizeof(u16)) = 1;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31 + 32 * sizeof(u16)) = 2;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31 + 33 * sizeof(u16)) = 3;

  pfr_renderer_init();

  REG_BG0CNT =
    BGCNT_SCREENBASE(31) | BGCNT_16COLOR | BGCNT_PRIORITY(0) | BGCNT_MOSAIC;
  REG_BG0HOFS = 7;
  REG_BG0VOFS = 7;
  REG_MOSAIC = 0x0011;
  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;

  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_top_left = test_rgb555_to_rgba8888(0x001F);
  expected_top_right = test_rgb555_to_rgba8888(0x03E0);
  expected_bottom_left = test_rgb555_to_rgba8888(0x7C00);
  assert(framebuffer[0] == expected_top_left);
  assert(framebuffer[1] == expected_top_left);
  assert(framebuffer[DISPLAY_WIDTH] == expected_top_left);
  assert(framebuffer[DISPLAY_WIDTH + 1] == expected_top_left);
  assert(framebuffer[2] == expected_top_right);
  assert(framebuffer[DISPLAY_WIDTH * 2] == expected_bottom_left);
}

static void
test_renderer_uses_captured_scanline_window_visibility(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_backdrop;
  uint32_t expected_bg0;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  ((u16*)gPfrPltt)[0] = 0x0400;
  ((u16*)gPfrPltt)[1] = 0x001F;
  gPfrVram[0] = 0x11;
  gPfrVram[4] = 0x11;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31) = 0;

  pfr_renderer_init();

  REG_BG0CNT = BGCNT_SCREENBASE(31) | BGCNT_16COLOR | BGCNT_PRIORITY(0);
  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON | DISPCNT_WIN0_ON;
  REG_WIN0H = WIN_RANGE(0, DISPLAY_WIDTH);
  REG_WINOUT = 0;

  pfr_renderer_begin_frame_capture();

  REG_WIN0V = WIN_RANGE(0, 1);
  REG_WININ = WININ_WIN0_BG0;
  pfr_renderer_capture_scanline(0);

  REG_WIN0V = WIN_RANGE(1, 2);
  REG_WININ = 0;
  pfr_renderer_capture_scanline(1);

  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_backdrop = test_rgb555_to_rgba8888(0x0400);
  expected_bg0 = test_rgb555_to_rgba8888(0x001F);
  assert(framebuffer[0] == expected_bg0);
  assert(framebuffer[DISPLAY_WIDTH] == expected_backdrop);
}

static void
test_renderer_uses_captured_scanline_bg0hofs(void)
{
  struct OamData* oam = (struct OamData*)gPfrOam;
  const uint32_t* framebuffer;
  uint32_t expected_tile0;
  uint32_t expected_tile1;
  u8* tile0 = gPfrVram;
  u8* tile1 = gPfrVram + 32;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  pfr_hide_all_test_sprites(oam);

  memset(tile0, 0x11, 32);
  memset(tile1, 0x22, 32);
  ((u16*)gPfrPltt)[1] = 0x001F;
  ((u16*)gPfrPltt)[2] = 0x03E0;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31) = 0;
  *(u16*)(gPfrVram + BG_SCREEN_SIZE * 31 + sizeof(u16)) = 1;

  pfr_renderer_init();

  REG_BG0CNT = BGCNT_SCREENBASE(31) | BGCNT_16COLOR | BGCNT_PRIORITY(0);
  REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;

  pfr_renderer_begin_frame_capture();
  REG_BG0HOFS = 0;
  pfr_renderer_capture_scanline(0);
  REG_BG0HOFS = 8;
  pfr_renderer_capture_scanline(1);

  pfr_renderer_render_frame();
  framebuffer = pfr_renderer_framebuffer();
  expected_tile0 = test_rgb555_to_rgba8888(0x001F);
  expected_tile1 = test_rgb555_to_rgba8888(0x03E0);
  assert(framebuffer[0] == expected_tile0);
  assert(framebuffer[DISPLAY_WIDTH] == expected_tile1);
}

static void
test_cpuset(void)
{
  u16 src16[4] = { 1, 2, 3, 4 };
  u16 dst16[4] = { 0, 0, 0, 0 };
  u32 src32[4] = { 10, 20, 30, 40 };
  u32 dst32[4] = { 0, 0, 0, 0 };
  u16 fill16 = 0x1234;
  u32 fill32 = 0x11223344;

  CpuSet(src16, dst16, CPU_SET_16BIT | 4);
  assert(memcmp(src16, dst16, sizeof(src16)) == 0);

  CpuSet(&fill16, dst16, CPU_SET_16BIT | CPU_SET_SRC_FIXED | 4);
  assert(dst16[0] == 0x1234 && dst16[3] == 0x1234);

  CpuFastSet(src32, dst32, 4);
  assert(memcmp(src32, dst32, sizeof(src32)) == 0);

  CpuFastSet(&fill32, dst32, CPU_FAST_SET_SRC_FIXED | 4);
  assert(dst32[0] == 0x11223344 && dst32[3] == 0x11223344);
}

static void
test_decompression(void)
{
  const u8 lz_data[] = { 0x10, 0x04, 0x00, 0x00, 0x00, 1, 2, 3, 4 };
  const u8 rl_data[] = { 0x30, 0x06, 0x00, 0x00, 0x83, 0xAB };
  u8 lz_out[4] = { 0 };
  u8 rl_out[6] = { 0 };
  size_t i;

  LZ77UnCompWram(lz_data, lz_out);
  assert(memcmp(lz_out, (u8[]){ 1, 2, 3, 4 }, sizeof(lz_out)) == 0);

  RLUnCompWram(rl_data, rl_out);
  for (i = 0; i < sizeof(rl_out); i++) {
    assert(rl_out[i] == 0xAB);
  }
}

static void
test_storage_roundtrip(void)
{
  const char* path = "pfr_smoke.sav";
  const char* temp_path = "pfr_smoke.sav.tmp";
  u8 buffer[64];
  u8 loaded[64];
  size_t i;

  for (i = 0; i < sizeof(buffer); i++) {
    buffer[i] = (u8)(i * 3);
  }

  remove(path);
  remove(temp_path);
  assert(pfr_storage_save(path, buffer, sizeof(buffer)));
  memset(loaded, 0, sizeof(loaded));
  assert(pfr_storage_load(path, loaded, sizeof(loaded)));
  assert(memcmp(buffer, loaded, sizeof(buffer)) == 0);
  remove(path);
  remove(temp_path);
}

static void
test_storage_overwrites_existing_file(void)
{
  const char* path = "pfr_smoke_overwrite.sav";
  const char* temp_path = "pfr_smoke_overwrite.sav.tmp";
  u8 initial[64];
  u8 updated[64];
  u8 loaded[64];
  size_t i;

  for (i = 0; i < sizeof(initial); i++) {
    initial[i] = (u8)i;
    updated[i] = (u8)(255U - i);
  }

  remove(path);
  remove(temp_path);

  assert(pfr_storage_save(path, initial, sizeof(initial)));
  assert(pfr_storage_save(path, updated, sizeof(updated)));

  memset(loaded, 0, sizeof(loaded));
  assert(pfr_storage_load(path, loaded, sizeof(loaded)));
  assert(memcmp(updated, loaded, sizeof(updated)) == 0);

  remove(path);
  remove(temp_path);
}

static void
test_storage_default_path(void)
{
  char path[PFR_MAX_PATH];
  const char* application_directory = GetApplicationDirectory();
  size_t directory_length = strlen(application_directory);

  assert(application_directory[0] != '\0');
  assert(pfr_storage_default_path(path, sizeof(path)));
  assert(strncmp(path, application_directory, directory_length) == 0);
  assert(strcmp(path + directory_length, "pokefirered.sav") == 0);
}

static void
test_tasks(void)
{
  u8 taskId;
  int marker = 1234;

  ResetTasks();
  sTaskLogCount = 0;
  CreateTask(test_task_priority_low, 10);
  CreateTask(test_task_priority_high, 1);
  CreateTask(test_task_priority_mid, 5);
  RunTasks();
  assert(sTaskLogCount == 3);
  assert(sTaskLog[0] == 1);
  assert(sTaskLog[1] == 2);
  assert(sTaskLog[2] == 3);
  assert(GetTaskCount() == 0);

  taskId = CreateTask(TaskDummy, 2);
  PfrSetTaskPtr(taskId, 0, &marker);
  assert(PfrGetTaskPtr(taskId, 0) == &marker);

  PfrSetTaskCallback(taskId, 1, test_task_followup);
  assert(PfrGetTaskCallback(taskId, 1) == test_task_followup);

  sFollowupState = 0;
  SetTaskFuncWithFollowupFunc(taskId, test_task_switcher, test_task_followup);
  RunTasks();
  assert(sFollowupState == 1);
  RunTasks();
  assert(sFollowupState == 2);
  assert(GetTaskCount() == 0);

  taskId = CreateTask(TaskDummy, 0);
  SetWordTaskArg(taskId, 4, 0x11223344UL);
  assert(GetWordTaskArg(taskId, 4) == 0x11223344UL);
  DestroyTask(taskId);

  taskId = CreateTask(TaskDummy, 0);
  SetWordTaskArg(taskId, 4, (uintptr_t)&marker);
  assert(GetWordTaskArg(taskId, 4) == (uintptr_t)&marker);
  DestroyTask(taskId);
}

static void
test_main_runtime(void)
{
  u32 vblankCounter = 0;
  struct OamData* oam = (struct OamData*)gPfrOam;

  pfr_main_init();
  sMainLogCount = 0;
  sVBlankCount = 0;

  gMain.callback1 = test_main_callback1;
  SetMainCallback2(test_main_callback2);
  SetVBlankCallback(test_main_vblank);
  SetVBlankCounter1Ptr(&vblankCounter);

  pfr_main_set_raw_keys(DPAD_UP | A_BUTTON);
  pfr_main_run_callbacks();
  assert(sMainLogCount == 2);
  assert(sMainLog[0] == 1);
  assert(sMainLog[1] == 2);
  assert(gMain.heldKeys == (DPAD_UP | A_BUTTON));
  assert(gMain.newKeys == (DPAD_UP | A_BUTTON));
  assert(gMain.newAndRepeatedKeys == (DPAD_UP | A_BUTTON));

  memset(gMain.oamBuffer, 0, sizeof(gMain.oamBuffer));
  memset(oam, 0xFF, PFR_OAM_SIZE);
  gMain.oamBuffer[0].x = 17;
  gMain.oamBuffer[0].y = 23;
  pfr_main_on_vblank();
  assert(sVBlankCount == 1);
  assert(vblankCounter == 1);
  assert(oam[0].x == 17);
  assert(oam[0].y == 23);

  pfr_main_set_raw_keys(DPAD_UP | A_BUTTON);
  pfr_main_run_callbacks();
  assert(gMain.newKeys == 0);

  gSaveBlock2Ptr->optionsButtonMode = OPTIONS_BUTTON_MODE_L_EQUALS_A;
  pfr_main_set_raw_keys(L_BUTTON);
  pfr_main_run_callbacks();
  assert(gMain.heldKeysRaw == L_BUTTON);
  assert(gMain.newKeysRaw == L_BUTTON);
  assert(gMain.heldKeys == (L_BUTTON | A_BUTTON));
  assert(gMain.newKeys == (L_BUTTON | A_BUTTON));

  pfr_main_shutdown();
}

static void
test_core_and_audio(void)
{
  PfrAudioStats stats;
  int16_t frame_audio[PFR_AUDIO_FRAMES_PER_GBA_FRAME * PFR_AUDIO_CHANNEL_COUNT];
  PfrAudioWindow intro_window = { 0 };
  PfrAudioWindow title_window = { 0 };
  PfrAudioWindow cry_window = { 0 };
  PfrAudioWindow menu_window = { 0 };
  bool game_freak_started = false;
  bool title_bgm_started = false;
  bool title_cry_started = false;
  bool title_bgm_stopped = false;
  bool main_menu_seen = false;
  bool menu_select_started = false;
  bool menu_input_sent = false;
  uint16_t se_song = 0;
  uint32_t frame_index;
  uint32_t main_menu_seen_frame = 0;

  pfr_core_init("pfr_smoke_core.sav", PFR_MODE_GAME);
  pfr_audio_reset();
  test_audio_window_begin(&intro_window, "intro", 120);

  for (frame_index = 0; frame_index < 2600; frame_index++) {
    uint16_t keys = 0;

    if (frame_index == 1860) {
      keys |= START_BUTTON;
    }

    if (pfr_core_is_main_menu_visible() && !main_menu_seen) {
      main_menu_seen = true;
      main_menu_seen_frame = frame_index;
    }

    if (main_menu_seen && !menu_input_sent &&
        frame_index >= main_menu_seen_frame + 120) {
      keys |= A_BUTTON;
      menu_input_sent = true;
    }

    pfr_core_set_keys(keys);
    pfr_core_run_frame();

    if (!game_freak_started && pfr_stub_current_bgm() == MUS_GAME_FREAK &&
        pfr_stub_is_bgm_playing()) {
      game_freak_started = true;
    }

    if (!title_bgm_started && pfr_stub_current_bgm() == MUS_TITLE &&
        pfr_stub_is_bgm_playing()) {
      title_bgm_started = true;
      test_audio_window_begin(&title_window, "title", 90);
    }

    if (title_bgm_started && !title_cry_started && pfr_stub_take_cry()) {
      title_cry_started = true;
      test_audio_window_begin(&cry_window, "cry", 45);
    }

    if (title_bgm_started && !title_bgm_stopped && !pfr_stub_is_bgm_playing()) {
      title_bgm_stopped = true;
    }

    if (!menu_select_started && pfr_stub_take_se(&se_song)) {
      if (se_song == SE_SELECT) {
        menu_select_started = true;
        test_audio_window_begin(&menu_window, "menu", 20);
      }
    }

    assert(pfr_audio_drain_source_frames(frame_audio,
                                         PFR_AUDIO_FRAMES_PER_GBA_FRAME) ==
           PFR_AUDIO_FRAMES_PER_GBA_FRAME);
    test_audio_window_feed(
      &intro_window, frame_audio, PFR_ARRAY_COUNT(frame_audio));
    test_audio_window_feed(
      &title_window, frame_audio, PFR_ARRAY_COUNT(frame_audio));
    test_audio_window_feed(
      &cry_window, frame_audio, PFR_ARRAY_COUNT(frame_audio));
    test_audio_window_feed(
      &menu_window, frame_audio, PFR_ARRAY_COUNT(frame_audio));

    if (pfr_core_should_quit()) {
      break;
    }
  }

  assert(pfr_core_frame_checksum() != 0);
  assert(game_freak_started);
  assert(main_menu_seen);
  assert(title_bgm_started);
  assert(title_cry_started);
  assert(title_bgm_stopped);
  assert(menu_select_started);
  assert(intro_window.done);
  assert(title_window.done);
  assert(cry_window.done);
  assert(menu_window.done);
  assert(intro_window.absmax == 0);
  assert(test_audio_window_rms(&intro_window) == 0.0);
  assert(title_window.absmax > 2048);
  assert(test_audio_window_rms(&title_window) > 512.0);
  assert(cry_window.absmax > 2048);
  assert(test_audio_window_rms(&cry_window) > 512.0);
  assert(menu_window.absmax > 2048);
  assert(test_audio_window_rms(&menu_window) > 512.0);

  gPfrRuntimeState.save[0] = 42;
  gPfrRuntimeState.save_dirty = true;
  pfr_core_flush_save();
  pfr_core_shutdown();

  stats = pfr_audio_stats();
  assert(stats.source_sample_rate == PFR_DEFAULT_AUDIO_SAMPLE_RATE);
  assert(stats.queued_frames == pfr_audio_available_frames());
  remove("pfr_smoke_core.sav");
}

int
main(void)
{
  printf("pfr_smoke: starting tests...\n");
  fflush(stdout);
  test_oam_layout();
  printf("pfr_smoke: test_oam_layout ok\n");
  fflush(stdout);
  test_cpuset();
  printf("pfr_smoke: test_cpuset ok\n");
  fflush(stdout);
  test_decompression();
  printf("pfr_smoke: test_decompression ok\n");
  fflush(stdout);
  test_dma3();
  printf("pfr_smoke: test_dma3 ok\n");
  fflush(stdout);
  test_gpu_regs();
  printf("pfr_smoke: test_gpu_regs ok\n");
  fflush(stdout);
  test_renderer_respects_obj_disable();
  printf("pfr_smoke: test_renderer_respects_obj_disable ok\n");
  fflush(stdout);
  test_renderer_applies_obj_mosaic();
  printf("pfr_smoke: test_renderer_applies_obj_mosaic ok\n");
  fflush(stdout);
  test_renderer_anchors_obj_mosaic_to_sprite_origin();
  printf("pfr_smoke: test_renderer_anchors_obj_mosaic_to_sprite_origin ok\n");
  fflush(stdout);
  test_renderer_blends_semi_transparent_obj_even_when_bldcnt_is_not_blend();
  printf("pfr_smoke: "
         "test_renderer_blends_semi_transparent_obj_even_when_bldcnt_is_not_"
         "blend ok\n");
  fflush(stdout);
  test_renderer_uses_fixed_2d_8bpp_obj_row_stride();
  printf("pfr_smoke: test_renderer_uses_fixed_2d_8bpp_obj_row_stride ok\n");
  fflush(stdout);
  test_renderer_skips_affine_erase_sprites();
  printf("pfr_smoke: test_renderer_skips_affine_erase_sprites ok\n");
  fflush(stdout);
  test_renderer_samples_affine_double_sprites();
  printf("pfr_smoke: test_renderer_samples_affine_double_sprites ok\n");
  fflush(stdout);
  test_renderer_respects_forced_blank();
  printf("pfr_smoke: test_renderer_respects_forced_blank ok\n");
  fflush(stdout);
  test_renderer_limits_scanline_sprites_to_32_objs();
  printf("pfr_smoke: test_renderer_limits_scanline_sprites_to_32_objs ok\n");
  fflush(stdout);
  test_renderer_obj_window_lightens_bg0();
  printf("pfr_smoke: test_renderer_obj_window_lightens_bg0 ok\n");
  fflush(stdout);
  test_renderer_uses_captured_scanline_bldy();
  printf("pfr_smoke: test_renderer_uses_captured_scanline_bldy ok\n");
  fflush(stdout);
  test_renderer_affine_bg_color_zero_is_transparent();
  printf("pfr_smoke: test_renderer_affine_bg_color_zero_is_transparent ok\n");
  fflush(stdout);
  test_renderer_applies_bg_mosaic();
  printf("pfr_smoke: test_renderer_applies_bg_mosaic ok\n");
  fflush(stdout);
  test_renderer_uses_captured_scanline_window_visibility();
  printf(
    "pfr_smoke: test_renderer_uses_captured_scanline_window_visibility ok\n");
  fflush(stdout);
  test_renderer_uses_captured_scanline_bg0hofs();
  printf("pfr_smoke: test_renderer_uses_captured_scanline_bg0hofs ok\n");
  fflush(stdout);
  test_storage_roundtrip();
  printf("pfr_smoke: test_storage_roundtrip ok\n");
  fflush(stdout);
  test_storage_overwrites_existing_file();
  printf("pfr_smoke: test_storage_overwrites_existing_file ok\n");
  fflush(stdout);
  test_storage_default_path();
  printf("pfr_smoke: test_storage_default_path ok\n");
  fflush(stdout);
  test_audio_resampler();
  printf("pfr_smoke: test_audio_resampler ok\n");
  fflush(stdout);
  test_tasks();
  printf("pfr_smoke: test_tasks ok\n");
  fflush(stdout);
  test_main_runtime();
  printf("pfr_smoke: test_main_runtime ok\n");
  fflush(stdout);
  test_core_and_audio();
  printf("pfr_smoke: test_core_and_audio ok\n");
  fflush(stdout);

  printf("pfr_smoke: all tests passed!\n");
  fflush(stdout);
  return 0;
}
