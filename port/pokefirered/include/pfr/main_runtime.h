#ifndef PFR_MAIN_RUNTIME_H
#define PFR_MAIN_RUNTIME_H

#include "gba/types.h"

void
pfr_main_init(void);
void
pfr_main_shutdown(void);
void
pfr_main_set_raw_keys(u16 keys);
void
pfr_main_run_callbacks(void);
void
pfr_main_on_hblank(void);
void
pfr_main_on_vcount(void);
void
pfr_main_on_vblank(void);

void
pfr_game_boot(void);

#endif
