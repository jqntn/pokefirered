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

pfr_generate_midi_asm(PFR_SONG_ASM_MUS_GAME_FREAK
  "sound/mus_game_freak.s"
  "${PFR_REPO_ROOT}/sound/songs/midi/mus_game_freak.mid")
pfr_generate_midi_asm(PFR_SONG_ASM_MUS_INTRO_FIGHT
  "sound/mus_intro_fight.s"
  "${PFR_REPO_ROOT}/sound/songs/midi/mus_intro_fight.mid")
pfr_generate_midi_asm(PFR_SONG_ASM_MUS_TITLE
  "sound/mus_title.s"
  "${PFR_REPO_ROOT}/sound/songs/midi/mus_title.mid")
pfr_generate_midi_asm(PFR_SONG_ASM_SE_SELECT
  "sound/se_select.s"
  "${PFR_REPO_ROOT}/sound/songs/midi/se_select.mid")

file(GLOB_RECURSE PFR_SOUND_WAV_FILES
  "${PFR_REPO_ROOT}/sound/direct_sound_samples/*.wav")
set(PFR_SOUND_WAV_ASM_FILES "")
foreach(wav_file IN LISTS PFR_SOUND_WAV_FILES)
  file(RELATIVE_PATH wav_rel "${PFR_REPO_ROOT}" "${wav_file}")
  string(REGEX REPLACE "\\.wav$" ".s" asm_rel "${wav_rel}")
  pfr_generate_wav_asm(wav_asm_output "${asm_rel}" "${wav_file}")
  list(APPEND PFR_SOUND_WAV_ASM_FILES "${wav_asm_output}")
endforeach()

set(PFR_GENERATED_AUDIO_ASSETS_C
    "${PFR_GENERATED_DIR}/audio_assets.native.c")

add_custom_command(
  OUTPUT ${PFR_GENERATED_AUDIO_ASSETS_C}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PFR_GENERATED_DIR}
  COMMAND
    ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/audio_asset_tool.py"
    --repo-root
    "${PFR_REPO_ROOT}"
    --asset-root
    "${PFR_ASSET_ROOT}"
    --out-c
    "${PFR_GENERATED_AUDIO_ASSETS_C}"
    --asset sound/mus_game_freak.s
    --asset sound/mus_intro_fight.s
    --asset sound/mus_title.s
    --asset sound/se_select.s
    --asset sound/direct_sound_samples/cries/charizard.s
    --asset sound/direct_sound_samples/cries/nidorino.s
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/audio_asset_tool.py"
    "${PFR_REPO_ROOT}/sound/MPlayDef.s"
    "${PFR_REPO_ROOT}/sound/keysplit_tables.inc"
    "${PFR_REPO_ROOT}/sound/song_table.inc"
    "${PFR_REPO_ROOT}/sound/voice_groups.inc"
    "${PFR_REPO_ROOT}/include/constants/species.h"
    ${PFR_SOUND_WAV_ASM_FILES}
    ${PFR_SONG_ASM_MUS_GAME_FREAK}
    ${PFR_SONG_ASM_MUS_INTRO_FIGHT}
    ${PFR_SONG_ASM_MUS_TITLE}
    ${PFR_SONG_ASM_SE_SELECT}
  VERBATIM)

set(PFR_GENERATED_AUDIO_ASSETS ${PFR_GENERATED_AUDIO_ASSETS_C})
