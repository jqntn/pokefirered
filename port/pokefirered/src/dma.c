#include "pfr/dma.h"
#include "pfr/core.h"

#include <string.h>

#include "gba/defines.h"
#include "gba/io_reg.h"

typedef struct PfrDmaChannel
{
  uintptr_t src_base;
  uintptr_t dest_base;
  uintptr_t src_current;
  uintptr_t dest_current;
  u32 count;
  u16 control;
  bool8 active;
} PfrDmaChannel;

static PfrDmaChannel sPfrDmaChannels[4];
static const u16 sPfrDmaInterruptFlags[4] = {
  INTR_FLAG_DMA0,
  INTR_FLAG_DMA1,
  INTR_FLAG_DMA2,
  INTR_FLAG_DMA3,
};

enum
{
  PFR_DMA_REGISTER_SIZE = sizeof(u32) * 3U,
};

static uint8_t*
pfr_dma_reg_block(u8 dmaNum)
{
  return gPfrIo + REG_OFFSET_DMA0 + (size_t)dmaNum * PFR_DMA_REGISTER_SIZE;
}

static volatile u32*
pfr_dma_regs(u8 dmaNum)
{
  return (volatile u32*)(void*)pfr_dma_reg_block(dmaNum);
}

static volatile u16*
pfr_dma_reg16(u8 dmaNum)
{
  return (volatile u16*)(void*)pfr_dma_reg_block(dmaNum);
}

static u32
pfr_dma_count(u8 dmaNum, u32 control)
{
  u32 count = control & 0xFFFFU;

  if (count != 0) {
    return count;
  }

  return dmaNum == 3 ? 0x10000U : 0x4000U;
}

static uintptr_t
pfr_dma_next_dest(uintptr_t address, u16 control, size_t transfer_size)
{
  switch (control & 0x0060) {
    case DMA_DEST_DEC:
      return address - transfer_size;
    case DMA_DEST_FIXED:
      return address;
    case DMA_DEST_RELOAD:
    case DMA_DEST_INC:
    default:
      return address + transfer_size;
  }
}

static uintptr_t
pfr_dma_next_src(uintptr_t address, u16 control, size_t transfer_size)
{
  switch (control & 0x0180) {
    case DMA_SRC_DEC:
      return address - transfer_size;
    case DMA_SRC_FIXED:
      return address;
    case DMA_SRC_INC:
    default:
      return address + transfer_size;
  }
}

static void
pfr_dma_disable(u8 dmaNum)
{
  PfrDmaChannel* channel = &sPfrDmaChannels[dmaNum];
  volatile u16* regs16 = pfr_dma_reg16(dmaNum);

  channel->active = FALSE;
  channel->control &= (u16) ~(DMA_REPEAT | DMA_ENABLE | DMA_START_MASK);
  regs16[5] &= (u16) ~(DMA_REPEAT | DMA_ENABLE | DMA_START_MASK | DMA_DREQ_ON);
}

static void
pfr_dma_signal_interrupt(u8 dmaNum, u16 control)
{
  u16 intr_flag;

  if ((control & DMA_INTR_ENABLE) == 0) {
    return;
  }

  intr_flag = sPfrDmaInterruptFlags[dmaNum];
  REG_IF |= intr_flag;
  INTR_CHECK |= intr_flag;
}

static void
pfr_dma_transfer(u8 dmaNum)
{
  PfrDmaChannel* channel = &sPfrDmaChannels[dmaNum];
  uintptr_t src = channel->src_current;
  uintptr_t dest = channel->dest_current;
  size_t transfer_size;
  u32 i;

  if (!channel->active || src == 0 || dest == 0) {
    return;
  }

  transfer_size =
    (channel->control & DMA_32BIT) != 0 ? sizeof(u32) : sizeof(u16);

  for (i = 0; i < channel->count; i++) {
    if (transfer_size == sizeof(u32)) {
      u32 value;

      memcpy(&value, (const void*)src, sizeof(value));
      *(volatile u32*)dest = value;
    } else {
      u16 value;

      memcpy(&value, (const void*)src, sizeof(value));
      *(volatile u16*)dest = value;
    }

    src = pfr_dma_next_src(src, channel->control, transfer_size);
    dest = pfr_dma_next_dest(dest, channel->control, transfer_size);
  }

  channel->src_current = src;

  if ((channel->control & 0x0060) == DMA_DEST_RELOAD) {
    channel->dest_current = channel->dest_base;
  } else {
    channel->dest_current = dest;
  }

  pfr_dma_signal_interrupt(dmaNum, channel->control);

  if ((channel->control & DMA_REPEAT) == 0) {
    pfr_dma_disable(dmaNum);
  }
}

void
pfr_dma_reset(void)
{
  memset(sPfrDmaChannels, 0, sizeof(sPfrDmaChannels));
  memset(pfr_dma_reg_block(0), 0, 4U * PFR_DMA_REGISTER_SIZE);
}

void
pfr_dma_set(u8 dmaNum, const void* src, volatile void* dest, u32 control)
{
  PfrDmaChannel* channel;
  volatile u32* regs;

  if (dmaNum >= 4) {
    return;
  }

  regs = pfr_dma_regs(dmaNum);
  regs[0] = (u32)(uintptr_t)src;
  regs[1] = (u32)(uintptr_t)dest;
  regs[2] = control;

  channel = &sPfrDmaChannels[dmaNum];
  channel->src_base = (uintptr_t)src;
  channel->dest_base = (uintptr_t)dest;
  channel->src_current = channel->src_base;
  channel->dest_current = channel->dest_base;
  channel->count = pfr_dma_count(dmaNum, control);
  channel->control = (u16)(control >> 16);
  channel->active = (channel->control & DMA_ENABLE) != 0;

  if (channel->active && (channel->control & DMA_START_MASK) == DMA_START_NOW) {
    pfr_dma_transfer(dmaNum);
  }
}

void
pfr_dma_stop(u8 dmaNum)
{
  if (dmaNum >= 4) {
    return;
  }

  pfr_dma_disable(dmaNum);
}

void
pfr_dma_on_hblank(void)
{
  u8 dmaNum;

  if (REG_VCOUNT >= DISPLAY_HEIGHT) {
    return;
  }

  for (dmaNum = 0; dmaNum < 4; dmaNum++) {
    if ((sPfrDmaChannels[dmaNum].control & DMA_START_MASK) ==
        DMA_START_HBLANK) {
      pfr_dma_transfer(dmaNum);
    }
  }
}
