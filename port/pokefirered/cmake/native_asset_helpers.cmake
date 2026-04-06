function(pfr_generate_native_asset out_var relative_output mode input_file)
  set(output_file "${PFR_ASSET_ROOT}/${relative_output}")
  get_filename_component(output_dir "${output_file}" DIRECTORY)

  set(command_args "${mode}" "${input_file}" "${output_file}")
  set(depends pfr_native_assettool "${input_file}")

  if(ARGC GREATER 4)
    list(APPEND command_args "${ARGV4}")
    list(APPEND depends "${ARGV4}")
  endif()

  add_custom_command(
    OUTPUT "${output_file}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
    COMMAND $<TARGET_FILE:pfr_native_assettool> ${command_args}
    DEPENDS ${depends})
  set(${out_var} "${output_file}" PARENT_SCOPE)
endfunction()

function(pfr_generate_wav_bin out_var relative_output wav_file)
  set(output_file "${PFR_ASSET_ROOT}/${relative_output}")
  get_filename_component(output_dir "${output_file}" DIRECTORY)
  set(extra_args ${ARGN})

  add_custom_command(
    OUTPUT "${output_file}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
    COMMAND $<TARGET_FILE:wav2agb> -b ${extra_args} "${wav_file}" "${output_file}"
    DEPENDS wav2agb "${wav_file}")
  set(${out_var} "${output_file}" PARENT_SCOPE)
endfunction()

