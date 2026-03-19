#include "gba/defines.h"
#include "task.h"

#include <stdint.h>
#include <string.h>

typedef struct PfrTaskSidecar
{
  uintptr_t ptr_values[NUM_TASK_DATA];
  TaskFunc callback_values[NUM_TASK_DATA];
  u16 ptr_mask;
  u16 callback_mask;
} PfrTaskSidecar;

COMMON_DATA struct Task gTasks[NUM_TASKS] = { 0 };

static PfrTaskSidecar sTaskSidecars[NUM_TASKS];

static void
pfr_clear_task_sidecar(u8 taskId)
{
  memset(&sTaskSidecars[taskId], 0, sizeof(sTaskSidecars[taskId]));
}

static void
InsertTask(u8 newTaskId);
static u8
FindFirstActiveTask(void);

void
ResetTasks(void)
{
  u8 i;

  for (i = 0; i < NUM_TASKS; i++) {
    gTasks[i].isActive = FALSE;
    gTasks[i].func = TaskDummy;
    gTasks[i].prev = i;
    gTasks[i].next = i + 1;
    gTasks[i].priority = (u8)-1;
    memset(gTasks[i].data, 0, sizeof(gTasks[i].data));
    pfr_clear_task_sidecar(i);
  }

  gTasks[0].prev = HEAD_SENTINEL;
  gTasks[NUM_TASKS - 1].next = TAIL_SENTINEL;
}

u8
CreateTask(TaskFunc func, u8 priority)
{
  u8 i;

  for (i = 0; i < NUM_TASKS; i++) {
    if (!gTasks[i].isActive) {
      gTasks[i].func = func;
      gTasks[i].priority = priority;
      InsertTask(i);
      memset(gTasks[i].data, 0, sizeof(gTasks[i].data));
      pfr_clear_task_sidecar(i);
      gTasks[i].isActive = TRUE;
      return i;
    }
  }

  return 0;
}

static void
InsertTask(u8 newTaskId)
{
  u8 taskId = FindFirstActiveTask();

  if (taskId == NUM_TASKS) {
    gTasks[newTaskId].prev = HEAD_SENTINEL;
    gTasks[newTaskId].next = TAIL_SENTINEL;
    return;
  }

  while (TRUE) {
    if (gTasks[newTaskId].priority < gTasks[taskId].priority) {
      gTasks[newTaskId].prev = gTasks[taskId].prev;
      gTasks[newTaskId].next = taskId;
      if (gTasks[taskId].prev != HEAD_SENTINEL) {
        gTasks[gTasks[taskId].prev].next = newTaskId;
      }

      gTasks[taskId].prev = newTaskId;
      return;
    }

    if (gTasks[taskId].next == TAIL_SENTINEL) {
      gTasks[newTaskId].prev = taskId;
      gTasks[newTaskId].next = gTasks[taskId].next;
      gTasks[taskId].next = newTaskId;
      return;
    }

    taskId = gTasks[taskId].next;
  }
}

void
DestroyTask(u8 taskId)
{
  if (gTasks[taskId].isActive) {
    gTasks[taskId].isActive = FALSE;

    if (gTasks[taskId].prev == HEAD_SENTINEL) {
      if (gTasks[taskId].next != TAIL_SENTINEL) {
        gTasks[gTasks[taskId].next].prev = HEAD_SENTINEL;
      }
    } else if (gTasks[taskId].next == TAIL_SENTINEL) {
      gTasks[gTasks[taskId].prev].next = TAIL_SENTINEL;
    } else {
      gTasks[gTasks[taskId].prev].next = gTasks[taskId].next;
      gTasks[gTasks[taskId].next].prev = gTasks[taskId].prev;
    }

    pfr_clear_task_sidecar(taskId);
  }
}

void
RunTasks(void)
{
  u8 taskId = FindFirstActiveTask();

  if (taskId != NUM_TASKS) {
    do {
      gTasks[taskId].func(taskId);
      taskId = gTasks[taskId].next;
    } while (taskId != TAIL_SENTINEL);
  }
}

