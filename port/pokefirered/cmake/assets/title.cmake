set(PFR_TITLE_GFX_DIR "${PFR_REPO_ROOT}/graphics/title_screen")
set(PFR_TITLE_FR_GFX_DIR "${PFR_TITLE_GFX_DIR}/firered")
set(PFR_TITLE_LG_GFX_DIR "${PFR_TITLE_GFX_DIR}/leafgreen")

function(pfr_title_palette output_rel source_rel)
  pfr_asset_palette(
    COLLECTION PFR_NATIVE_ASSET_FILES
    OUTPUT "${output_rel}"
    SOURCE "${PFR_TITLE_GFX_DIR}/${source_rel}")
endfunction()

function(pfr_title_bin output_rel source_rel)
  pfr_asset_bin_lz(
    COLLECTION PFR_NATIVE_ASSET_FILES
    OUTPUT "${output_rel}"
    SOURCE "${PFR_TITLE_GFX_DIR}/${source_rel}")
endfunction()

function(pfr_title_tile4_lz rel_stem png_rel palette_rel)
  pfr_asset_tile4_lz(
    COLLECTION PFR_NATIVE_ASSET_FILES
    STEM "${rel_stem}"
    PNG "${PFR_TITLE_GFX_DIR}/${png_rel}"
    PALETTE "${PFR_TITLE_GFX_DIR}/${palette_rel}")
endfunction()

function(pfr_title_tile8_lz rel_stem png_rel palette_rel)
  pfr_asset_tile8_lz(
    COLLECTION PFR_NATIVE_ASSET_FILES
    STEM "${rel_stem}"
    PNG "${PFR_TITLE_GFX_DIR}/${png_rel}"
    PALETTE "${PFR_TITLE_GFX_DIR}/${palette_rel}")
endfunction()

pfr_title_tile4_lz(
  "graphics/title_screen/border_bg"
  "border_bg.png"
  "firered/background.pal")
pfr_title_tile4_lz(
  "graphics/title_screen/copyright_press_start"
  "copyright_press_start.png"
  "firered/background.pal")
pfr_title_bin(
  "graphics/title_screen/copyright_press_start.bin.lz"
  "copyright_press_start.bin")

foreach(index RANGE 1 6)
  pfr_title_bin("graphics/title_screen/unused${index}.bin.lz" "unused${index}.bin")
endforeach()

pfr_title_tile4_lz("graphics/title_screen/slash" "slash.png" "firered/slash.pal")
pfr_title_tile4_lz("graphics/title_screen/blank_sprite" "blank_sprite.png" "firered/slash.pal")

pfr_title_palette(
  "graphics/title_screen/firered/background.gbapal"
  "firered/background.pal")
pfr_title_palette(
  "graphics/title_screen/firered/slash.gbapal"
  "firered/slash.pal")
pfr_asset_tilemap_bundle(
  COLLECTION PFR_NATIVE_ASSET_FILES
  PALETTE_OUTPUT "graphics/title_screen/firered/game_title_logo.gbapal"
  PALETTE_SOURCE "${PFR_TITLE_FR_GFX_DIR}/game_title_logo.pal"
  TILES_STEM "graphics/title_screen/firered/game_title_logo"
  TILE_MODE tile8
  PNG "${PFR_TITLE_FR_GFX_DIR}/game_title_logo.png"
  TILE_PALETTE "${PFR_TITLE_FR_GFX_DIR}/game_title_logo.pal"
  MAP_OUTPUT "graphics/title_screen/firered/game_title_logo.bin.lz"
  MAP_SOURCE "${PFR_TITLE_FR_GFX_DIR}/game_title_logo.bin")
