/*
 * Port stub for pokemon.h
 *
 * Forward-declares Pokemon-related types that global.h uses inline for
 * SaveBlock struct definitions.  The full pokemon.h from the original repo
 * will be integrated later.
 */
#ifndef PFR_STUB_POKEMON_H
#define PFR_STUB_POKEMON_H

#include "constants/global.h"
#include "gba/types.h"
#include "sprite.h"

/* Minimal struct definitions needed by global.h's SaveBlock types. */

struct Pokemon
{
  u8 data[100]; /* placeholder — real size is 100 bytes */
};

struct BoxPokemon
{
  u8 data[80]; /* placeholder — real size is 80 bytes */
};

struct BattleTowerPokemon
{
  u8 data[36]; /* placeholder */
};

void DrawSpindaSpots(u16 species, u32 personality, u8 *dest, bool8 isFrontPic);

#endif /* PFR_STUB_POKEMON_H */
