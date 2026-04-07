#ifndef PFR_STUBS_H
#define PFR_STUBS_H

#include "gba/types.h"

void
ResetSerial(void);
void
SerialCB(void);
void
SetSerialCallback(void (*cb)(void));

void
StartBlendTask(u8 eva_start,
               u8 evb_start,
               u8 eva_end,
               u8 evb_end,
               u8 ev_step,
               u8 priority);
bool8
IsBlendTaskActive(void);

u16
pfr_stub_current_bgm(void);
bool8
pfr_stub_is_bgm_playing(void);
bool8
pfr_stub_take_se(u16* song_num);
bool8
pfr_stub_take_cry(void);

#endif
