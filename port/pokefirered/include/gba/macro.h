#ifndef GUARD_GBA_MACRO_H
#define GUARD_GBA_MACRO_H

#include <stdint.h>

#include "gba/io_reg.h"
#include "gba/syscall.h"
#include "pfr/dma.h"

#define CPU_FILL(value, dest, size, bit)                                       \
  {                                                                            \
    vu##bit tmp = (vu##bit)(value);                                            \
    CpuSet((void*)&tmp,                                                        \
           dest,                                                               \
           CPU_SET_##bit##BIT | CPU_SET_SRC_FIXED |                            \
             ((size) / (bit / 8) & 0x1FFFFF));                                 \
  }

#define CpuFill16(value, dest, size) CPU_FILL(value, dest, size, 16)
#define CpuFill32(value, dest, size) CPU_FILL(value, dest, size, 32)

#define CPU_COPY(src, dest, size, bit)                                         \
  CpuSet(src, dest, CPU_SET_##bit##BIT | ((size) / (bit / 8) & 0x1FFFFF))

#define CpuCopy16(src, dest, size) CPU_COPY(src, dest, size, 16)
#define CpuCopy32(src, dest, size) CPU_COPY(src, dest, size, 32)

#define CpuFastFill(value, dest, size)                                         \
  {                                                                            \
    vu32 tmp = (vu32)(value);                                                  \
    CpuFastSet((void*)&tmp,                                                    \
               dest,                                                           \
               CPU_FAST_SET_SRC_FIXED | ((size) / (32 / 8) & 0x1FFFFF));       \
  }

#define CpuFastFill16(value, dest, size)                                       \
  CpuFastFill(((value) << 16) | (value), (dest), (size))

#define CpuFastFill8(value, dest, size)                                        \
  CpuFastFill(((value) << 24) | ((value) << 16) | ((value) << 8) | (value),    \
              (dest),                                                          \
              (size))

#define CpuFastCopy(src, dest, size)                                           \
  CpuFastSet(src, dest, ((size) / (32 / 8) & 0x1FFFFF))

#define DmaSet(dmaNum, src, dest, control)                                     \
  pfr_dma_set((dmaNum), (src), (dest), (control))

#define DMA_FILL(dmaNum, value, dest, size, bit)                               \
  {                                                                            \
    vu##bit tmp = (vu##bit)(value);                                            \
    DmaSet(dmaNum,                                                             \
           &tmp,                                                               \
           dest,                                                               \
           (DMA_ENABLE | DMA_START_NOW | DMA_##bit##BIT | DMA_SRC_FIXED |      \
            DMA_DEST_INC)                                                      \
               << 16 |                                                         \
             ((size) / (bit / 8)));                                            \
  }

#define DmaFill16(dmaNum, value, dest, size)                                   \
  DMA_FILL(dmaNum, value, dest, size, 16)
#define DmaFill32(dmaNum, value, dest, size)                                   \
  DMA_FILL(dmaNum, value, dest, size, 32)

#define DMA_COPY(dmaNum, src, dest, size, bit)                                 \
  {                                                                            \
    DmaSet(dmaNum,                                                             \
           src,                                                                \
           dest,                                                               \
           (DMA_ENABLE | DMA_START_NOW | DMA_##bit##BIT | DMA_SRC_INC |        \
            DMA_DEST_INC)                                                      \
               << 16 |                                                         \
             ((size) / (bit / 8)));                                            \
  }

#define DmaCopy16(dmaNum, src, dest, size) DMA_COPY(dmaNum, src, dest, size, 16)
#define DmaCopy32(dmaNum, src, dest, size) DMA_COPY(dmaNum, src, dest, size, 32)

#define DmaStop(dmaNum) pfr_dma_stop(dmaNum)

#endif
