file(READ "${PFR_SOURCE}" pfr_text_printer_source)

string(REPLACE
       "static void CopyGlyphToWindow_Parameterized(void *tileData, u16 currentX, u16 currentY, u16 width, u16 height)"
       "static void CopyGlyphToWindow_Parameterized(u8 *tileData, u16 currentX, u16 currentY, u16 width, u16 height)"
       pfr_text_printer_source
       "${pfr_text_printer_source}")

file(WRITE "${PFR_OUTPUT}" "${pfr_text_printer_source}")
