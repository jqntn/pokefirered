#ifndef GUARD_GBA_ISAGBPRINT_H
#define GUARD_GBA_ISAGBPRINT_H

#include "gba/types.h"

#define MGBA_LOG_FATAL 0
#define MGBA_LOG_ERROR 1
#define MGBA_LOG_WARN 2
#define MGBA_LOG_INFO 3
#define MGBA_LOG_DEBUG 4

static inline bool32
MgbaOpen(void)
{
  return FALSE;
}

static inline void
MgbaClose(void)
{
}

static inline void
AGBPrintInit(void)
{
}

#define DebugPrintf(...)
#define DebugPrintfLevel(...)
#define DebugAssert(...)

#define AGB_ASSERT(exp) ((void)(exp))
#define AGB_WARNING(exp) ((void)(exp))
#define AGB_ASSERT_EX(exp, file, line) ((void)(exp))
#define AGB_WARNING_EX(exp, file, line) ((void)(exp))

#endif
