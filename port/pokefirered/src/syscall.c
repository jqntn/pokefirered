#include <math.h>

#include "gba/defines.h"
#include "gba/flash_internal.h"
#include "gba/io_reg.h"
#include "gba/syscall.h"
#include <string.h>

#include "pfr/core.h"

u16 gFlashNumRemainingBytes = 0;
u16 (*ProgramFlashByte)(u16, u32, u8) = NULL;
u16 (*ProgramFlashSector)(u16, void*) = NULL;
u16 (*EraseFlashChip)(void) = NULL;
u16 (*EraseFlashSector)(u16) = NULL;
u16 (*WaitForFlashWrite)(u8, u8*, u8) = NULL;
const u16* gFlashMaxTime = NULL;
const struct FlashType* gFlash = NULL;
u8 (*PollFlashStatus)(u8*) = NULL;
u8 gFlashTimeoutFlag = 0;

const struct FlashSetupInfo DefaultFlash = {
  0,
  0,
  0,
  0,
  0,
  0,
  {
    FLASH_ROM_SIZE_1M,
    { 0x1000, 12, SECTORS_PER_BANK * 2, SECTORS_PER_BANK * 2 },
    { 0, 0 },
    { .joined = 0x1CC2 },
  },
};

static uint8_t*
pfr_flash_base(void)
{
  return gPfrRuntimeState.save;
}

static u32
pfr_comp_size(const uint8_t* src)
{
  return (u32)src[1] | ((u32)src[2] << 8) | ((u32)src[3] << 16);
}

static void
pfr_lz77_uncomp(const uint8_t* src, uint8_t* dest)
{
  u32 output_size = pfr_comp_size(src);
  u32 output_pos = 0;
  size_t input_pos = 4;

  while (output_pos < output_size) {
    uint8_t flags = src[input_pos++];
    int bit;

    for (bit = 7; bit >= 0 && output_pos < output_size; bit--) {
      if ((flags & (1U << bit)) == 0) {
        dest[output_pos++] = src[input_pos++];
      } else {
        u8 first = src[input_pos++];
        u8 second = src[input_pos++];
        u32 length = (u32)(first >> 4) + 3;
        u32 displacement = ((u32)(first & 0x0F) << 8) | second;
        u32 copy_pos;

        displacement++;
        copy_pos = output_pos - displacement;

        while (length-- != 0 && output_pos < output_size) {
          dest[output_pos++] = dest[copy_pos++];
        }
      }
    }
  }
}

static void
pfr_rl_uncomp(const uint8_t* src, uint8_t* dest)
{
  u32 output_size = pfr_comp_size(src);
  u32 output_pos = 0;
  size_t input_pos = 4;

  while (output_pos < output_size) {
    u8 flag = src[input_pos++];
    u32 length;

    if ((flag & 0x80) != 0) {
      u8 value = src[input_pos++];

      length = (flag & 0x7F) + 3;
      while (length-- != 0 && output_pos < output_size) {
        dest[output_pos++] = value;
      }
    } else {
      length = (flag & 0x7F) + 1;
      while (length-- != 0 && output_pos < output_size) {
        dest[output_pos++] = src[input_pos++];
      }
    }
  }
}

void
SoftReset(u32 resetFlags)
{
  RegisterRamReset(resetFlags);
  gPfrRuntimeState.frame_counter = 0;
  gPfrIntrCheck = 0;
}

void
RegisterRamReset(u32 resetFlags)
{
  if ((resetFlags & RESET_EWRAM) != 0) {
    memset(gPfrEwram, 0, PFR_EWRAM_SIZE);
  }

  if ((resetFlags & RESET_IWRAM) != 0) {
    memset(gPfrIwram, 0, PFR_IWRAM_SIZE);
  }

  if ((resetFlags & RESET_PALETTE) != 0) {
    memset(gPfrPltt, 0, PFR_PLTT_SIZE);
  }

  if ((resetFlags & RESET_VRAM) != 0) {
    memset(gPfrVram, 0, PFR_VRAM_SIZE);
  }

  if ((resetFlags & RESET_OAM) != 0) {
    memset(gPfrOam, 0, PFR_OAM_SIZE);
  }

  if ((resetFlags & RESET_REGS) != 0) {
    memset(gPfrIo, 0, PFR_IO_SIZE);
  }
}

void
VBlankIntrWait(void)
{
}

u16
Sqrt(u32 num)
{
  return (u16)sqrt((double)num);
}

u16
ArcTan2(s16 x, s16 y)
{
  const double radians = atan2((double)y, (double)x);
  double turns = radians / (6.28318530717958647692);

  if (turns < 0.0) {
    turns += 1.0;
  }

  return (u16)(turns * 65536.0);
}

void
CpuSet(const void* src, void* dest, u32 control)
{
  bool src_fixed = (control & CPU_SET_SRC_FIXED) != 0;
  bool copy_32 = (control & CPU_SET_32BIT) != 0;
  u32 count = control & 0x1FFFFF;

  if (copy_32) {
    const u32* src32 = (const u32*)src;
    u32* dest32 = (u32*)dest;
    u32 value = *src32;
    u32 i;

    for (i = 0; i < count; i++) {
      dest32[i] = src_fixed ? value : src32[i];
    }
  } else {
    const u16* src16 = (const u16*)src;
    u16* dest16 = (u16*)dest;
    u16 value = *src16;
    u32 i;

    for (i = 0; i < count; i++) {
      dest16[i] = src_fixed ? value : src16[i];
    }
  }
}

