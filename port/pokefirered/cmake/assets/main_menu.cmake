set(PFR_MAIN_MENU_GFX_DIR "${PFR_REPO_ROOT}/graphics/main_menu")

pfr_asset_palette(
  COLLECTION PFR_NATIVE_ASSET_FILES
  OUTPUT "graphics/main_menu/bg.gbapal"
  SOURCE "${PFR_MAIN_MENU_GFX_DIR}/bg.pal")
pfr_asset_palette(
  COLLECTION PFR_NATIVE_ASSET_FILES
  OUTPUT "graphics/main_menu/textbox.gbapal"
  SOURCE "${PFR_MAIN_MENU_GFX_DIR}/textbox.pal")
