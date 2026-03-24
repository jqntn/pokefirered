#include "global.h"

#include "menu.h"
#include "new_menu_helpers.h"
#include "text.h"

static const struct FontInfo sPfrFontInfos[] = {
  [FONT_SMALL] = {
    .fontFunction = FontFunc_Small,
    .maxLetterWidth = 8,
    .maxLetterHeight = 13,
    .letterSpacing = 0,
    .lineSpacing = 0,
    .fgColor = 2,
    .bgColor = 1,
    .shadowColor = 3,
  },
  [FONT_NORMAL_COPY_1] = {
    .fontFunction = FontFunc_NormalCopy1,
    .maxLetterWidth = 8,
    .maxLetterHeight = 14,
    .letterSpacing = 0,
    .lineSpacing = 0,
    .fgColor = 2,
    .bgColor = 1,
    .shadowColor = 3,
  },
  [FONT_NORMAL] = {
    .fontFunction = FontFunc_Normal,
    .maxLetterWidth = 10,
    .maxLetterHeight = 14,
    .letterSpacing = 1,
    .lineSpacing = 0,
    .fgColor = 2,
    .bgColor = 1,
    .shadowColor = 3,
  },
  [FONT_NORMAL_COPY_2] = {
    .fontFunction = FontFunc_NormalCopy2,
    .maxLetterWidth = 10,
    .maxLetterHeight = 14,
    .letterSpacing = 1,
    .lineSpacing = 0,
    .fgColor = 2,
    .bgColor = 1,
    .shadowColor = 3,
  },
  [FONT_MALE] = {
    .fontFunction = FontFunc_Male,
    .maxLetterWidth = 10,
    .maxLetterHeight = 14,
    .letterSpacing = 0,
    .lineSpacing = 0,
    .fgColor = 2,
    .bgColor = 1,
    .shadowColor = 3,
  },
  [FONT_FEMALE] = {
    .fontFunction = FontFunc_Female,
    .maxLetterWidth = 10,
    .maxLetterHeight = 14,
    .letterSpacing = 0,
    .lineSpacing = 0,
    .fgColor = 2,
    .bgColor = 1,
    .shadowColor = 3,
  },
  [FONT_BRAILLE] = {
    .fontFunction = FontFunc_Braille,
    .maxLetterWidth = 8,
    .maxLetterHeight = 16,
    .letterSpacing = 0,
    .lineSpacing = 2,
    .fgColor = 2,
    .bgColor = 1,
    .shadowColor = 3,
  },
  [FONT_BOLD] = {
    .fontFunction = NULL,
    .maxLetterWidth = 8,
    .maxLetterHeight = 8,
    .letterSpacing = 0,
    .lineSpacing = 0,
    .fgColor = 1,
    .bgColor = 2,
    .shadowColor = 15,
  },
};

static const u8 sMenuCursorDimensions[][2] = {
  [FONT_SMALL] = { 8, 13 },   [FONT_NORMAL_COPY_1] = { 8, 14 },
  [FONT_NORMAL] = { 8, 14 },  [FONT_NORMAL_COPY_2] = { 8, 14 },
  [FONT_MALE] = { 8, 14 },    [FONT_FEMALE] = { 8, 14 },
  [FONT_BRAILLE] = { 8, 16 }, [FONT_BOLD] = { 8, 8 },
};

const u16 gStandardMenuPalette[] = { 0 };

u16
FontFunc_Braille(struct TextPrinter* textPrinter)
{
  return FontFunc_Normal(textPrinter);
}

s32
GetGlyphWidth_Braille(u16 fontType, bool32 isJapanese)
{
  (void)fontType;
  (void)isJapanese;
  return 8;
}

void
SetDefaultFontsPointer(void)
{
  SetFontsPointer(sPfrFontInfos);
}

u8
GetFontAttribute(u8 fontId, u8 attributeId)
{
  switch (attributeId) {
    case FONTATTR_MAX_LETTER_WIDTH:
      return sPfrFontInfos[fontId].maxLetterWidth;
    case FONTATTR_MAX_LETTER_HEIGHT:
      return sPfrFontInfos[fontId].maxLetterHeight;
    case FONTATTR_LETTER_SPACING:
      return sPfrFontInfos[fontId].letterSpacing;
    case FONTATTR_LINE_SPACING:
      return sPfrFontInfos[fontId].lineSpacing;
    case FONTATTR_UNKNOWN:
      return sPfrFontInfos[fontId].unk;
    case FONTATTR_COLOR_FOREGROUND:
      return sPfrFontInfos[fontId].fgColor;
    case FONTATTR_COLOR_BACKGROUND:
      return sPfrFontInfos[fontId].bgColor;
    case FONTATTR_COLOR_SHADOW:
      return sPfrFontInfos[fontId].shadowColor;
    default:
      return 0;
  }
}

u8
GetMenuCursorDimensionByFont(u8 fontId, u8 whichDimension)
{
  return sMenuCursorDimensions[fontId][whichDimension];
}

void
AddTextPrinterParameterized3(u8 windowId,
                             u8 fontId,
                             u8 x,
                             u8 y,
                             const u8* color,
                             s8 speed,
                             const u8* str)
{
  struct TextPrinterTemplate printer;

  printer.currentChar = str;
  printer.windowId = windowId;
  printer.fontId = fontId;
  printer.x = x;
  printer.y = y;
  printer.currentX = x;
  printer.currentY = y;
  printer.letterSpacing = GetFontAttribute(fontId, FONTATTR_LETTER_SPACING);
  printer.lineSpacing = GetFontAttribute(fontId, FONTATTR_LINE_SPACING);
  printer.unk = 0;
  printer.fgColor = color[1];
  printer.bgColor = color[0];
  printer.shadowColor = color[2];

  AddTextPrinter(&printer, (u8)speed, NULL);
}
