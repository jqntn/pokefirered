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
#include "random.h"

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

int
main(void)
{
  test_random();
  test_malloc_alloc_free();
  puts("pfr_integration: ok");
  return 0;
}
