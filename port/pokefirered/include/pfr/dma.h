#ifndef PFR_DMA_H
#define PFR_DMA_H

#include "gba/types.h"

void
pfr_dma_reset(void);
void
pfr_dma_set(u8 dmaNum, const void* src, volatile void* dest, u32 control);
void
pfr_dma_stop(u8 dmaNum);
void
pfr_dma_on_hblank(void);

#endif
