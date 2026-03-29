#ifndef GUARD_DMA3_H
#define GUARD_DMA3_H

#include "gba/gba.h"

#define MAX_DMA_BLOCK_SIZE 0x1000

#define DMA_REQUEST_COPY32 1
#define DMA_REQUEST_FILL32 2
#define DMA_REQUEST_COPY16 3
#define DMA_REQUEST_FILL16 4

#define DMA3_16BIT 0
#define DMA3_32BIT 1

void
Dma3CopyLarge16_(const void* src, void* dest, u32 size);
void
Dma3CopyLarge32_(const void* src, void* dest, u32 size);
void
Dma3FillLarge16_(u16 value, void* dest, u32 size);
void
Dma3FillLarge32_(u32 value, void* dest, u32 size);

void
ClearDma3Requests(void);
void
ProcessDma3Requests(void);
s16
RequestDma3Copy(const void* src, void* dest, u16 size, u8 mode);
s16
RequestDma3Fill(s32 value, void* dest, u16 size, u8 mode);
s16
WaitDma3Request(s16 index);

#endif
