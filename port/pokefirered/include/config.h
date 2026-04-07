#ifndef PFR_CONFIG_H
#define PFR_CONFIG_H

#ifndef NDEBUG
#define NDEBUG
#endif

#ifndef BUGFIX
#define BUGFIX
#endif
#ifndef UBFIX
#define UBFIX
#endif

#ifndef FIRERED
#define FIRERED
#endif
#ifndef ENGLISH
#define ENGLISH
#endif

#include "../../../include/constants/global.h"

#define GAME_VERSION VERSION_FIRE_RED

#define REVISION 0
#define CODE_ROOT "C:/WORK/POKeFRLG/src/pm_lgfr_ose/source/"
#define ABSPATH(x) (CODE_ROOT x)

#define UNITS_IMPERIAL

#define PRETTY_PRINT_OFF 0
#define PRETTY_PRINT_MINI_PRINTF 1
#define PRETTY_PRINT_LIBC 2
#define PRETTY_PRINT_HANDLER PRETTY_PRINT_OFF

#define LOG_HANDLER_AGB_PRINT 0
#define LOG_HANDLER_NOCASH_PRINT 1
#define LOG_HANDLER_MGBA_PRINT 2
#define LOG_HANDLER LOG_HANDLER_AGB_PRINT

#endif