static u8
FindFirstActiveTask(void)
{
  u8 taskId;

  for (taskId = 0; taskId < NUM_TASKS; taskId++) {
    if (gTasks[taskId].isActive == TRUE &&
        gTasks[taskId].prev == HEAD_SENTINEL) {
      break;
    }
  }

  return taskId;
}

void
TaskDummy(u8 taskId)
{
  (void)taskId;
}

void
SetTaskFuncWithFollowupFunc(u8 taskId, TaskFunc func, TaskFunc followupFunc)
{
  u8 followupFuncIndex = NUM_TASK_DATA - 2;

  PfrSetTaskCallback(taskId, followupFuncIndex, followupFunc);
  gTasks[taskId].func = func;
}

void
SwitchTaskToFollowupFunc(u8 taskId)
{
  u8 followupFuncIndex = NUM_TASK_DATA - 2;
  TaskFunc followupFunc = PfrGetTaskCallback(taskId, followupFuncIndex);

  gTasks[taskId].func = followupFunc != NULL ? followupFunc : TaskDummy;
}

bool8
FuncIsActiveTask(TaskFunc func)
{
  u8 i;

  for (i = 0; i < NUM_TASKS; i++) {
    if (gTasks[i].isActive == TRUE && gTasks[i].func == func) {
      return TRUE;
    }
  }

  return FALSE;
}

u8
FindTaskIdByFunc(TaskFunc func)
{
  s32 i;

  for (i = 0; i < NUM_TASKS; i++) {
    if (gTasks[i].isActive == TRUE && gTasks[i].func == func) {
      return (u8)i;
    }
  }

  return (u8)-1;
}

u8
GetTaskCount(void)
{
  u8 i;
  u8 count = 0;

  for (i = 0; i < NUM_TASKS; i++) {
    if (gTasks[i].isActive == TRUE) {
      count++;
    }
  }

  return count;
}

void
SetWordTaskArg(u8 taskId, u8 dataElem, unsigned long value)
{
  if (dataElem <= 14) {
    gTasks[taskId].data[dataElem] = (s16)value;
    gTasks[taskId].data[dataElem + 1] = (s16)(value >> 16);
  }
}

u32
GetWordTaskArg(u8 taskId, u8 dataElem)
{
  if (dataElem <= 14) {
    return (u16)gTasks[taskId].data[dataElem] |
           ((u32)(u16)gTasks[taskId].data[dataElem + 1] << 16);
  } else {
    return 0;
  }
}

void
PfrSetTaskPtr(u8 taskId, u8 dataElem, const void* value)
{
  if (taskId < NUM_TASKS && dataElem < NUM_TASK_DATA) {
    sTaskSidecars[taskId].ptr_values[dataElem] = (uintptr_t)value;
    sTaskSidecars[taskId].ptr_mask |= (u16)(1U << dataElem);
  }
}

void*
PfrGetTaskPtr(u8 taskId, u8 dataElem)
{
  if (taskId < NUM_TASKS && dataElem < NUM_TASK_DATA &&
      (sTaskSidecars[taskId].ptr_mask & (u16)(1U << dataElem)) != 0) {
    return (void*)sTaskSidecars[taskId].ptr_values[dataElem];
  }

  return NULL;
}

void
PfrSetTaskCallback(u8 taskId, u8 dataElem, TaskFunc func)
{
  if (taskId < NUM_TASKS && dataElem < NUM_TASK_DATA) {
    sTaskSidecars[taskId].callback_values[dataElem] = func;
    sTaskSidecars[taskId].callback_mask |= (u16)(1U << dataElem);
  }
}

TaskFunc
PfrGetTaskCallback(u8 taskId, u8 dataElem)
{
  if (taskId < NUM_TASKS && dataElem < NUM_TASK_DATA &&
      (sTaskSidecars[taskId].callback_mask & (u16)(1U << dataElem)) != 0) {
    return sTaskSidecars[taskId].callback_values[dataElem];
  }

  return NULL;
}
