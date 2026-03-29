/*
 * Port stub for malloc.h
 *
 * Forwards to the original repo's malloc.h which only depends on global.h.
 */
#ifndef PFR_STUB_MALLOC_H
#define PFR_STUB_MALLOC_H

#include "../../../include/malloc.h"

typedef char pfr_heap_fits_in_ewram[(HEAP_SIZE <= PFR_EWRAM_SIZE) ? 1 : -1];

#define PFR_HEAP_OFFSET (PFR_EWRAM_SIZE - HEAP_SIZE)

static inline u8*
pfr_heap_base(void)
{
  return gPfrEwram + PFR_HEAP_OFFSET;
}

#define gHeap (pfr_heap_base())

#endif