function(pfr_generate_midi_asm out_var relative_output midi_file)
  set(output_file "${PFR_ASSET_ROOT}/${relative_output}")
  get_filename_component(output_dir "${output_file}" DIRECTORY)
  set(midi_cfg "${PFR_REPO_ROOT}/sound/songs/midi/midi.cfg")

  add_custom_command(
    OUTPUT "${output_file}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
    COMMAND
      ${CMAKE_COMMAND}
      -DPFR_MID2AGB=$<TARGET_FILE:mid2agb>
      -DPFR_MIDI_CFG="${midi_cfg}"
      -DPFR_MIDI_FILE="${midi_file}"
      -DPFR_OUTPUT_FILE="${output_file}"
      -DPFR_REPO_ROOT="${PFR_REPO_ROOT}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_mid2agb_from_cfg.cmake"
    DEPENDS
      mid2agb
      "${midi_file}"
      "${midi_cfg}"
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_mid2agb_from_cfg.cmake")
  set(${out_var} "${output_file}" PARENT_SCOPE)
endfunction()

function(pfr_add_preprocessed_c out_var source_file output_name working_directory)
  set(output_file "${PFR_GENERATED_DIR}/${output_name}")
  add_custom_command(
    OUTPUT "${output_file}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_GENERATED_DIR}"
    COMMAND
      ${CMAKE_COMMAND}
      -DPFR_PREPROC=$<TARGET_FILE:pfr_preproc>
      -DPFR_SOURCE="${source_file}"
      -DPFR_CHARMAP="${PFR_CHARMAP}"
      -DPFR_OUTPUT="${output_file}"
      -DPFR_WORKING_DIRECTORY="${working_directory}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/preprocess_c_file.cmake"
    DEPENDS
      pfr_preproc
      "${source_file}"
      "${PFR_CHARMAP}"
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/preprocess_c_file.cmake"
      ${ARGN})
  set(${out_var} "${output_file}" PARENT_SCOPE)
endfunction()

function(pfr_append_files_to_collection collection_name)
  set(updated "${${collection_name}}")

  foreach(file IN LISTS ARGN)
    if(NOT "${file}" STREQUAL "")
      list(APPEND updated "${file}")
    endif()
  endforeach()

  set(${collection_name} "${updated}" PARENT_SCOPE)
endfunction()

function(pfr_generate_compressed_tiles out_var tile_mode relative_stem png_source palette_source)
  if(tile_mode STREQUAL "tile4")
    set(tile_mode_suffix "4bpp")
  elseif(tile_mode STREQUAL "tile8")
    set(tile_mode_suffix "8bpp")
  else()
    message(FATAL_ERROR "unsupported tile mode: ${tile_mode}")
  endif()

  set(raw_var "${out_var}_RAW")
  pfr_generate_native_asset(
    ${raw_var}
    "${relative_stem}.${tile_mode_suffix}"
    "${tile_mode}"
    "${png_source}"
    "${palette_source}")
  pfr_generate_native_asset(
    ${out_var}
    "${relative_stem}.${tile_mode_suffix}.lz"
    lz
    "${${raw_var}}")
  set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

function(pfr_generate_raw_tiles out_var tile_mode relative_output png_source palette_source)
  if(NOT tile_mode STREQUAL "tile4" AND NOT tile_mode STREQUAL "tile8")
    message(FATAL_ERROR "unsupported tile mode: ${tile_mode}")
  endif()

  pfr_generate_native_asset(
    ${out_var}
    "${relative_output}"
    "${tile_mode}"
    "${png_source}"
    "${palette_source}")
  set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

function(pfr_asset_palette)
  cmake_parse_arguments(PFR "" "COLLECTION;OUTPUT;SOURCE" "" ${ARGN})

  if(NOT PFR_COLLECTION OR NOT PFR_OUTPUT OR NOT PFR_SOURCE)
    message(FATAL_ERROR "pfr_asset_palette requires COLLECTION, OUTPUT, and SOURCE")
  endif()

  pfr_generate_native_asset(output "${PFR_OUTPUT}" gbapal "${PFR_SOURCE}")
  pfr_append_files_to_collection("${PFR_COLLECTION}" "${output}")
  set(${PFR_COLLECTION} "${${PFR_COLLECTION}}" PARENT_SCOPE)
endfunction()

function(pfr_asset_bin_lz)
  cmake_parse_arguments(PFR "" "COLLECTION;OUTPUT;SOURCE" "" ${ARGN})

  if(NOT PFR_COLLECTION OR NOT PFR_OUTPUT OR NOT PFR_SOURCE)
    message(FATAL_ERROR "pfr_asset_bin_lz requires COLLECTION, OUTPUT, and SOURCE")
  endif()

  pfr_generate_native_asset(output "${PFR_OUTPUT}" lz "${PFR_SOURCE}")
  pfr_append_files_to_collection("${PFR_COLLECTION}" "${output}")
  set(${PFR_COLLECTION} "${${PFR_COLLECTION}}" PARENT_SCOPE)
endfunction()

function(pfr_asset_tile4)
  cmake_parse_arguments(PFR "" "COLLECTION;OUTPUT;PNG;PALETTE" "" ${ARGN})

  if(NOT PFR_COLLECTION OR NOT PFR_OUTPUT OR NOT PFR_PNG)
    message(FATAL_ERROR "pfr_asset_tile4 requires COLLECTION, OUTPUT, and PNG")
  endif()

  if(NOT PFR_PALETTE)
    set(PFR_PALETTE "${PFR_PNG}")
  endif()

  pfr_generate_raw_tiles(output tile4 "${PFR_OUTPUT}" "${PFR_PNG}" "${PFR_PALETTE}")
  pfr_append_files_to_collection("${PFR_COLLECTION}" "${output}")
  set(${PFR_COLLECTION} "${${PFR_COLLECTION}}" PARENT_SCOPE)
endfunction()

function(pfr_asset_tile4_lz)
  cmake_parse_arguments(PFR "" "COLLECTION;STEM;PNG;PALETTE" "" ${ARGN})

  if(NOT PFR_COLLECTION OR NOT PFR_STEM OR NOT PFR_PNG)
    message(FATAL_ERROR "pfr_asset_tile4_lz requires COLLECTION, STEM, and PNG")
  endif()

  if(NOT PFR_PALETTE)
    set(PFR_PALETTE "${PFR_PNG}")
  endif()

  pfr_generate_compressed_tiles(output tile4 "${PFR_STEM}" "${PFR_PNG}" "${PFR_PALETTE}")
  pfr_append_files_to_collection("${PFR_COLLECTION}" "${output}")
  set(${PFR_COLLECTION} "${${PFR_COLLECTION}}" PARENT_SCOPE)
endfunction()

function(pfr_asset_tile8_lz)
  cmake_parse_arguments(PFR "" "COLLECTION;STEM;PNG;PALETTE" "" ${ARGN})

  if(NOT PFR_COLLECTION OR NOT PFR_STEM OR NOT PFR_PNG)
    message(FATAL_ERROR "pfr_asset_tile8_lz requires COLLECTION, STEM, and PNG")
  endif()

  if(NOT PFR_PALETTE)
    set(PFR_PALETTE "${PFR_PNG}")
  endif()

  pfr_generate_compressed_tiles(output tile8 "${PFR_STEM}" "${PFR_PNG}" "${PFR_PALETTE}")
  pfr_append_files_to_collection("${PFR_COLLECTION}" "${output}")
  set(${PFR_COLLECTION} "${${PFR_COLLECTION}}" PARENT_SCOPE)
endfunction()

function(pfr_asset_font)
  cmake_parse_arguments(PFR "" "COLLECTION;OUTPUT;MODE;PNG" "" ${ARGN})

  if(NOT PFR_COLLECTION OR NOT PFR_OUTPUT OR NOT PFR_MODE OR NOT PFR_PNG)
    message(FATAL_ERROR "pfr_asset_font requires COLLECTION, OUTPUT, MODE, and PNG")
  endif()

  pfr_generate_native_asset(output "${PFR_OUTPUT}" "${PFR_MODE}" "${PFR_PNG}")
  pfr_append_files_to_collection("${PFR_COLLECTION}" "${output}")
  set(${PFR_COLLECTION} "${${PFR_COLLECTION}}" PARENT_SCOPE)
endfunction()

function(pfr_asset_tiles_bundle)
  cmake_parse_arguments(
    PFR
    ""
    "COLLECTION;PALETTE_OUTPUT;PALETTE_SOURCE;TILES_STEM;TILE_MODE;PNG;TILE_PALETTE"
    ""
    ${ARGN})

  if(NOT PFR_COLLECTION OR NOT PFR_PALETTE_OUTPUT OR NOT PFR_PALETTE_SOURCE OR NOT PFR_TILES_STEM
     OR NOT PFR_PNG)
    message(
      FATAL_ERROR
        "pfr_asset_tiles_bundle requires COLLECTION, PALETTE_OUTPUT, PALETTE_SOURCE, TILES_STEM, and PNG")
  endif()

  if(NOT PFR_TILE_MODE)
    set(PFR_TILE_MODE tile4)
  endif()

  if(NOT PFR_TILE_PALETTE)
    set(PFR_TILE_PALETTE "${PFR_PALETTE_SOURCE}")
  endif()

  pfr_generate_native_asset(palette_output "${PFR_PALETTE_OUTPUT}" gbapal "${PFR_PALETTE_SOURCE}")

  if(PFR_TILE_MODE STREQUAL "tile4")
    pfr_generate_compressed_tiles(tile_output tile4 "${PFR_TILES_STEM}" "${PFR_PNG}" "${PFR_TILE_PALETTE}")
  elseif(PFR_TILE_MODE STREQUAL "tile8")
    pfr_generate_compressed_tiles(tile_output tile8 "${PFR_TILES_STEM}" "${PFR_PNG}" "${PFR_TILE_PALETTE}")
  else()
    message(FATAL_ERROR "unsupported TILE_MODE for pfr_asset_tiles_bundle: ${PFR_TILE_MODE}")
  endif()

  pfr_append_files_to_collection("${PFR_COLLECTION}" "${palette_output}" "${tile_output}")
  set(${PFR_COLLECTION} "${${PFR_COLLECTION}}" PARENT_SCOPE)
endfunction()

function(pfr_asset_tilemap_bundle)
  cmake_parse_arguments(
    PFR
    ""
    "COLLECTION;PALETTE_OUTPUT;PALETTE_SOURCE;TILES_STEM;TILE_MODE;PNG;TILE_PALETTE;MAP_OUTPUT;MAP_SOURCE"
    ""
    ${ARGN})

  if(NOT PFR_COLLECTION OR NOT PFR_PALETTE_OUTPUT OR NOT PFR_PALETTE_SOURCE OR NOT PFR_TILES_STEM
     OR NOT PFR_PNG OR NOT PFR_MAP_OUTPUT OR NOT PFR_MAP_SOURCE)
    message(
      FATAL_ERROR
        "pfr_asset_tilemap_bundle requires COLLECTION, PALETTE_OUTPUT, PALETTE_SOURCE, TILES_STEM, PNG, MAP_OUTPUT, and MAP_SOURCE")
  endif()

  if(NOT PFR_TILE_MODE)
    set(PFR_TILE_MODE tile4)
  endif()

  if(NOT PFR_TILE_PALETTE)
    set(PFR_TILE_PALETTE "${PFR_PALETTE_SOURCE}")
  endif()

  pfr_generate_native_asset(palette_output "${PFR_PALETTE_OUTPUT}" gbapal "${PFR_PALETTE_SOURCE}")

  if(PFR_TILE_MODE STREQUAL "tile4")
    pfr_generate_compressed_tiles(tile_output tile4 "${PFR_TILES_STEM}" "${PFR_PNG}" "${PFR_TILE_PALETTE}")
  elseif(PFR_TILE_MODE STREQUAL "tile8")
    pfr_generate_compressed_tiles(tile_output tile8 "${PFR_TILES_STEM}" "${PFR_PNG}" "${PFR_TILE_PALETTE}")
  else()
    message(
      FATAL_ERROR
        "unsupported TILE_MODE for pfr_asset_tilemap_bundle: ${PFR_TILE_MODE}")
  endif()

  pfr_generate_native_asset(map_output "${PFR_MAP_OUTPUT}" lz "${PFR_MAP_SOURCE}")
  pfr_append_files_to_collection(
    "${PFR_COLLECTION}" "${palette_output}" "${tile_output}" "${map_output}")
  set(${PFR_COLLECTION} "${${PFR_COLLECTION}}" PARENT_SCOPE)
endfunction()
