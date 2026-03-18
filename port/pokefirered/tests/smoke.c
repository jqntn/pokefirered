#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gba/io_reg.h"
#include "gba/syscall.h"
#include "pfr/audio.h"
#include "pfr/core.h"
#include "pfr/storage.h"
#include "task.h"

static int sTaskLog[8];
static size_t sTaskLogCount;
static int sFollowupState;

static void
test_task_priority_high(u8 taskId)
{
  sTaskLog[sTaskLogCount++] = 1;
  DestroyTask(taskId);
}

static void
test_task_priority_mid(u8 taskId)
{
  sTaskLog[sTaskLogCount++] = 2;
  DestroyTask(taskId);
}

static void
test_task_priority_low(u8 taskId)
{
  sTaskLog[sTaskLogCount++] = 3;
  DestroyTask(taskId);
}

static void
test_task_followup(u8 taskId)
{
  sFollowupState = 2;
  DestroyTask(taskId);
}

static void
test_task_switcher(u8 taskId)
{
  sFollowupState = 1;
  SwitchTaskToFollowupFunc(taskId);
}

static void
test_cpuset(void)
{
  u16 src16[4] = { 1, 2, 3, 4 };
  u16 dst16[4] = { 0, 0, 0, 0 };
  u32 src32[4] = { 10, 20, 30, 40 };
  u32 dst32[4] = { 0, 0, 0, 0 };
  u16 fill16 = 0x1234;
  u32 fill32 = 0x11223344;

  CpuSet(src16, dst16, CPU_SET_16BIT | 4);
  assert(memcmp(src16, dst16, sizeof(src16)) == 0);

  CpuSet(&fill16, dst16, CPU_SET_16BIT | CPU_SET_SRC_FIXED | 4);
  assert(dst16[0] == 0x1234 && dst16[3] == 0x1234);

  CpuFastSet(src32, dst32, 4);
  assert(memcmp(src32, dst32, sizeof(src32)) == 0);

  CpuFastSet(&fill32, dst32, CPU_FAST_SET_SRC_FIXED | 4);
  assert(dst32[0] == 0x11223344 && dst32[3] == 0x11223344);
}

static void
test_decompression(void)
{
  const u8 lz_data[] = { 0x10, 0x04, 0x00, 0x00, 0x00, 1, 2, 3, 4 };
  const u8 rl_data[] = { 0x30, 0x06, 0x00, 0x00, 0x83, 0xAB };
  u8 lz_out[4] = { 0 };
  u8 rl_out[6] = { 0 };
  size_t i;

  LZ77UnCompWram(lz_data, lz_out);
  assert(memcmp(lz_out, (u8[]){ 1, 2, 3, 4 }, sizeof(lz_out)) == 0);

  RLUnCompWram(rl_data, rl_out);
  for (i = 0; i < sizeof(rl_out); i++) {
    assert(rl_out[i] == 0xAB);
  }
}

static void
test_storage_roundtrip(void)
{
  const char* path = "pfr_smoke.sav";
  u8 buffer[64];
  u8 loaded[64];
  size_t i;

  for (i = 0; i < sizeof(buffer); i++) {
    buffer[i] = (u8)(i * 3);
  }

  remove(path);
  assert(pfr_storage_save(path, buffer, sizeof(buffer)));
  memset(loaded, 0, sizeof(loaded));
  assert(pfr_storage_load(path, loaded, sizeof(loaded)));
  assert(memcmp(buffer, loaded, sizeof(buffer)) == 0);
  remove(path);
}

static void
test_tasks(void)
{
  u8 taskId;
  int marker = 1234;

  ResetTasks();
  sTaskLogCount = 0;
  CreateTask(test_task_priority_low, 10);
  CreateTask(test_task_priority_high, 1);
  CreateTask(test_task_priority_mid, 5);
  RunTasks();
  assert(sTaskLogCount == 3);
  assert(sTaskLog[0] == 1);
  assert(sTaskLog[1] == 2);
  assert(sTaskLog[2] == 3);
  assert(GetTaskCount() == 0);

  taskId = CreateTask(TaskDummy, 2);
  PfrSetTaskPtr(taskId, 0, &marker);
  assert(PfrGetTaskPtr(taskId, 0) == &marker);

  PfrSetTaskCallback(taskId, 1, test_task_followup);
  assert(PfrGetTaskCallback(taskId, 1) == test_task_followup);

  sFollowupState = 0;
  SetTaskFuncWithFollowupFunc(taskId, test_task_switcher, test_task_followup);
  RunTasks();
  assert(sFollowupState == 1);
  RunTasks();
  assert(sFollowupState == 2);
  assert(GetTaskCount() == 0);

  taskId = CreateTask(TaskDummy, 0);
  SetWordTaskArg(taskId, 4, 0x11223344UL);
  assert(GetWordTaskArg(taskId, 4) == 0x11223344UL);
  DestroyTask(taskId);
}

static void
test_core_and_audio(void)
{
  PfrAudioState audio_state;
  int16_t samples[256] = { 0 };

  pfr_core_init("pfr_smoke_core.sav");
  pfr_core_set_keys(A_BUTTON | DPAD_RIGHT);
  pfr_core_run_frame();
  assert(pfr_core_frame_checksum() != 0);
  assert(gPfrRuntimeState.keys_held == (A_BUTTON | DPAD_RIGHT));

  gPfrRuntimeState.save[0] = 42;
  gPfrRuntimeState.save_dirty = true;
  pfr_core_flush_save();
  pfr_core_shutdown();

  pfr_audio_reset(&audio_state);
  pfr_audio_generate(
    &audio_state, samples, PFR_ARRAY_COUNT(samples), A_BUTTON, 1);
  assert(pfr_audio_has_signal(samples, PFR_ARRAY_COUNT(samples)));
  remove("pfr_smoke_core.sav");
}

int
main(void)
{
  test_cpuset();
  test_decompression();
  test_storage_roundtrip();
  test_tasks();
  test_core_and_audio();
  puts("pfr_smoke: ok");
  return 0;
}
