#ifndef GUARD_GBA_DEFINES
#define GUARD_GBA_DEFINES

#include <stddef.h>
#include <stdint.h>

#include "gba/types.h"
#include "pfr/core.h"

#define TRUE 1
#define FALSE 0

#define MODERN 1

#define IWRAM_DATA
#define EWRAM_DATA
#define COMMON_DATA

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#define ALIGNED(n) __declspec(align(n))
#else
#define NOINLINE __attribute__((noinline))
#define ALIGNED(n) __attribute__((aligned(n)))
#endif

#define NAKED
#define UNUSED

struct SoundInfo;

#define SOUND_INFO_PTR (*(struct SoundInfo**)&gPfrSoundInfoPtr)
#define INTR_CHECK gPfrIntrCheck
#define INTR_VECTOR gPfrIntrVector

#define EWRAM_START ((u8*)gPfrEwram)
#define EWRAM_END (EWRAM_START + PFR_EWRAM_SIZE)
#define IWRAM_START ((u8*)gPfrIwram)
#define IWRAM_END (IWRAM_START + PFR_IWRAM_SIZE)

#define PLTT ((u8*)gPfrPltt)
#define BG_PLTT PLTT
#define BG_PLTT_SIZE 0x200
#define OBJ_PLTT (PLTT + BG_PLTT_SIZE)
#define OBJ_PLTT_SIZE 0x200
#define PLTT_SIZE (BG_PLTT_SIZE + OBJ_PLTT_SIZE)

#define VRAM ((u8*)gPfrVram)
#define VRAM_SIZE PFR_VRAM_SIZE

#define BG_VRAM VRAM
#define BG_VRAM_SIZE 0x10000
#define BG_CHAR_SIZE 0x4000
#define BG_SCREEN_SIZE 0x800
#define BG_CHAR_ADDR(n) (void*)(BG_VRAM + (BG_CHAR_SIZE * (n)))
#define BG_SCREEN_ADDR(n) (void*)(BG_VRAM + (BG_SCREEN_SIZE * (n)))
#define BG_TILE_ADDR(n) (void*)(BG_VRAM + (0x80 * (n)))

#define BG_TILE_H_FLIP(n) (0x400 + (n))
#define BG_TILE_V_FLIP(n) (0x800 + (n))
#define BG_TILE_H_V_FLIP(n) (0xC00 + (n))

#define OBJ_VRAM0 (void*)(VRAM + 0x10000)
#define OBJ_VRAM0_SIZE 0x8000

#define OBJ_VRAM1 (void*)(VRAM + 0x14000)
#define OBJ_VRAM1_SIZE 0x4000

#define OAM ((u8*)gPfrOam)
#define OAM_SIZE PFR_OAM_SIZE

#define ROM_HEADER_SIZE 0xC0

#define DISPLAY_WIDTH PFR_SCREEN_WIDTH
#define DISPLAY_HEIGHT PFR_SCREEN_HEIGHT

#define TILE_SIZE_4BPP 32
#define TILE_SIZE_8BPP 64

#define BG_TILE_ADDR_4BPP(n) (void*)(BG_VRAM + (TILE_SIZE_4BPP * (n)))

#define TILE_OFFSET_4BPP(n) ((n) * TILE_SIZE_4BPP)
#define TILE_OFFSET_8BPP(n) ((n) * TILE_SIZE_8BPP)

#define TOTAL_OBJ_TILE_COUNT 1024

#define PLTT_SIZEOF(n) ((n) * sizeof(u16))
#define PLTT_SIZE_4BPP PLTT_SIZEOF(16)
#define PLTT_SIZE_8BPP PLTT_SIZEOF(256)

#define PLTT_OFFSET_4BPP(n) ((n) * PLTT_SIZE_4BPP)

#endif
