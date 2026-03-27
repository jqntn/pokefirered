#include "gba/io_reg.h"
#include <stdio.h>
#include <string.h>

#include "dma3.h"
#include "gpu_regs.h"
#include "main.h"
#include "pfr/core.h"
#include "pfr/demo.h"
#include "pfr/main_runtime.h"
#include "pfr/renderer.h"
#include "pfr/sandbox.h"
#include "pfr/storage.h"
#include "task.h"

typedef union PfrAlignedStorage
{
  uint8_t bytes[PFR_VRAM_SIZE];
  uint64_t alignment;
} PfrAlignedStorage;

typedef union PfrSmallStorage
{
  uint8_t bytes[PFR_PLTT_SIZE];
  uint64_t alignment;
} PfrSmallStorage;

typedef union PfrIoStorage
{
  uint8_t bytes[PFR_IO_SIZE];

  uint64_t alignment;
} PfrIoStorage;

#include "global.h"

struct SaveBlock1 gSaveBlock1;
struct SaveBlock2 gSaveBlock2;
struct SaveBlock1* gSaveBlock1Ptr = &gSaveBlock1;
struct SaveBlock2* gSaveBlock2Ptr = &gSaveBlock2;

static union
{
  uint8_t bytes[PFR_EWRAM_SIZE];
  uint64_t alignment;
} sPfrEwramStorage;

static union
{
  uint8_t bytes[PFR_IWRAM_SIZE];
  uint64_t alignment;
} sPfrIwramStorage;

static PfrAlignedStorage sPfrVramStorage;
static PfrSmallStorage sPfrPlttStorage;
static PfrSmallStorage sPfrOamStorage;
static PfrIoStorage sPfrIoStorage;

PfrRuntimeState gPfrRuntimeState = { 0 };

uint8_t* gPfrEwram = sPfrEwramStorage.bytes;
uint8_t* gPfrIwram = sPfrIwramStorage.bytes;
uint8_t* gPfrVram = sPfrVramStorage.bytes;
uint8_t* gPfrPltt = sPfrPlttStorage.bytes;
uint8_t* gPfrOam = sPfrOamStorage.bytes;
uint8_t* gPfrIo = sPfrIoStorage.bytes;
void* gPfrIntrVector = NULL;
volatile u16 gPfrIntrCheck = 0;
void* gPfrSoundInfoPtr = NULL;

static void
pfr_core_reset_memory(void)
{
  memset(gPfrEwram, 0, PFR_EWRAM_SIZE);
  memset(gPfrIwram, 0, PFR_IWRAM_SIZE);
  memset(gPfrVram, 0, PFR_VRAM_SIZE);
  memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  memset(gPfrOam, 0, PFR_OAM_SIZE);
  memset(gPfrIo, 0, PFR_IO_SIZE);
  gPfrIntrVector = NULL;
  gPfrIntrCheck = 0;
  gPfrSoundInfoPtr = NULL;
}

static void
pfr_core_simulate_scanlines(void)
{
  u16 vcount_line = (u16)(REG_DISPSTAT >> 8);
  int scanline;

  pfr_renderer_begin_frame_capture();

  /* The main callback runs before WaitForVBlank. Apply that VBlank work first,
   * then capture the visible scanlines that the hardware would display next. */
  for (scanline = DISPLAY_HEIGHT; scanline < 228; scanline++) {
    REG_VCOUNT = (u16)scanline;

    if ((REG_DISPSTAT & DISPSTAT_VCOUNT_INTR) != 0 && scanline == vcount_line) {
      pfr_main_on_vcount();
    }

    if ((REG_DISPSTAT & DISPSTAT_VBLANK) == 0) {
      REG_DISPSTAT |= DISPSTAT_VBLANK;
      pfr_main_on_vblank();
    }

    REG_DISPSTAT |= DISPSTAT_HBLANK;
    pfr_main_on_hblank();
    REG_DISPSTAT &= (u16)~DISPSTAT_HBLANK;
  }

  for (scanline = 0; scanline < DISPLAY_HEIGHT; scanline++) {
    REG_VCOUNT = (u16)scanline;
    REG_DISPSTAT &= (u16)~DISPSTAT_VBLANK;

    if ((REG_DISPSTAT & DISPSTAT_VCOUNT_INTR) != 0 && scanline == vcount_line) {
      pfr_main_on_vcount();
    }

    pfr_renderer_capture_scanline(scanline);

    REG_DISPSTAT |= DISPSTAT_HBLANK;
    pfr_main_on_hblank();
    REG_DISPSTAT &= (u16)~DISPSTAT_HBLANK;
  }

  REG_VCOUNT = 0;
  REG_DISPSTAT &= (u16)~DISPSTAT_VBLANK;
}

void
pfr_core_init(const char* save_path, PfrBootMode boot_mode)
{
  memset(&gPfrRuntimeState, 0, sizeof(gPfrRuntimeState));
  pfr_core_reset_memory();

  if (save_path == NULL) {
    pfr_storage_default_path(gPfrRuntimeState.save_path,
                             sizeof(gPfrRuntimeState.save_path));
  } else {
    snprintf(gPfrRuntimeState.save_path,
             sizeof(gPfrRuntimeState.save_path),
             "%s",
             save_path);
  }

  gPfrRuntimeState.save_loaded =
    pfr_storage_load(gPfrRuntimeState.save_path,
                     gPfrRuntimeState.save,
                     sizeof(gPfrRuntimeState.save));

  InitGpuRegManager();
  pfr_main_init();
  ResetTasks();
  ClearDma3Requests();
  pfr_renderer_init();

  if (boot_mode == PFR_BOOT_DEMO) {
    pfr_demo_boot();
  } else if (boot_mode == PFR_BOOT_FRONTEND) {
    pfr_frontend_boot();
  } else if (boot_mode == PFR_BOOT_SANDBOX) {
    pfr_sandbox_boot();
  } else {
    pfr_game_boot();
  }
}

void
pfr_core_shutdown(void)
{
  pfr_core_flush_save();
  pfr_main_shutdown();
  pfr_renderer_shutdown();
}

void
pfr_core_set_keys(uint16_t keys_held)
{
  pfr_main_set_raw_keys(keys_held);
}

void
pfr_core_run_frame(void)
{
  pfr_main_run_callbacks();
  gPfrRuntimeState.keys_held = gMain.heldKeys;
  gPfrRuntimeState.keys_pressed = gMain.newKeys;
  pfr_core_simulate_scanlines();
  pfr_renderer_render_frame();
  gPfrRuntimeState.frame_counter++;
}

void
pfr_core_flush_save(void)
{
  if (!gPfrRuntimeState.save_dirty) {
    return;
  }

  if (pfr_storage_save(gPfrRuntimeState.save_path,
                       gPfrRuntimeState.save,
                       sizeof(gPfrRuntimeState.save))) {
    gPfrRuntimeState.save_dirty = false;
  }
}

void
pfr_core_request_quit(void)
{
  gPfrRuntimeState.quit_requested = true;
}

bool
pfr_core_should_quit(void)
{
  return gPfrRuntimeState.quit_requested;
}

bool
pfr_core_is_title_visible(void)
{
  return gPfrRuntimeState.title_visible;
}

bool
pfr_core_is_main_menu_visible(void)
{
  return gPfrRuntimeState.main_menu_visible;
}

const uint32_t*
pfr_core_framebuffer(void)
{
  return pfr_renderer_framebuffer();
}

uint32_t
pfr_core_frame_checksum(void)
{
  return pfr_renderer_checksum();
}

const char*
pfr_core_status_line(void)
{
  return gPfrRuntimeState.status_line;
}
