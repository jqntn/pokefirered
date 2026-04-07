set(PFR_FONT_GFX_DIR "${PFR_REPO_ROOT}/graphics/fonts")

pfr_asset_tile4(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/down_arrows.4bpp"
  PNG "${PFR_FONT_GFX_DIR}/down_arrows.png")
pfr_asset_tile4(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/down_arrow_3.4bpp"
  PNG "${PFR_FONT_GFX_DIR}/down_arrow_3.png")
pfr_asset_tile4(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/down_arrow_4.4bpp"
  PNG "${PFR_FONT_GFX_DIR}/down_arrow_4.png")
pfr_asset_tile4(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/keypad_icons.4bpp"
  PNG "${PFR_FONT_GFX_DIR}/keypad_icons.png")

pfr_asset_font(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/latin_small.latfont"
  MODE latfont
  PNG "${PFR_FONT_GFX_DIR}/latin_small.png")
pfr_asset_font(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/japanese_small.fwjpnfont"
  MODE fwjpnfont
  PNG "${PFR_FONT_GFX_DIR}/japanese_small.png")
pfr_asset_font(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/latin_normal.latfont"
  MODE latfont
  PNG "${PFR_FONT_GFX_DIR}/latin_normal.png")
pfr_asset_font(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/japanese_tall.fwjpnfont"
  MODE fwjpnfont
  PNG "${PFR_FONT_GFX_DIR}/japanese_tall.png")
pfr_asset_font(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/japanese_normal.fwjpnfont"
  MODE fwjpnfont
  PNG "${PFR_FONT_GFX_DIR}/japanese_normal.png")
pfr_asset_font(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/latin_male.latfont"
  MODE latfont
  PNG "${PFR_FONT_GFX_DIR}/latin_male.png")
pfr_asset_font(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/japanese_male.fwjpnfont"
  MODE fwjpnfont
  PNG "${PFR_FONT_GFX_DIR}/japanese_male.png")
pfr_asset_font(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/latin_female.latfont"
  MODE latfont
  PNG "${PFR_FONT_GFX_DIR}/latin_female.png")
pfr_asset_font(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/japanese_female.fwjpnfont"
  MODE fwjpnfont
  PNG "${PFR_FONT_GFX_DIR}/japanese_female.png")
pfr_asset_font(
  COLLECTION PFR_TEXT_ASSET_FILES
  OUTPUT "graphics/fonts/japanese_bold.fwjpnfont"
  MODE fwjpnfont
  PNG "${PFR_FONT_GFX_DIR}/japanese_bold.png")
