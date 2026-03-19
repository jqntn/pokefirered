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

/* Tile Data Buffers */
void ResetBgPositions(void);
void ResetTempTileDataBuffers(void);
void FreeTempTileDataBuffersIfPossible(void);
void DecompressAndCopyTileDataToVram(u8 bgId, const void *src, u16 size, u16 offset, u8 mode);

#endif /* PFR_STUBS_H */
