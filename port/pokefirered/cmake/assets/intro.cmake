set(PFR_INTRO_GFX_DIR "${PFR_REPO_ROOT}/graphics/intro")

macro(pfr_intro_palette output_rel source_rel)
  pfr_asset_palette(
    COLLECTION PFR_INTRO_ASSET_FILES
    OUTPUT "${output_rel}"
    SOURCE "${PFR_INTRO_GFX_DIR}/${source_rel}")
endmacro()

macro(pfr_intro_tilemap rel_stem palette_rel)
  pfr_asset_tilemap_bundle(
    COLLECTION PFR_INTRO_ASSET_FILES
    PALETTE_OUTPUT "graphics/intro/${rel_stem}.gbapal"
    PALETTE_SOURCE "${PFR_INTRO_GFX_DIR}/${palette_rel}"
    TILES_STEM "graphics/intro/${rel_stem}"
    PNG "${PFR_INTRO_GFX_DIR}/${rel_stem}.png"
    TILE_PALETTE "${PFR_INTRO_GFX_DIR}/${palette_rel}"
    MAP_OUTPUT "graphics/intro/${rel_stem}.bin.lz"
    MAP_SOURCE "${PFR_INTRO_GFX_DIR}/${rel_stem}.bin")
endmacro()

macro(pfr_intro_tiles rel_stem palette_rel)
  pfr_asset_tile4_lz(
    COLLECTION PFR_INTRO_ASSET_FILES
    STEM "graphics/intro/${rel_stem}"
    PNG "${PFR_INTRO_GFX_DIR}/${rel_stem}.png"
    PALETTE "${PFR_INTRO_GFX_DIR}/${palette_rel}")
endmacro()

pfr_intro_tilemap("copyright" "copyright.pal")
pfr_intro_tilemap("game_freak/bg" "game_freak/bg.pal")
pfr_intro_palette("graphics/intro/game_freak/logo.gbapal" "game_freak/logo.png")
pfr_intro_tiles("game_freak/game_freak" "game_freak/logo.png")
pfr_intro_tiles("game_freak/logo" "game_freak/logo.png")
pfr_intro_palette("graphics/intro/game_freak/star.gbapal" "game_freak/star.png")
pfr_intro_tiles("game_freak/star" "game_freak/star.png")
pfr_intro_palette(
  "graphics/intro/game_freak/sparkles.gbapal"
  "game_freak/sparkles.pal")
pfr_intro_tiles("game_freak/sparkles_small" "game_freak/sparkles.pal")
pfr_intro_tiles("game_freak/sparkles_big" "game_freak/sparkles.pal")
pfr_intro_tiles("game_freak/presents" "game_freak/logo.png")

pfr_intro_tilemap("scene_1/grass" "scene_1/grass.png")
pfr_intro_tilemap("scene_1/bg" "scene_1/bg.png")

pfr_intro_tilemap("scene_2/bg" "scene_2/bg.pal")
pfr_intro_tilemap("scene_2/plants" "scene_2/plants.png")
pfr_intro_palette("graphics/intro/gengar.gbapal" "gengar.pal")
pfr_intro_tiles("scene_2/gengar_close" "gengar.pal")
pfr_asset_bin_lz(
  COLLECTION PFR_INTRO_ASSET_FILES
  OUTPUT "graphics/intro/scene_2/gengar_close.bin.lz"
  SOURCE "${PFR_INTRO_GFX_DIR}/scene_2/gengar_close.bin")
pfr_intro_palette(
  "graphics/intro/scene_2/nidorino_close.gbapal"
  "scene_2/nidorino_close.pal")
pfr_intro_tilemap("scene_2/nidorino_close" "scene_2/nidorino_close.pal")
pfr_intro_palette("graphics/intro/nidorino.gbapal" "nidorino.pal")
pfr_intro_tiles("scene_2/gengar" "gengar.pal")
pfr_intro_tiles("scene_2/nidorino" "nidorino.pal")

pfr_intro_tilemap("scene_3/bg" "scene_3/bg.pal")
pfr_intro_tiles("scene_3/gengar_anim" "gengar.pal")
pfr_asset_bin_lz(
  COLLECTION PFR_INTRO_ASSET_FILES
  OUTPUT "graphics/intro/scene_3/gengar_anim.bin.lz"
  SOURCE "${PFR_INTRO_GFX_DIR}/scene_3/gengar_anim.bin")
pfr_intro_palette("graphics/intro/scene_3/grass.gbapal" "scene_3/grass.png")
pfr_intro_tiles("scene_3/grass" "scene_3/grass.png")
pfr_intro_tiles("scene_3/gengar_static" "gengar.pal")
pfr_intro_tiles("scene_3/nidorino" "nidorino.pal")
pfr_intro_palette("graphics/intro/scene_3/swipe.gbapal" "scene_3/swipe.png")
pfr_intro_palette(
  "graphics/intro/scene_3/recoil_dust.gbapal"
  "scene_3/recoil_dust.png")
pfr_intro_tiles("scene_3/swipe" "scene_3/swipe.png")
pfr_intro_tiles("scene_3/recoil_dust" "scene_3/recoil_dust.png")
