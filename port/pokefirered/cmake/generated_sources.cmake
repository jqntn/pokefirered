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

file(GLOB PFR_SOUND_MIDI_FILES
  RELATIVE "${PFR_REPO_ROOT}"
  "${PFR_REPO_ROOT}/sound/songs/midi/*.mid")
list(SORT PFR_SOUND_MIDI_FILES)
set(PFR_SOUND_MIDI_ASM_FILES "")
foreach(midi_rel IN LISTS PFR_SOUND_MIDI_FILES)
  get_filename_component(midi_stem "${midi_rel}" NAME_WE)
  pfr_generate_midi_asm(midi_output
    "sound/${midi_stem}.s"
    "${PFR_REPO_ROOT}/${midi_rel}")
  list(APPEND PFR_SOUND_MIDI_ASM_FILES "${midi_output}")
endforeach()

file(GLOB_RECURSE PFR_SOUND_WAV_FILES
  "${PFR_REPO_ROOT}/sound/direct_sound_samples/*.wav")
set(PFR_SOUND_WAV_BIN_FILES "")
foreach(wav_file IN LISTS PFR_SOUND_WAV_FILES)
  file(RELATIVE_PATH wav_rel "${PFR_REPO_ROOT}" "${wav_file}")
  string(REGEX REPLACE "\\.wav$" ".bin" bin_rel "${wav_rel}")
  if(wav_rel MATCHES "^sound/direct_sound_samples/cries/")
    pfr_generate_wav_bin(wav_bin_output "${bin_rel}" "${wav_file}" -c -l 1 --no-pad)
  else()
    pfr_generate_wav_bin(wav_bin_output "${bin_rel}" "${wav_file}")
  endif()
  list(APPEND PFR_SOUND_WAV_BIN_FILES "${wav_bin_output}")
endforeach()

set(PFR_GENERATED_AUDIO_ASSETS_C
    "${PFR_GENERATED_DIR}/audio_assets.native.c")
set(PFR_GENERATED_AUDIO_ASSET_MANIFEST
    "${PFR_GENERATED_DIR}/audio_assets.native.json")

add_custom_command(
  OUTPUT ${PFR_GENERATED_AUDIO_ASSETS_C} ${PFR_GENERATED_AUDIO_ASSET_MANIFEST}
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
    --manifest-out
    "${PFR_GENERATED_AUDIO_ASSET_MANIFEST}"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/audio_asset_tool.py"
    "${PFR_REPO_ROOT}/sound/MPlayDef.s"
    "${PFR_REPO_ROOT}/sound/direct_sound_data.inc"
    "${PFR_REPO_ROOT}/sound/keysplit_tables.inc"
    "${PFR_REPO_ROOT}/sound/programmable_wave_data.inc"
    "${PFR_REPO_ROOT}/sound/song_table.inc"
    "${PFR_REPO_ROOT}/sound/voice_groups.inc"
    "${PFR_REPO_ROOT}/sound/cry_tables.inc"
    ${PFR_SOUND_WAV_BIN_FILES}
    ${PFR_SOUND_MIDI_ASM_FILES}
  VERBATIM)

set(PFR_GENERATED_AUDIO_ASSETS ${PFR_GENERATED_AUDIO_ASSETS_C})
