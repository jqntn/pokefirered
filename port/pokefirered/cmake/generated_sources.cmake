pfr_add_preprocessed_c(
  PFR_GENERATED_TEXT
  "${PFR_REPO_ROOT}/src/text.c"
  "text.native.c"
  "${PFR_ASSET_ROOT}"
  ${PFR_TEXT_ASSET_FILES})
pfr_add_preprocessed_c(
  PFR_GENERATED_STRINGS
  "${PFR_REPO_ROOT}/src/strings.c"
  "strings.native.c"
  "${PFR_REPO_ROOT}")
pfr_add_preprocessed_c(
  PFR_GENERATED_TEXT_WINDOW_GRAPHICS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/text_window_assets.c"
  "text_window_assets.native.c"
  "${PFR_ASSET_ROOT}"
  ${PFR_NATIVE_ASSET_FILES})
pfr_add_preprocessed_c(
  PFR_GENERATED_NATIVE_ASSETS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/native_assets.c"
  "native_assets.native.c"
  "${PFR_ASSET_ROOT}"
  ${PFR_NATIVE_ASSET_FILES})
pfr_add_preprocessed_c(
  PFR_GENERATED_INTRO
  "${PFR_REPO_ROOT}/src/intro.c"
  "intro.native.c"
  "${PFR_ASSET_ROOT}"
  ${PFR_INTRO_ASSET_FILES})
pfr_add_preprocessed_c(
  PFR_GENERATED_TITLE_SCREEN
  "${PFR_REPO_ROOT}/src/title_screen.c"
  "title_screen.native.c"
  "${PFR_ASSET_ROOT}"
  ${PFR_NATIVE_ASSET_FILES})
pfr_add_preprocessed_c(
  PFR_GENERATED_MAIN_MENU
  "${PFR_REPO_ROOT}/src/main_menu.c"
  "main_menu.native.c"
  "${PFR_ASSET_ROOT}"
  ${PFR_NATIVE_ASSET_FILES})

set(PFR_GENERATED_AUDIO_ASSETS_C
    "${PFR_GENERATED_DIR}/audio_assets.native.c")
set(PFR_GENERATED_AUDIO_ASSETS_H
    "${PFR_GENERATED_DIR}/audio_assets.native.h")
set(PFR_AUDIO_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/audio-stage")

add_custom_command(
  OUTPUT ${PFR_GENERATED_AUDIO_ASSETS_C} ${PFR_GENERATED_AUDIO_ASSETS_H}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PFR_GENERATED_DIR}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PFR_AUDIO_STAGE_DIR}
  COMMAND
    ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/audio_asset_tool.py"
    --repo-root
    "${PFR_REPO_ROOT}"
    --stage-dir
    "${PFR_AUDIO_STAGE_DIR}"
    --mid2agb
    "$<TARGET_FILE:mid2agb>"
    --out-c
    "${PFR_GENERATED_AUDIO_ASSETS_C}"
    --out-h
    "${PFR_GENERATED_AUDIO_ASSETS_H}"
  DEPENDS
    mid2agb
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/audio_asset_tool.py"
    "${PFR_REPO_ROOT}/sound/MPlayDef.s"
    "${PFR_REPO_ROOT}/sound/keysplit_tables.inc"
    "${PFR_REPO_ROOT}/sound/song_table.inc"
    "${PFR_REPO_ROOT}/sound/voice_groups.inc"
    "${PFR_REPO_ROOT}/sound/direct_sound_samples/cries/charizard.wav"
    "${PFR_REPO_ROOT}/sound/direct_sound_samples/cries/nidorino.wav"
    "${PFR_REPO_ROOT}/sound/songs/midi/mus_game_freak.mid"
    "${PFR_REPO_ROOT}/sound/songs/midi/mus_intro_fight.mid"
    "${PFR_REPO_ROOT}/sound/songs/midi/mus_title.mid"
    "${PFR_REPO_ROOT}/sound/songs/midi/se_select.mid"
  VERBATIM)

set(PFR_GENERATED_AUDIO_ASSETS ${PFR_GENERATED_AUDIO_ASSETS_C})
