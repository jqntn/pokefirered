#ifndef PFR_STUBS_H
#define PFR_STUBS_H

#include "gba/types.h"

/* Serial / Link */
void ResetSerial(void);
void SerialCB(void);
void SetSerialCallback(void (*cb)(void));

/* Graphics / Blend Tasks */
void StartBlendTask(s8 startY, s8 targetY, s8 deltaY, u8 delay, u8 submode, u32 selectedPalettes);
bool8 IsBlendTaskActive(void);

#endif /* PFR_STUBS_H */
