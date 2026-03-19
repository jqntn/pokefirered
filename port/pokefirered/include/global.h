/*
 * Port-compatible global.h
 *
 * This replaces the original include/global.h for the port build.
 * It includes:
 *   1. The port's config.h (which defines NDEBUG, FIRERED, ENGLISH, etc.)
 *   2. The port's GBA shim headers (gba/gba.h)
 *   3. The original repo's constants headers (via relative paths)
 *   4. MSVC-compatible attribute macros
 *   5. INCBIN_* / _() / __() stubs
 *   6. All the macros and type definitions from the original global.h
 */
#ifndef GUARD_GLOBAL_H
#define GUARD_GLOBAL_H

#include "config.h"
#include "gba/gba.h"
#include "pfr/stubs.h"

#include <string.h>

/* ---- Original repo constants ------------------------------------------- */
/* constants/global.h is already included by config.h, so skip it here.     */
#include "../../../include/constants/easy_chat.h"
#include "../../../include/constants/flags.h"
#include "../../../include/constants/pokedex.h"
#include "../../../include/constants/rgb.h"
#include "../../../include/constants/species.h"
#include "../../../include/constants/vars.h"

/* ---- Compiler-compat --------------------------------------------------- */

/* Prevent cross-jump optimization — no-op on the port. */
#define BLOCK_CROSS_JUMP

/* Decompilation helpers — no-ops on the port. */
#define asm_comment(x)
#define asm_unified(x)

#if defined(_MSC_VER) && __STDC_VERSION__ < 202311L
/* MSVC doesn't support GNU asm keyword in C mode. */
#define asm __noop
#endif

/* ---- INCBIN / preproc stubs -------------------------------------------- */
#define _(x) (x)
#define __(x) (x)
#define INCBIN(...) { 0 }
#define INCBIN_U8 INCBIN
#define INCBIN_U16 INCBIN
#define INCBIN_U32 INCBIN
#define INCBIN_S8 INCBIN
#define INCBIN_S16 INCBIN
#define INCBIN_S32 INCBIN

/* ---- __attribute__ compat for MSVC ------------------------------------- */
#if defined(_MSC_VER)
#define __attribute__(x)
/* Allow zero-length arrays (MSVC extension warning suppressed elsewhere).  */
#pragma warning(disable : 4200) /* zero-sized array in struct */
#pragma warning(disable : 4201) /* nameless struct/union */
#pragma warning(disable : 4214) /* bit field types other than int */
#endif

/* ---- Utility macros (from original global.h) --------------------------- */
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define NELEMS(array) ARRAY_COUNT(array)

#define SWAP(a, b, temp)                                                       \
  {                                                                            \
    temp = a;                                                                  \
    a = b;                                                                     \
    b = temp;                                                                  \
  }

/* Fixed-point conversions */
#define Q_8_8(n) ((s16)((n) * 256))
#define Q_8_8_TO_INT(n) ((s16)((n) >> 8))
#define Q_4_12(n) ((s16)((n) * 4096))
#define Q_4_12_TO_INT(n) ((s16)((n) >> 12))
#define Q_N_S(s, n) ((s16)((n) * (1 << (s))))
#define Q_N_S_TO_INT(s, n) ((s16)((n) >> (s)))
#define Q_24_8(n) ((s32)((n) << 8))
#define Q_24_8_TO_INT(n) ((s32)((n) >> 8))

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) >= (b) ? (a) : (b))

#if MODERN
#define abs(x) (((x) < 0) ? -(x) : (x))
#endif

#ifdef UBFIX
#define SAFE_DIV(a, b) ((b) ? (a) / (b) : 0)
#else
#define SAFE_DIV(a, b) ((a) / (b))
#endif

#define HIHALF(n) (((n) & 0xFFFF0000) >> 16)
#define LOHALF(n) ((n) & 0xFFFF)

/* Byte read macros */
#define T1_READ_8(ptr) ((ptr)[0])
#define T1_READ_16(ptr) ((ptr)[0] | ((ptr)[1] << 8))
#define T1_READ_32(ptr)                                                        \
  ((ptr)[0] | ((ptr)[1] << 8) | ((ptr)[2] << 16) | ((ptr)[3] << 24))
#define T1_READ_PTR(ptr) (u8*)T1_READ_32(ptr)