pfr_asset_tilemap_bundle(
  COLLECTION PFR_NATIVE_ASSET_FILES
  PALETTE_OUTPUT "graphics/title_screen/firered/box_art_mon.gbapal"
  PALETTE_SOURCE "${PFR_TITLE_FR_GFX_DIR}/box_art_mon.pal"
  TILES_STEM "graphics/title_screen/firered/box_art_mon"
  PNG "${PFR_TITLE_FR_GFX_DIR}/box_art_mon.png"
  TILE_PALETTE "${PFR_TITLE_FR_GFX_DIR}/box_art_mon.pal"
  MAP_OUTPUT "graphics/title_screen/firered/box_art_mon.bin.lz"
  MAP_SOURCE "${PFR_TITLE_FR_GFX_DIR}/box_art_mon.bin")
pfr_title_bin(
  "graphics/title_screen/firered/border_bg.bin.lz"
  "firered/border_bg.bin")
pfr_asset_tiles_bundle(
  COLLECTION PFR_NATIVE_ASSET_FILES
  PALETTE_OUTPUT "graphics/title_screen/firered/flames.gbapal"
  PALETTE_SOURCE "${PFR_TITLE_FR_GFX_DIR}/flames.png"
  TILES_STEM "graphics/title_screen/firered/flames"
  PNG "${PFR_TITLE_FR_GFX_DIR}/flames.png"
  TILE_PALETTE "${PFR_TITLE_FR_GFX_DIR}/flames.png")
pfr_title_tile4_lz(
  "graphics/title_screen/firered/blank_flames"
  "firered/blank_flames.png"
  "firered/flames.png")

pfr_title_palette(
  "graphics/title_screen/leafgreen/background.gbapal"
  "leafgreen/background.pal")
pfr_title_palette(
  "graphics/title_screen/leafgreen/slash.gbapal"
  "leafgreen/slash.pal")
pfr_asset_tilemap_bundle(
  COLLECTION PFR_NATIVE_ASSET_FILES
  PALETTE_OUTPUT "graphics/title_screen/leafgreen/game_title_logo.gbapal"
  PALETTE_SOURCE "${PFR_TITLE_LG_GFX_DIR}/game_title_logo.pal"
  TILES_STEM "graphics/title_screen/leafgreen/game_title_logo"
  TILE_MODE tile8
  PNG "${PFR_TITLE_LG_GFX_DIR}/game_title_logo.png"
  TILE_PALETTE "${PFR_TITLE_LG_GFX_DIR}/game_title_logo.pal"
  MAP_OUTPUT "graphics/title_screen/leafgreen/game_title_logo.bin.lz"
  MAP_SOURCE "${PFR_TITLE_LG_GFX_DIR}/game_title_logo.bin")
pfr_asset_tilemap_bundle(
  COLLECTION PFR_NATIVE_ASSET_FILES
  PALETTE_OUTPUT "graphics/title_screen/leafgreen/box_art_mon.gbapal"
  PALETTE_SOURCE "${PFR_TITLE_LG_GFX_DIR}/box_art_mon.pal"
  TILES_STEM "graphics/title_screen/leafgreen/box_art_mon"
  PNG "${PFR_TITLE_LG_GFX_DIR}/box_art_mon.png"
  TILE_PALETTE "${PFR_TITLE_LG_GFX_DIR}/box_art_mon.pal"
  MAP_OUTPUT "graphics/title_screen/leafgreen/box_art_mon.bin.lz"
  MAP_SOURCE "${PFR_TITLE_LG_GFX_DIR}/box_art_mon.bin")
pfr_title_bin(
  "graphics/title_screen/leafgreen/border_bg.bin.lz"
  "leafgreen/border_bg.bin")
pfr_asset_tiles_bundle(
  COLLECTION PFR_NATIVE_ASSET_FILES
  PALETTE_OUTPUT "graphics/title_screen/leafgreen/leaves.gbapal"
  PALETTE_SOURCE "${PFR_TITLE_LG_GFX_DIR}/leaves.png"
  TILES_STEM "graphics/title_screen/leafgreen/leaves"
  PNG "${PFR_TITLE_LG_GFX_DIR}/leaves.png"
  TILE_PALETTE "${PFR_TITLE_LG_GFX_DIR}/leaves.png")
pfr_title_tile4_lz(
  "graphics/title_screen/leafgreen/streak"
  "leafgreen/streak.png"
  "leafgreen/leaves.png")
