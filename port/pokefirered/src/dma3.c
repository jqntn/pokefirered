#include "dma3.h"
#include <string.h>

enum
{
  PFR_MAX_DMA_REQUESTS = 128,
  PFR_MAX_VBLANK_TRANSFER = 40 * 1024,
};

typedef struct PfrDma3Request
{
  const u8* src;
  u8* dest;
  u16 size;
  u16 mode;
  u32 value;
} PfrDma3Request;

static PfrDma3Request sDma3Requests[PFR_MAX_DMA_REQUESTS];
static volatile bool8 sDma3ManagerLocked;
static u8 sDma3RequestCursor;

static void
pfr_dma3_copy_large(const void* src, void* dest, u32 size)
{
  const u8* src_bytes = (const u8*)src;
  u8* dest_bytes = (u8*)dest;
  u32 remaining = size;

  while (remaining > 0) {
    u32 blockSize = remaining;

    if (blockSize > MAX_DMA_BLOCK_SIZE) {
      blockSize = MAX_DMA_BLOCK_SIZE;
    }

    memcpy(dest_bytes, src_bytes, blockSize);
    src_bytes += blockSize;
    dest_bytes += blockSize;
    remaining -= blockSize;
  }
}

static void
pfr_dma3_fill_large(u32 value, void* dest, u32 size, size_t unitSize)
{
  u8* dest_bytes = (u8*)dest;
  u32 remaining = size;

  while (remaining > 0) {
    u32 blockSize = remaining;
    u32 offset;

    if (blockSize > MAX_DMA_BLOCK_SIZE) {
      blockSize = MAX_DMA_BLOCK_SIZE;
    }

    for (offset = 0; offset < blockSize; offset += (u32)unitSize) {
      memcpy(dest_bytes + offset, &value, unitSize);
    }

    dest_bytes += blockSize;
    remaining -= blockSize;
  }
}

static s16
pfr_find_free_dma3_slot(void)
{
  int cursor = sDma3RequestCursor;
  int count;

  for (count = 0; count < PFR_MAX_DMA_REQUESTS; count++) {
    if (sDma3Requests[cursor].size == 0) {
      return (s16)cursor;
    }

    cursor++;
    if (cursor >= PFR_MAX_DMA_REQUESTS) {
      cursor = 0;
    }
  }

  return -1;
}

void
Dma3CopyLarge16_(const void* src, void* dest, u32 size)
{
  pfr_dma3_copy_large(src, dest, size);
}

void
Dma3CopyLarge32_(const void* src, void* dest, u32 size)
{
  pfr_dma3_copy_large(src, dest, size);
}

void
Dma3FillLarge16_(u16 value, void* dest, u32 size)
{
  pfr_dma3_fill_large(value, dest, size, sizeof(value));
}

void
Dma3FillLarge32_(u32 value, void* dest, u32 size)
{
  pfr_dma3_fill_large(value, dest, size, sizeof(value));
}

void
ClearDma3Requests(void)
{
  sDma3ManagerLocked = TRUE;
  sDma3RequestCursor = 0;
  memset(sDma3Requests, 0, sizeof(sDma3Requests));
  sDma3ManagerLocked = FALSE;
}

void
ProcessDma3Requests(void)
{
  u32 bytesTransferred = 0;

  if (sDma3ManagerLocked) {
    return;
  }

  while (sDma3Requests[sDma3RequestCursor].size != 0) {
    PfrDma3Request* request = &sDma3Requests[sDma3RequestCursor];

    bytesTransferred += request->size;

    if (bytesTransferred > PFR_MAX_VBLANK_TRANSFER) {
      return;
    }

    if (REG_VCOUNT > 224) {
      return;
    }

    switch (request->mode) {
      case DMA_REQUEST_COPY32:
        Dma3CopyLarge32_(request->src, request->dest, request->size);
        break;
      case DMA_REQUEST_FILL32:
        Dma3FillLarge32_(request->value, request->dest, request->size);
        break;
      case DMA_REQUEST_COPY16:
        Dma3CopyLarge16_(request->src, request->dest, request->size);
        break;
      case DMA_REQUEST_FILL16:
        Dma3FillLarge16_((u16)request->value, request->dest, request->size);
        break;
      default:
        break;
    }

    memset(request, 0, sizeof(*request));
    sDma3RequestCursor++;

    if (sDma3RequestCursor >= PFR_MAX_DMA_REQUESTS) {
      sDma3RequestCursor = 0;
    }
  }
}

s16
RequestDma3Copy(const void* src, void* dest, u16 size, u8 mode)
{
  s16 slot;

  sDma3ManagerLocked = TRUE;
  slot = pfr_find_free_dma3_slot();

  if (slot >= 0) {
    sDma3Requests[slot].src = (const u8*)src;
    sDma3Requests[slot].dest = (u8*)dest;
    sDma3Requests[slot].size = size;
    sDma3Requests[slot].mode =
      (mode == DMA3_32BIT) ? DMA_REQUEST_COPY32 : DMA_REQUEST_COPY16;
  }

  sDma3ManagerLocked = FALSE;
  return slot;
}

s16
RequestDma3Fill(s32 value, void* dest, u16 size, u8 mode)
{
  s16 slot;

  sDma3ManagerLocked = TRUE;
  slot = pfr_find_free_dma3_slot();

  if (slot >= 0) {
    sDma3Requests[slot].dest = (u8*)dest;
    sDma3Requests[slot].size = size;
    sDma3Requests[slot].mode =
      (mode == DMA3_32BIT) ? DMA_REQUEST_FILL32 : DMA_REQUEST_FILL16;
    sDma3Requests[slot].value = (u32)value;
  }

  sDma3ManagerLocked = FALSE;
  return slot;
}

s16
WaitDma3Request(s16 index)
{
  int current;

  if (index == -1) {
    for (current = 0; current < PFR_MAX_DMA_REQUESTS; current++) {
      if (sDma3Requests[current].size != 0) {
        return -1;
      }
    }

    return 0;
  }

  if (sDma3Requests[index].size != 0) {
    return -1;
  }

  return 0;
}