#define T2_READ_8(ptr) ((ptr)[0])
#define T2_READ_16(ptr) ((ptr)[0] + ((ptr)[1] << 8))
#define T2_READ_32(ptr)                                                        \
  ((ptr)[0] + ((ptr)[1] << 8) + ((ptr)[2] << 16) + ((ptr)[3] << 24))
#define T2_READ_PTR(ptr) (void*)T2_READ_32(ptr)

/*
 * TEST_BUTTON — the original uses a GCC statement expression ({ ... }).
 * MSVC does not support this, so use a simple bitwise AND instead.
 */
#define TEST_BUTTON(field, button) ((field) & (button))
#define JOY_NEW(button) TEST_BUTTON(gMain.newKeys, button)
#define JOY_HELD(button) TEST_BUTTON(gMain.heldKeys, button)
#define JOY_HELD_RAW(button) TEST_BUTTON(gMain.heldKeysRaw, button)
#define JOY_REPT(button) TEST_BUTTON(gMain.newAndRepeatedKeys, button)

extern u8 gStringVar1[];
extern u8 gStringVar2[];
extern u8 gStringVar3[];
extern u8 gStringVar4[];

#define DIV_ROUND_UP(val, roundBy)                                             \
  (((val) / (roundBy)) + (((val) % (roundBy)) ? 1 : 0))

#define ROUND_BITS_TO_BYTES(numBits) DIV_ROUND_UP(numBits, 8)

#define DEX_FLAGS_NO ROUND_BITS_TO_BYTES(NUM_SPECIES)
#define NUM_FLAG_BYTES ROUND_BITS_TO_BYTES(FLAGS_COUNT)
#define NUM_ADDITIONAL_PHRASE_BYTES ROUND_BITS_TO_BYTES(NUM_ADDITIONAL_PHRASES)

/* Variadic argument counting */
#define NARG_8(...) NARG_8_(_, ##__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define NARG_8_(_, a, b, c, d, e, f, g, h, N, ...) N

#define CAT(a, b) CAT_(a, b)
#define CAT_(a, b) a##b

/* Compile-time assert */
#define STATIC_ASSERT(expr, id) typedef char id[(expr) ? 1 : -1];

/* ---- Coordinate structs ------------------------------------------------ */
struct Coords8
{
  s8 x;
  s8 y;
};

struct UCoords8
{
  u8 x;
  u8 y;
};

struct Coords16
{
  s16 x;
  s16 y;
};

struct UCoords16
{
  u16 x;
  u16 y;
};

struct Coords32
{
  s32 x;
  s32 y;
};

struct UCoords32
{
  u32 x;
  u32 y;
};

struct Time
{
  s16 days;
  s8 hours;
  s8 minutes;
  s8 seconds;
};

/* ---- Forward declarations for heavy game types ------------------------- */
/*
 * The original global.h defines SaveBlock1, SaveBlock2, and many other huge
 * structs inline.  For now the port forward-declares them and provides the
 * extern pointers that game code expects.  Full definitions will be added
 * as more game files are integrated.
 */

/* Minimal padded structs for compilation */
struct SaveBlock1
{
  u8 _pad_0[0x9FC];
  u8 rivalName[PLAYER_NAME_LENGTH];
  u8 _pad_A04[0x3DE0];
};

struct SaveBlock2
{
  u8 playerName[PLAYER_NAME_LENGTH + 1];
  u8 playerGender;
  u8 specialSaveWarpFlags;
  u8 playerTrainerId[TRAINER_ID_LENGTH];
  u16 playTimeHours;
  u8 playTimeMinutes;
  u8 playTimeSeconds;
  u8 playTimeVBlanks;
  u8 optionsButtonMode;
  u16 optionsTextSpeed : 3;
  u16 optionsWindowFrameType : 5;
  u16 optionsSound : 1;
  u16 optionsBattleStyle : 1;
  u16 optionsBattleSceneOff : 1;
  u16 regionMapZoom : 1;
  u8 _pad_16[0xF0E];
};

extern struct SaveBlock1* gSaveBlock1Ptr;
extern struct SaveBlock2* gSaveBlock2Ptr;

#include "main.h"

/* ---- Additional constant headers that original global.h pulls in ------- */
#include "../../../include/constants/game_stat.h"

/* ---- Stub includes for headers that the original global.h chains to ---- */
/* These are lightweight forwarding stubs in the port's include directory.   */
#include "fame_checker.h"
#include "global.berry.h"
#include "global.fieldmap.h"
#include "pokemon.h"

#endif /* GUARD_GLOBAL_H */
