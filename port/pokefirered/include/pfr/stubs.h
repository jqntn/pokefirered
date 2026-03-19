#ifndef PFR_STUBS_H
#define PFR_STUBS_H

#include "gba/types.h"

/* Serial / Link */
void ResetSerial(void);
void SerialCB(void);
void SetSerialCallback(void (*cb)(void));

/* Graphics / Blend Tasks */
void StartBlendTask(u8 eva_start, u8 evb_start, u8 eva_end, u8 evb_end, u8 ev_step, u8 priority);
bool8 IsBlendTaskActive(void);

#endif /* PFR_STUBS_H */
