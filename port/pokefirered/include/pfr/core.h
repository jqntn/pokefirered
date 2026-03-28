#ifndef PFR_CORE_H
#define PFR_CORE_H

#include "gba/types.h"
#include "pfr/common.h"

typedef struct PfrRuntimeState
{
  uint8_t save[PFR_SAVE_SIZE];
  char save_path[PFR_MAX_PATH];
  char status_line[160];
  uint32_t frame_counter;
  uint16_t keys_held;
  uint16_t keys_pressed;
  bool save_loaded;
  bool save_dirty;
  bool quit_requested;
  bool title_visible;
  bool main_menu_visible;
} PfrRuntimeState;

extern PfrRuntimeState gPfrRuntimeState;

extern uint8_t* gPfrEwram;
extern uint8_t* gPfrIwram;
extern uint8_t* gPfrVram;
extern uint8_t* gPfrPltt;
extern uint8_t* gPfrOam;
extern uint8_t* gPfrIo;
extern void* gPfrIntrVector;
extern volatile u16 gPfrIntrCheck;
extern void* gPfrSoundInfoPtr;

typedef enum PfrMode
{
  PFR_MODE_GAME,
  PFR_MODE_DEMO,
  PFR_MODE_SANDBOX
} PfrMode;

void
pfr_core_init(const char* save_path, PfrMode mode);
void
pfr_core_shutdown(void);
void
pfr_core_set_keys(uint16_t keys_held);
void
pfr_core_run_frame(void);
void
pfr_core_flush_save(void);
void
pfr_core_request_quit(void);
bool
pfr_core_should_quit(void);
bool
pfr_core_is_title_visible(void);
bool
pfr_core_is_main_menu_visible(void);
const uint32_t*
pfr_core_framebuffer(void);
uint32_t
pfr_core_frame_checksum(void);
const char*
pfr_core_status_line(void);

#endif
