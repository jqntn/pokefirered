set(PFR_TEXT_WINDOW_GFX_DIR "${PFR_REPO_ROOT}/graphics/text_window")

pfr_asset_tile4(
  COLLECTION PFR_NATIVE_ASSET_FILES
  OUTPUT "graphics/text_window/std.4bpp"
  PNG "${PFR_TEXT_WINDOW_GFX_DIR}/std.png"
  PALETTE "${PFR_TEXT_WINDOW_GFX_DIR}/stdpal_3.pal")

foreach(palette RANGE 0 4)
  pfr_asset_palette(
    COLLECTION PFR_NATIVE_ASSET_FILES
    OUTPUT "graphics/text_window/stdpal_${palette}.gbapal"
    SOURCE "${PFR_TEXT_WINDOW_GFX_DIR}/stdpal_${palette}.pal")
endforeach()

foreach(frame RANGE 1 10)
  pfr_asset_tile4(
    COLLECTION PFR_NATIVE_ASSET_FILES
    OUTPUT "graphics/text_window/type${frame}.4bpp"
    PNG "${PFR_TEXT_WINDOW_GFX_DIR}/type${frame}.png")
  pfr_asset_palette(
    COLLECTION PFR_NATIVE_ASSET_FILES
    OUTPUT "graphics/text_window/type${frame}.gbapal"
    SOURCE "${PFR_TEXT_WINDOW_GFX_DIR}/type${frame}.png")
endforeach()
