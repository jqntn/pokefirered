#include <stdio.h>
#include <string.h>

#include "gba/io_reg.h"
#include "pfr/core.h"
#include "pfr/demo.h"
#include "pfr/renderer.h"
#include "pfr/storage.h"

typedef union PfrAlignedStorage
{
  uint8_t bytes[PFR_VRAM_SIZE];
  uint32_t alignment;
} PfrAlignedStorage;

typedef union PfrSmallStorage
{
  uint8_t bytes[PFR_PLTT_SIZE];
  uint32_t alignment;
} PfrSmallStorage;

typedef union PfrIoStorage
{
  uint8_t bytes[PFR_IO_SIZE];
  uint32_t alignment;
} PfrIoStorage;

static union
{
  uint8_t bytes[PFR_EWRAM_SIZE];
  uint32_t alignment;
} sPfrEwramStorage;

static union
{
  uint8_t bytes[PFR_IWRAM_SIZE];
  uint32_t alignment;
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
  REG_KEYINPUT = KEYS_MASK;
}

static void
pfr_core_simulate_scanlines(void)
{
  int scanline;

  for (scanline = 0; scanline < 228; scanline++) {
    REG_VCOUNT = (u16)scanline;

    if (scanline >= DISPLAY_HEIGHT) {
      REG_DISPSTAT |= DISPSTAT_VBLANK;
    } else {
      REG_DISPSTAT &= (u16)~DISPSTAT_VBLANK;
    }
  }

  REG_IF = INTR_FLAG_VBLANK;
  gPfrIntrCheck |= INTR_FLAG_VBLANK;
  REG_VCOUNT = 0;
  REG_DISPSTAT &= (u16)~DISPSTAT_VBLANK;
}

void
pfr_core_init(const char* save_path)
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

  pfr_renderer_init();
  pfr_demo_boot();
}

void
pfr_core_shutdown(void)
{
  pfr_core_flush_save();
  pfr_renderer_shutdown();
}

void
pfr_core_set_keys(uint16_t keys_held)
{
  gPfrRuntimeState.keys_pressed =
    (u16)(keys_held & ~gPfrRuntimeState.keys_held);
  gPfrRuntimeState.keys_held = keys_held;
  REG_KEYINPUT = (u16)(KEYS_MASK & ~keys_held);
}

void
pfr_core_run_frame(void)
{
  pfr_demo_run_frame();
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
