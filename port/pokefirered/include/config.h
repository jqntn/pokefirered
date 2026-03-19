/*
 * Port-compatible config.h
 *
 * The original config.h circularly includes global.h.  The port breaks this
 * cycle by defining the needed macros directly and NOT including global.h from
 * here.  global.h will include this file instead of the original config.h.
 */
#ifndef PFR_CONFIG_H
#define PFR_CONFIG_H

/* ---- Build-mode flags -------------------------------------------------- */
/* The port always compiles with NDEBUG so that AGB asserts become no-ops
   (the port's isagbprint.h already stubs them). */
#ifndef NDEBUG
#define NDEBUG
#endif

/* MODERN is already defined in gba/defines.h (set to 1). */

/* MODERN implies BUGFIX and UBFIX */
#ifndef BUGFIX
#define BUGFIX
#endif
#ifndef UBFIX
#define UBFIX
#endif

/* ---- Game version ------------------------------------------------------ */
#ifndef FIRERED
#define FIRERED
#endif
#ifndef ENGLISH
#define ENGLISH
#endif

#include "../../../include/constants/global.h" /* VERSION_FIRE_RED, LANGUAGE_ENGLISH, etc. */

#define GAME_VERSION VERSION_FIRE_RED

#define REVISION 0
#define CODE_ROOT "C:/WORK/POKeFRLG/src/pm_lgfr_ose/source/"
#define ABSPATH(x) (CODE_ROOT x)

#define UNITS_IMPERIAL

/* ---- Log handler (no-ops, but keep defines so isagbprint.h compiles) --- */
#define PRETTY_PRINT_OFF 0
#define PRETTY_PRINT_MINI_PRINTF 1
#define PRETTY_PRINT_LIBC 2
#define PRETTY_PRINT_HANDLER PRETTY_PRINT_OFF

#define LOG_HANDLER_AGB_PRINT 0
#define LOG_HANDLER_NOCASH_PRINT 1
#define LOG_HANDLER_MGBA_PRINT 2
#define LOG_HANDLER LOG_HANDLER_AGB_PRINT

#endif /* PFR_CONFIG_H */
