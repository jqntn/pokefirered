#include "global.h"
#include "graphics.h"

#if defined(FIRERED)
const u16 gGraphics_TitleScreen_GameTitleLogoPals[] =
  INCBIN_U16("graphics/title_screen/firered/game_title_logo.gbapal");
const u8 gGraphics_TitleScreen_GameTitleLogoTiles[] =
  INCBIN_U8("graphics/title_screen/firered/game_title_logo.8bpp.lz");
const u8 gGraphics_TitleScreen_GameTitleLogoMap[] =
  INCBIN_U8("graphics/title_screen/firered/game_title_logo.bin.lz");
const u16 gGraphics_TitleScreen_BoxArtMonPals[] =
  INCBIN_U16("graphics/title_screen/firered/box_art_mon.gbapal");
const u8 gGraphics_TitleScreen_BoxArtMonTiles[] =
  INCBIN_U8("graphics/title_screen/firered/box_art_mon.4bpp.lz");
const u8 gGraphics_TitleScreen_BoxArtMonMap[] =
  INCBIN_U8("graphics/title_screen/firered/box_art_mon.bin.lz");
u16 gGraphics_TitleScreen_BackgroundPals[] =
  INCBIN_U16("graphics/title_screen/firered/background.gbapal");
const u16 gTitleScreen_Slash_Pal[] =
  INCBIN_U16("graphics/title_screen/firered/slash.gbapal");
#elif defined(LEAFGREEN)
const u16 gGraphics_TitleScreen_GameTitleLogoPals[] =
  INCBIN_U16("graphics/title_screen/leafgreen/game_title_logo.gbapal");
const u8 gGraphics_TitleScreen_GameTitleLogoTiles[] =
  INCBIN_U8("graphics/title_screen/leafgreen/game_title_logo.8bpp.lz");
const u8 gGraphics_TitleScreen_GameTitleLogoMap[] =
  INCBIN_U8("graphics/title_screen/leafgreen/game_title_logo.bin.lz");
const u16 gGraphics_TitleScreen_BoxArtMonPals[] =
  INCBIN_U16("graphics/title_screen/leafgreen/box_art_mon.gbapal");
const u8 gGraphics_TitleScreen_BoxArtMonTiles[] =
  INCBIN_U8("graphics/title_screen/leafgreen/box_art_mon.4bpp.lz");
const u8 gGraphics_TitleScreen_BoxArtMonMap[] =
  INCBIN_U8("graphics/title_screen/leafgreen/box_art_mon.bin.lz");
u16 gGraphics_TitleScreen_BackgroundPals[] =
  INCBIN_U16("graphics/title_screen/leafgreen/background.gbapal");
const u16 gTitleScreen_Slash_Pal[] =
  INCBIN_U16("graphics/title_screen/leafgreen/slash.gbapal");
#endif

const u8 gGraphics_TitleScreen_CopyrightPressStartTiles[] =
  INCBIN_U8("graphics/title_screen/copyright_press_start.4bpp.lz");
const u8 gGraphics_TitleScreen_CopyrightPressStartMap[] =
  INCBIN_U8("graphics/title_screen/copyright_press_start.bin.lz");
const u32 gTitleScreen_BlankSprite_Tiles[] =
  INCBIN_U32("graphics/title_screen/blank_sprite.4bpp.lz");
const u16 gMenuMessageWindow_Gfx[16] = { 0 };
const u8 gHelpMessageWindow_Gfx[32] = { 0 };
