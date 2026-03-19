#include "gpu_regs.h"
#include <stdbool.h>

enum
{
  PFR_GPU_REG_BUF_SIZE = 0x60,
  PFR_EMPTY_SLOT = 0xFF,
};

#define GPU_REG_BUF(offset) (*(u16*)(&sGpuRegBuffer[offset]))
#define GPU_REG(offset) (*(vu16*)(gPfrIo + (offset)))

static u8 sGpuRegBuffer[PFR_GPU_REG_BUF_SIZE];
static u8 sGpuRegWaitingList[PFR_GPU_REG_BUF_SIZE];
static volatile bool8 sGpuRegBufferLocked;
static volatile bool8 sShouldSyncRegIE;
static vu16 sRegIE;

static void
CopyBufferedValueToGpuReg(u8 regOffset)
{
  if (regOffset == REG_OFFSET_DISPSTAT) {
    REG_DISPSTAT &= (u16) ~(DISPSTAT_HBLANK_INTR | DISPSTAT_VBLANK_INTR);
    REG_DISPSTAT |= GPU_REG_BUF(REG_OFFSET_DISPSTAT);
  } else {
    GPU_REG(regOffset) = GPU_REG_BUF(regOffset);
  }
}

static void
SyncRegIE(void)
{
  if (sShouldSyncRegIE) {
    u16 ime = REG_IME;

    REG_IME = 0;
    REG_IE = sRegIE;
    REG_IME = ime;
    sShouldSyncRegIE = FALSE;
  }
}

static void
UpdateRegDispstatIntrBits(u16 regIE)
{
  u16 oldValue = (u16)(GetGpuReg(REG_OFFSET_DISPSTAT) &
                       (DISPSTAT_HBLANK_INTR | DISPSTAT_VBLANK_INTR));
  u16 newValue = 0;

  if ((regIE & INTR_FLAG_VBLANK) != 0) {
    newValue |= DISPSTAT_VBLANK_INTR;
  }

  if ((regIE & INTR_FLAG_HBLANK) != 0) {
    newValue |= DISPSTAT_HBLANK_INTR;
  }

  if (oldValue != newValue) {
    SetGpuReg(REG_OFFSET_DISPSTAT, newValue);
  }
}

void
InitGpuRegManager(void)
{
  u8 i;

  for (i = 0; i < PFR_GPU_REG_BUF_SIZE; i++) {
    sGpuRegBuffer[i] = 0;
    sGpuRegWaitingList[i] = PFR_EMPTY_SLOT;
  }

  sGpuRegBufferLocked = FALSE;
  sShouldSyncRegIE = FALSE;
  sRegIE = 0;
}

void
CopyBufferedValuesToGpuRegs(void)
{
  u8 i;

  if (sGpuRegBufferLocked) {
    return;
  }

  for (i = 0; i < PFR_GPU_REG_BUF_SIZE; i++) {
    u8 regOffset = sGpuRegWaitingList[i];

    if (regOffset == PFR_EMPTY_SLOT) {
      return;
    }

    CopyBufferedValueToGpuReg(regOffset);
    sGpuRegWaitingList[i] = PFR_EMPTY_SLOT;
  }
}

void
SetGpuReg(u8 regOffset, u16 value)
{
  if (regOffset >= PFR_GPU_REG_BUF_SIZE) {
    return;
  }

  GPU_REG_BUF(regOffset) = value;

  if ((REG_VCOUNT >= DISPLAY_HEIGHT + 1 && REG_VCOUNT <= 225) ||
      (REG_DISPCNT & DISPCNT_FORCED_BLANK) != 0) {
    CopyBufferedValueToGpuReg(regOffset);
  } else {
    u8 i;

    sGpuRegBufferLocked = TRUE;

    for (i = 0;
         i < PFR_GPU_REG_BUF_SIZE && sGpuRegWaitingList[i] != PFR_EMPTY_SLOT;
         i++) {
      if (sGpuRegWaitingList[i] == regOffset) {
        sGpuRegBufferLocked = FALSE;
        return;
      }
    }

    sGpuRegWaitingList[i] = regOffset;
    sGpuRegBufferLocked = FALSE;
  }
}

void
SetGpuReg_ForcedBlank(u8 regOffset, u16 value)
{
  if (regOffset >= PFR_GPU_REG_BUF_SIZE) {
    return;
  }

  GPU_REG_BUF(regOffset) = value;
  CopyBufferedValueToGpuReg(regOffset);
}

u16
GetGpuReg(u8 regOffset)
{
  if (regOffset == REG_OFFSET_DISPSTAT) {
    return REG_DISPSTAT;
  }

  if (regOffset == REG_OFFSET_VCOUNT) {
    return REG_VCOUNT;
  }

  return GPU_REG_BUF(regOffset);
}

void
SetGpuRegBits(u8 regOffset, u16 mask)
{
  SetGpuReg(regOffset, (u16)(GPU_REG_BUF(regOffset) | mask));
}

void
ClearGpuRegBits(u8 regOffset, u16 mask)
{
  SetGpuReg(regOffset, (u16)(GPU_REG_BUF(regOffset) & (u16)~mask));
}

void
EnableInterrupts(u16 mask)
{
  sRegIE |= mask;
  sShouldSyncRegIE = TRUE;
  SyncRegIE();
  UpdateRegDispstatIntrBits(sRegIE);
}

void
DisableInterrupts(u16 mask)
{
  sRegIE &= (u16)~mask;
  sShouldSyncRegIE = TRUE;
  SyncRegIE();
  UpdateRegDispstatIntrBits(sRegIE);
}
