#ifndef GUARD_PFR_GBA_M4A_INTERNAL_H
#define GUARD_PFR_GBA_M4A_INTERNAL_H

#ifdef PFR_PORT
#undef PFR_PORT
#include "../../../../include/gba/m4a_internal.h"
#define PFR_PORT 1
#else
#include "../../../../include/gba/m4a_internal.h"
#endif

#endif
