/*
 * Integration test — verifies that original game source files link correctly
 * and produce expected results when called through the port shim headers.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "malloc.h"
#include "pfr/main_runtime.h"
#include "random.h"
#include "scanline_effect.h"
#include "task.h"
#include "trig.h"

static void
test_random(void)
{
  u16 a;
  u16 b;

  SeedRng(42);
  a = Random();
  b = Random();
  assert(a != 0 || b != 0);
  assert(a != b);

  SeedRng(42);
  assert(Random() == a);
  assert(Random() == b);
}

/* Provide the gHeap symbol expected by malloc.h */
static u8 sTestHeap[0x4000];

static void
test_malloc_alloc_free(void)
{
  void* p1;
  void* p2;

  InitHeap(sTestHeap, sizeof(sTestHeap));

  p1 = Alloc(128);
  assert(p1 != NULL);

  p2 = AllocZeroed(64);
  assert(p2 != NULL);
  assert(p1 != p2);

  /* AllocZeroed should produce zeros. */
  {
    u8* bytes = (u8*)p2;
    size_t i;

    for (i = 0; i < 64; i++) {
      assert(bytes[i] == 0);
    }
  }

  Free(p2);
  Free(p1);
}

#include "string_util.h"

static void
test_string_util(void)
{
  u8 buffer[32];
  const u8 testString[] = { 0xBB, 0xCC, 0xDD, 0xFF }; /* "Poké" */

  assert(StringLength(testString) == 3);

  StringCopy(buffer, testString);
  assert(buffer[0] == 0xBB);
  assert(buffer[3] == 0xFF);
}

static void
test_trig(void)
{
  assert(gSineTable[0] == 0);
  assert(gSineTable[64] == Q_8_8(1));
  assert(gSineTable[192] == Q_8_8(-1));
  assert(Sin(64, 256) == 256);
  assert(Cos(0, 256) == 256);
  assert(Sin2(90) == Q_4_12(1));
  assert(Cos2(180) == Q_4_12(-1));
}

static void
test_scanline_effect(void)
{
  struct ScanlineEffectParams params;
  u8 waveTaskId;

  memset(gPfrIo, 0, PFR_IO_SIZE);
  pfr_main_init();
  ResetTasks();
  ScanlineEffect_Clear();

  gScanlineEffectRegBuffers[0][0] = 10;
  gScanlineEffectRegBuffers[0][1] = 20;
  gScanlineEffectRegBuffers[0][2] = 30;

  params.dmaDest = (void*)REG_ADDR_BG1HOFS;
  params.dmaControl = (u32)SCANLINE_EFFECT_DMACNT_16BIT;
  params.initState = 1;
  params.unused9 = 0;
  ScanlineEffect_SetParams(params);
  ScanlineEffect_InitHBlankDmaTransfer();

  assert(REG_BG1HOFS == 10);

  REG_VCOUNT = 0;
  pfr_main_on_hblank();
  assert(REG_BG1HOFS == 20);

  REG_VCOUNT = 1;
  pfr_main_on_hblank();
  assert(REG_BG1HOFS == 30);

  REG_VCOUNT = DISPLAY_HEIGHT;
  pfr_main_on_hblank();
  assert(REG_BG1HOFS == 30);

  waveTaskId =
    ScanlineEffect_InitWave(5, 9, 4, 32, 0, SCANLINE_EFFECT_REG_BG1HOFS, FALSE);
  assert(waveTaskId != TASK_NONE);
  assert(gScanlineEffect.waveTaskId == waveTaskId);
  assert(gTasks[waveTaskId].isActive == TRUE);

  ScanlineEffect_Stop();
  assert(gScanlineEffect.state == 0);
  assert(gScanlineEffect.waveTaskId == 0xFF);
  assert(gTasks[waveTaskId].isActive == FALSE);

  pfr_main_shutdown();
}

int
main(void)
{
  test_random();
  test_malloc_alloc_free();
  test_string_util();
  test_trig();
  test_scanline_effect();
  puts("pfr_integration: ok");
  return 0;
}