void
CpuFastSet(const void* src, void* dest, u32 control)
{
  bool src_fixed = (control & CPU_FAST_SET_SRC_FIXED) != 0;
  u32 count = control & 0x1FFFFF;
  const u32* src32 = (const u32*)src;
  u32* dest32 = (u32*)dest;
  u32 value = *src32;
  u32 i;

  for (i = 0; i < count; i++) {
    dest32[i] = src_fixed ? value : src32[i];
  }
}

void
BgAffineSet(struct BgAffineSrcData* src,
            struct BgAffineDstData* dest,
            s32 count)
{
  const double angle_scale = 6.28318530717958647692 / 256.0;
  s32 i;

  for (i = 0; i < count; i++) {
    double angle = src[i].alpha * angle_scale;
    double cos_value = cos(angle);
    double sin_value = sin(angle);
    double sx = src[i].sx / 256.0;
    double sy = src[i].sy / 256.0;

    dest[i].pa = (s16)(cos_value * sx * 256.0);
    dest[i].pb = (s16)(-sin_value * sx * 256.0);
    dest[i].pc = (s16)(sin_value * sy * 256.0);
    dest[i].pd = (s16)(cos_value * sy * 256.0);
    dest[i].dx =
      src[i].texX - dest[i].pa * src[i].scrX - dest[i].pb * src[i].scrY;
    dest[i].dy =
      src[i].texY - dest[i].pc * src[i].scrX - dest[i].pd * src[i].scrY;
  }
}

void
ObjAffineSet(struct ObjAffineSrcData* src, void* dest, s32 count, s32 offset)
{
  const double angle_scale = 6.28318530717958647692 / 256.0;
  int16_t* matrix = (int16_t*)dest;
  s32 i;

  for (i = 0; i < count; i++) {
    double angle = src[i].rotation * angle_scale;
    double cos_value = cos(angle);
    double sin_value = sin(angle);
    double sx = src[i].xScale / 256.0;
    double sy = src[i].yScale / 256.0;

    matrix[0] = (s16)(cos_value * sx * 256.0);
    matrix[offset] = (s16)(-sin_value * sx * 256.0);
    matrix[offset * 2] = (s16)(sin_value * sy * 256.0);
    matrix[offset * 3] = (s16)(cos_value * sy * 256.0);
    matrix += offset * 4;
  }
}

void
LZ77UnCompWram(const void* src, void* dest)
{
  pfr_lz77_uncomp((const uint8_t*)src, (uint8_t*)dest);
}

void
LZ77UnCompVram(const void* src, void* dest)
{
  pfr_lz77_uncomp((const uint8_t*)src, (uint8_t*)dest);
}

void
RLUnCompWram(const void* src, void* dest)
{
  pfr_rl_uncomp((const uint8_t*)src, (uint8_t*)dest);
}

void
RLUnCompVram(const void* src, void* dest)
{
  pfr_rl_uncomp((const uint8_t*)src, (uint8_t*)dest);
}

int
MultiBoot(struct MultiBootParam* mp)
{
  (void)mp;
  return -1;
}

s32
Div(s32 num, s32 denom)
{
  if (denom == 0) {
    return 0;
  }

  return num / denom;
}

void
SwitchFlashBank(u8 bankNum)
{
  (void)bankNum;
}

u16
ReadFlashId(void)
{
  return DefaultFlash.type.ids.joined;
}

void
StartFlashTimer(u8 phase)
{
  (void)phase;
}

void
SetReadFlash1(u16* dest)
{
  (void)dest;
}

void
StopFlashTimer(void)
{
}

u16
SetFlashTimerIntr(u8 timerNum, void (**intrFunc)(void))
{
  (void)timerNum;
  (void)intrFunc;
  return 0;
}

u32
ProgramFlashSectorAndVerify(u16 sectorNum, u8* src)
{
  return ProgramFlashSectorAndVerifyNBytes(sectorNum, src, 0x1000);
}

void
ReadFlash(u16 sectorNum, u32 offset, void* dest, u32 size)
{
  size_t base = (size_t)sectorNum * 0x1000U + offset;

  memcpy(dest, pfr_flash_base() + base, size);
}

u32
ProgramFlashSectorAndVerifyNBytes(u16 sectorNum, void* dataSrc, u32 n)
{
  size_t base = (size_t)sectorNum * 0x1000U;

  if (base + n > PFR_SAVE_SIZE) {
    return 1;
  }

  memcpy(pfr_flash_base() + base, dataSrc, n);
  gPfrRuntimeState.save_dirty = true;
  return 0;
}

u16
WaitForFlashWrite_Common(u8 phase, u8* addr, u8 lastData)
{
  (void)phase;
  (void)addr;
  (void)lastData;
  return 0;
}

u16
IdentifyFlash(void)
{
  gFlash = &DefaultFlash.type;
  return 0;
}
