cmake_minimum_required(VERSION 3.21)

include(FetchContent)

project(pokefirered LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "$<$<NOT:$<CONFIG:Debug>>:MultiThreaded>")

option(POKEFIRERED_HEADLESS "Default the native executable to headless mode" OFF)
option(POKEFIRERED_BUILD_ASSET_TOOLS "Build native helper tools from the repo" OFF)
option(POKEFIRERED_USE_SYSTEM_RAYLIB "Use a preinstalled raylib package" OFF)

get_filename_component(PFR_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(PFR_CHARMAP "${PFR_REPO_ROOT}/charmap.txt")
set(PFR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PFR_ASSET_ROOT "${CMAKE_CURRENT_BINARY_DIR}/assetroot")

add_executable(
  pfr_preproc
  ../../tools/preproc/asm_file.cpp
  ../../tools/preproc/c_file.cpp
  ../../tools/preproc/charmap.cpp
  ../../tools/preproc/io.cpp
  ../../tools/preproc/preproc.cpp
  ../../tools/preproc/string_parser.cpp
  ../../tools/preproc/utf8.cpp)

if(MSVC)
  target_sources(pfr_preproc PRIVATE tools/msvc/getopt_compat.cpp)
  target_include_directories(pfr_preproc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools/msvc/include)
  target_compile_definitions(pfr_preproc PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set(PFR_CORE_SOURCES
    src/audio.c
    src/core.c
    src/dma.c
    src/dma3.c
    src/demo.c
    src/gpu_regs.c
    src/main_runtime.c
    src/renderer.c
    src/storage.c
    src/sandbox.c
    src/stubs.c
    src/syscall.c
    src/task.c
    src/text_runtime.c)

add_library(pfr_core STATIC ${PFR_CORE_SOURCES})
target_include_directories(
  pfr_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include  # Port headers (shadow originals)
)
target_include_directories(pfr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_compile_definitions(
  pfr_core PUBLIC $<$<BOOL:${POKEFIRERED_HEADLESS}>:PFR_DEFAULT_HEADLESS=1>)

if(MSVC)
  target_compile_definitions(pfr_core PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(pfr_core PRIVATE /W4 /WX)
else()
  # The port and translated game code alias shared GBA buffers/registers
  # through multiple view types, which GCC/Clang may miscompile under
  # strict-aliasing optimizations on 64-bit hosts.
  target_compile_options(
    pfr_core
    PRIVATE -Wall
            -Wextra
            -Werror
            -Wpedantic
            -Wno-strict-prototypes
            -fno-strict-aliasing)
  target_link_libraries(pfr_core PRIVATE m)
endif()

if(POKEFIRERED_USE_SYSTEM_RAYLIB)
  find_package(raylib CONFIG REQUIRED)
else()
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    URL_HASH SHA256=AEA98ECF5BC5C5E0B789A76DE0083A21A70457050EA4CC2AEC7566935F5E258E)
  FetchContent_MakeAvailable(raylib)
endif()

target_link_libraries(pfr_core PUBLIC raylib)

add_executable(pfr_native_assettool tools/native_asset_tool.c)
target_link_libraries(pfr_native_assettool PRIVATE raylib)

if(MSVC)
  target_compile_definitions(pfr_native_assettool PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(pfr_native_assettool PRIVATE /W4 /WX)
else()
  target_compile_options(pfr_native_assettool PRIVATE -Wall -Wextra -Werror)
endif()

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

function(pfr_generate_gbapal out_var relative_output palette_source)
  pfr_generate_native_asset(
    ${out_var}
    "${relative_output}"
    gbapal
    "${palette_source}")
  set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

function(pfr_generate_tile4_lz out_var relative_stem png_source palette_source)
  set(raw_var "${out_var}_RAW")
  pfr_generate_native_asset(
    ${raw_var}
    "${relative_stem}.4bpp"
    tile4
    "${png_source}"
    "${palette_source}")
  pfr_generate_native_asset(
    ${out_var}
    "${relative_stem}.4bpp.lz"
    lz
    "${${raw_var}}")
  set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

function(pfr_generate_bin_lz out_var relative_output input_file)
  pfr_generate_native_asset(
    ${out_var}
    "${relative_output}"
    lz
    "${input_file}")
  set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

pfr_generate_native_asset(
  PFR_ASSET_TITLE_BORDER_BG_RAW
  "graphics/title_screen/border_bg.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/title_screen/border_bg.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/background.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BORDER_BG
  "graphics/title_screen/border_bg.4bpp.lz"
  lz
  "${PFR_ASSET_TITLE_BORDER_BG_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_COPYRIGHT_TILES_RAW
  "graphics/title_screen/copyright_press_start.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/title_screen/copyright_press_start.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/background.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_COPYRIGHT_TILES
  "graphics/title_screen/copyright_press_start.4bpp.lz"
  lz
  "${PFR_ASSET_TITLE_COPYRIGHT_TILES_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_COPYRIGHT_MAP
  "graphics/title_screen/copyright_press_start.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/copyright_press_start.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_UNUSED_1
  "graphics/title_screen/unused1.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/unused1.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_UNUSED_2
  "graphics/title_screen/unused2.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/unused2.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_UNUSED_3
  "graphics/title_screen/unused3.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/unused3.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_UNUSED_4
  "graphics/title_screen/unused4.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/unused4.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_UNUSED_5
  "graphics/title_screen/unused5.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/unused5.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_UNUSED_6
  "graphics/title_screen/unused6.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/unused6.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_SLASH_RAW
  "graphics/title_screen/slash.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/title_screen/slash.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/slash.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_SLASH
  "graphics/title_screen/slash.4bpp.lz"
  lz
  "${PFR_ASSET_TITLE_SLASH_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BLANK_SPRITE_RAW
  "graphics/title_screen/blank_sprite.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/title_screen/blank_sprite.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/slash.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BLANK_SPRITE
  "graphics/title_screen/blank_sprite.4bpp.lz"
  lz
  "${PFR_ASSET_TITLE_BLANK_SPRITE_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LOGO_PAL
  "graphics/title_screen/firered/game_title_logo.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/game_title_logo.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LOGO_TILES_RAW
  "graphics/title_screen/firered/game_title_logo.8bpp"
  tile8
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/game_title_logo.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/game_title_logo.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LOGO_TILES
  "graphics/title_screen/firered/game_title_logo.8bpp.lz"
  lz
  "${PFR_ASSET_TITLE_LOGO_TILES_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LOGO_MAP
  "graphics/title_screen/firered/game_title_logo.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/game_title_logo.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BOX_ART_PAL
  "graphics/title_screen/firered/box_art_mon.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/box_art_mon.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BOX_ART_TILES_RAW
  "graphics/title_screen/firered/box_art_mon.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/box_art_mon.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/box_art_mon.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BOX_ART_TILES
  "graphics/title_screen/firered/box_art_mon.4bpp.lz"
  lz
  "${PFR_ASSET_TITLE_BOX_ART_TILES_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BOX_ART_MAP
  "graphics/title_screen/firered/box_art_mon.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/box_art_mon.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BACKGROUND_PAL
  "graphics/title_screen/firered/background.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/background.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BORDER_BG_MAP
  "graphics/title_screen/firered/border_bg.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/border_bg.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_FLAMES_PAL
  "graphics/title_screen/firered/flames.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/flames.png")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_FLAMES_RAW
  "graphics/title_screen/firered/flames.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/flames.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/flames.png")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_FLAMES
  "graphics/title_screen/firered/flames.4bpp.lz"
  lz
  "${PFR_ASSET_TITLE_FLAMES_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BLANK_FLAMES_RAW
  "graphics/title_screen/firered/blank_flames.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/blank_flames.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/flames.png")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_BLANK_FLAMES
  "graphics/title_screen/firered/blank_flames.4bpp.lz"
  lz
  "${PFR_ASSET_TITLE_BLANK_FLAMES_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_SLASH_PAL
  "graphics/title_screen/firered/slash.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/title_screen/firered/slash.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_LOGO_PAL
  "graphics/title_screen/leafgreen/game_title_logo.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/game_title_logo.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_LOGO_TILES_RAW
  "graphics/title_screen/leafgreen/game_title_logo.8bpp"
  tile8
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/game_title_logo.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/game_title_logo.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_LOGO_TILES
  "graphics/title_screen/leafgreen/game_title_logo.8bpp.lz"
  lz
  "${PFR_ASSET_TITLE_LG_LOGO_TILES_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_LOGO_MAP
  "graphics/title_screen/leafgreen/game_title_logo.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/game_title_logo.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_BOX_ART_PAL
  "graphics/title_screen/leafgreen/box_art_mon.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/box_art_mon.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_BOX_ART_TILES_RAW
  "graphics/title_screen/leafgreen/box_art_mon.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/box_art_mon.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/box_art_mon.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_BOX_ART_TILES
  "graphics/title_screen/leafgreen/box_art_mon.4bpp.lz"
  lz
  "${PFR_ASSET_TITLE_LG_BOX_ART_TILES_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_BOX_ART_MAP
  "graphics/title_screen/leafgreen/box_art_mon.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/box_art_mon.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_BACKGROUND_PAL
  "graphics/title_screen/leafgreen/background.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/background.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_SLASH_PAL
  "graphics/title_screen/leafgreen/slash.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/slash.pal")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LG_BORDER_BG_MAP
  "graphics/title_screen/leafgreen/border_bg.bin.lz"
  lz
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/border_bg.bin")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LEAVES_PAL
  "graphics/title_screen/leafgreen/leaves.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/leaves.png")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LEAVES_RAW
  "graphics/title_screen/leafgreen/leaves.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/leaves.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/leaves.png")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_LEAVES
  "graphics/title_screen/leafgreen/leaves.4bpp.lz"
  lz
  "${PFR_ASSET_TITLE_LEAVES_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_STREAK_RAW
  "graphics/title_screen/leafgreen/streak.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/streak.png"
  "${PFR_REPO_ROOT}/graphics/title_screen/leafgreen/leaves.png")
pfr_generate_native_asset(
  PFR_ASSET_TITLE_STREAK
  "graphics/title_screen/leafgreen/streak.4bpp.lz"
  lz
  "${PFR_ASSET_TITLE_STREAK_RAW}")
pfr_generate_native_asset(
  PFR_ASSET_MAIN_MENU_BG_PAL
  "graphics/main_menu/bg.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/main_menu/bg.pal")
pfr_generate_native_asset(
  PFR_ASSET_MAIN_MENU_TEXTBOX_PAL
  "graphics/main_menu/textbox.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/main_menu/textbox.pal")
pfr_generate_native_asset(
  PFR_ASSET_TEXT_WINDOW_STD
  "graphics/text_window/std.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/text_window/std.png"
  "${PFR_REPO_ROOT}/graphics/text_window/stdpal_3.pal")
pfr_generate_native_asset(
  PFR_ASSET_TEXTPAL_0
  "graphics/text_window/stdpal_0.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/text_window/stdpal_0.pal")
pfr_generate_native_asset(
  PFR_ASSET_TEXTPAL_1
  "graphics/text_window/stdpal_1.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/text_window/stdpal_1.pal")
pfr_generate_native_asset(
  PFR_ASSET_TEXTPAL_2
  "graphics/text_window/stdpal_2.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/text_window/stdpal_2.pal")
pfr_generate_native_asset(
  PFR_ASSET_TEXTPAL_3
  "graphics/text_window/stdpal_3.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/text_window/stdpal_3.pal")
pfr_generate_native_asset(
  PFR_ASSET_TEXTPAL_4
  "graphics/text_window/stdpal_4.gbapal"
  gbapal
  "${PFR_REPO_ROOT}/graphics/text_window/stdpal_4.pal")

foreach(frame RANGE 1 10)
  pfr_generate_native_asset(
    PFR_ASSET_USER_FRAME_${frame}_TILES
    "graphics/text_window/type${frame}.4bpp"
    tile4
    "${PFR_REPO_ROOT}/graphics/text_window/type${frame}.png"
    "${PFR_REPO_ROOT}/graphics/text_window/type${frame}.png")
  pfr_generate_native_asset(
    PFR_ASSET_USER_FRAME_${frame}_PAL
    "graphics/text_window/type${frame}.gbapal"
    gbapal
    "${PFR_REPO_ROOT}/graphics/text_window/type${frame}.png")
  list(APPEND PFR_NATIVE_ASSET_FILES
       "${PFR_ASSET_USER_FRAME_${frame}_TILES}"
       "${PFR_ASSET_USER_FRAME_${frame}_PAL}")
endforeach()

pfr_generate_gbapal(
  PFR_ASSET_INTRO_COPYRIGHT_PAL
  "graphics/intro/copyright.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/copyright.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_COPYRIGHT_GFX
  "graphics/intro/copyright"
  "${PFR_REPO_ROOT}/graphics/intro/copyright.png"
  "${PFR_REPO_ROOT}/graphics/intro/copyright.pal")
pfr_generate_bin_lz(
  PFR_ASSET_INTRO_COPYRIGHT_MAP
  "graphics/intro/copyright.bin.lz"
  "${PFR_REPO_ROOT}/graphics/intro/copyright.bin")

pfr_generate_gbapal(
  PFR_ASSET_INTRO_GF_BG_PAL
  "graphics/intro/game_freak/bg.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/bg.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_GF_BG_GFX
  "graphics/intro/game_freak/bg"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/bg.png"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/bg.pal")
pfr_generate_bin_lz(
  PFR_ASSET_INTRO_GF_BG_MAP
  "graphics/intro/game_freak/bg.bin.lz"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/bg.bin")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_GF_LOGO_PAL
  "graphics/intro/game_freak/logo.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/logo.png")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_GF_TEXT_GFX
  "graphics/intro/game_freak/game_freak"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/game_freak.png"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/logo.png")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_GF_LOGO_GFX
  "graphics/intro/game_freak/logo"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/logo.png"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/logo.png")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_GF_STAR_PAL
  "graphics/intro/game_freak/star.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/star.png")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_GF_STAR_GFX
  "graphics/intro/game_freak/star"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/star.png"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/star.png")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_GF_SPARKLES_PAL
  "graphics/intro/game_freak/sparkles.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/sparkles.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_GF_SPARKLES_SMALL_GFX
  "graphics/intro/game_freak/sparkles_small"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/sparkles_small.png"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/sparkles.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_GF_SPARKLES_BIG_GFX
  "graphics/intro/game_freak/sparkles_big"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/sparkles_big.png"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/sparkles.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_GF_PRESENTS_GFX
  "graphics/intro/game_freak/presents"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/presents.png"
  "${PFR_REPO_ROOT}/graphics/intro/game_freak/logo.png")

pfr_generate_gbapal(
  PFR_ASSET_INTRO_SCENE1_GRASS_PAL
  "graphics/intro/scene_1/grass.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/scene_1/grass.png")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE1_GRASS_GFX
  "graphics/intro/scene_1/grass"
  "${PFR_REPO_ROOT}/graphics/intro/scene_1/grass.png"
  "${PFR_REPO_ROOT}/graphics/intro/scene_1/grass.png")
pfr_generate_bin_lz(
  PFR_ASSET_INTRO_SCENE1_GRASS_MAP
  "graphics/intro/scene_1/grass.bin.lz"
  "${PFR_REPO_ROOT}/graphics/intro/scene_1/grass.bin")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_SCENE1_BG_PAL
  "graphics/intro/scene_1/bg.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/scene_1/bg.png")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE1_BG_GFX
  "graphics/intro/scene_1/bg"
  "${PFR_REPO_ROOT}/graphics/intro/scene_1/bg.png"
  "${PFR_REPO_ROOT}/graphics/intro/scene_1/bg.png")
pfr_generate_bin_lz(
  PFR_ASSET_INTRO_SCENE1_BG_MAP
  "graphics/intro/scene_1/bg.bin.lz"
  "${PFR_REPO_ROOT}/graphics/intro/scene_1/bg.bin")

pfr_generate_gbapal(
  PFR_ASSET_INTRO_SCENE2_BG_PAL
  "graphics/intro/scene_2/bg.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/bg.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE2_BG_GFX
  "graphics/intro/scene_2/bg"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/bg.png"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/bg.pal")
pfr_generate_bin_lz(
  PFR_ASSET_INTRO_SCENE2_BG_MAP
  "graphics/intro/scene_2/bg.bin.lz"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/bg.bin")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_SCENE2_PLANTS_PAL
  "graphics/intro/scene_2/plants.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/plants.png")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE2_PLANTS_GFX
  "graphics/intro/scene_2/plants"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/plants.png"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/plants.png")
pfr_generate_bin_lz(
  PFR_ASSET_INTRO_SCENE2_PLANTS_MAP
  "graphics/intro/scene_2/plants.bin.lz"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/plants.bin")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_GENGAR_PAL
  "graphics/intro/gengar.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/gengar.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE2_GENGAR_CLOSE_GFX
  "graphics/intro/scene_2/gengar_close"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/gengar_close.png"
  "${PFR_REPO_ROOT}/graphics/intro/gengar.pal")
pfr_generate_bin_lz(
  PFR_ASSET_INTRO_SCENE2_GENGAR_CLOSE_MAP
  "graphics/intro/scene_2/gengar_close.bin.lz"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/gengar_close.bin")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_SCENE2_NIDORINO_CLOSE_PAL
  "graphics/intro/scene_2/nidorino_close.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/nidorino_close.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE2_NIDORINO_CLOSE_GFX
  "graphics/intro/scene_2/nidorino_close"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/nidorino_close.png"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/nidorino_close.pal")
pfr_generate_bin_lz(
  PFR_ASSET_INTRO_SCENE2_NIDORINO_CLOSE_MAP
  "graphics/intro/scene_2/nidorino_close.bin.lz"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/nidorino_close.bin")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_NIDORINO_PAL
  "graphics/intro/nidorino.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/nidorino.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE2_GENGAR_GFX
  "graphics/intro/scene_2/gengar"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/gengar.png"
  "${PFR_REPO_ROOT}/graphics/intro/gengar.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE2_NIDORINO_GFX
  "graphics/intro/scene_2/nidorino"
  "${PFR_REPO_ROOT}/graphics/intro/scene_2/nidorino.png"
  "${PFR_REPO_ROOT}/graphics/intro/nidorino.pal")

pfr_generate_gbapal(
  PFR_ASSET_INTRO_SCENE3_BG_PAL
  "graphics/intro/scene_3/bg.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/bg.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE3_BG_GFX
  "graphics/intro/scene_3/bg"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/bg.png"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/bg.pal")
pfr_generate_bin_lz(
  PFR_ASSET_INTRO_SCENE3_BG_MAP
  "graphics/intro/scene_3/bg.bin.lz"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/bg.bin")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE3_GENGAR_ANIM_GFX
  "graphics/intro/scene_3/gengar_anim"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/gengar_anim.png"
  "${PFR_REPO_ROOT}/graphics/intro/gengar.pal")
pfr_generate_bin_lz(
  PFR_ASSET_INTRO_SCENE3_GENGAR_ANIM_MAP
  "graphics/intro/scene_3/gengar_anim.bin.lz"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/gengar_anim.bin")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_SCENE3_GRASS_PAL
  "graphics/intro/scene_3/grass.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/grass.png")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE3_GRASS_GFX
  "graphics/intro/scene_3/grass"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/grass.png"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/grass.png")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE3_GENGAR_STATIC_GFX
  "graphics/intro/scene_3/gengar_static"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/gengar_static.png"
  "${PFR_REPO_ROOT}/graphics/intro/gengar.pal")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE3_NIDORINO_GFX
  "graphics/intro/scene_3/nidorino"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/nidorino.png"
  "${PFR_REPO_ROOT}/graphics/intro/nidorino.pal")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_SCENE3_SWIPE_PAL
  "graphics/intro/scene_3/swipe.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/swipe.png")
pfr_generate_gbapal(
  PFR_ASSET_INTRO_SCENE3_RECOIL_DUST_PAL
  "graphics/intro/scene_3/recoil_dust.gbapal"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/recoil_dust.png")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE3_SWIPE_GFX
  "graphics/intro/scene_3/swipe"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/swipe.png"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/swipe.png")
pfr_generate_tile4_lz(
  PFR_ASSET_INTRO_SCENE3_RECOIL_DUST_GFX
  "graphics/intro/scene_3/recoil_dust"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/recoil_dust.png"
  "${PFR_REPO_ROOT}/graphics/intro/scene_3/recoil_dust.png")

list(APPEND PFR_INTRO_ASSET_FILES
     "${PFR_ASSET_INTRO_COPYRIGHT_PAL}"
     "${PFR_ASSET_INTRO_COPYRIGHT_GFX}"
     "${PFR_ASSET_INTRO_COPYRIGHT_MAP}"
     "${PFR_ASSET_INTRO_GF_BG_PAL}"
     "${PFR_ASSET_INTRO_GF_BG_GFX}"
     "${PFR_ASSET_INTRO_GF_BG_MAP}"
     "${PFR_ASSET_INTRO_GF_LOGO_PAL}"
     "${PFR_ASSET_INTRO_GF_TEXT_GFX}"
     "${PFR_ASSET_INTRO_GF_LOGO_GFX}"
     "${PFR_ASSET_INTRO_GF_STAR_PAL}"
     "${PFR_ASSET_INTRO_GF_STAR_GFX}"
     "${PFR_ASSET_INTRO_GF_SPARKLES_PAL}"
     "${PFR_ASSET_INTRO_GF_SPARKLES_SMALL_GFX}"
     "${PFR_ASSET_INTRO_GF_SPARKLES_BIG_GFX}"
     "${PFR_ASSET_INTRO_GF_PRESENTS_GFX}"
     "${PFR_ASSET_INTRO_SCENE1_GRASS_PAL}"
     "${PFR_ASSET_INTRO_SCENE1_GRASS_GFX}"
     "${PFR_ASSET_INTRO_SCENE1_GRASS_MAP}"
     "${PFR_ASSET_INTRO_SCENE1_BG_PAL}"
     "${PFR_ASSET_INTRO_SCENE1_BG_GFX}"
     "${PFR_ASSET_INTRO_SCENE1_BG_MAP}"
     "${PFR_ASSET_INTRO_SCENE2_BG_PAL}"
     "${PFR_ASSET_INTRO_SCENE2_BG_GFX}"
     "${PFR_ASSET_INTRO_SCENE2_BG_MAP}"
     "${PFR_ASSET_INTRO_SCENE2_PLANTS_PAL}"
     "${PFR_ASSET_INTRO_SCENE2_PLANTS_GFX}"
     "${PFR_ASSET_INTRO_SCENE2_PLANTS_MAP}"
     "${PFR_ASSET_INTRO_GENGAR_PAL}"
     "${PFR_ASSET_INTRO_SCENE2_GENGAR_CLOSE_GFX}"
     "${PFR_ASSET_INTRO_SCENE2_GENGAR_CLOSE_MAP}"
     "${PFR_ASSET_INTRO_SCENE2_NIDORINO_CLOSE_PAL}"
     "${PFR_ASSET_INTRO_SCENE2_NIDORINO_CLOSE_GFX}"
     "${PFR_ASSET_INTRO_SCENE2_NIDORINO_CLOSE_MAP}"
     "${PFR_ASSET_INTRO_NIDORINO_PAL}"
     "${PFR_ASSET_INTRO_SCENE2_GENGAR_GFX}"
     "${PFR_ASSET_INTRO_SCENE2_NIDORINO_GFX}"
     "${PFR_ASSET_INTRO_SCENE3_BG_PAL}"
     "${PFR_ASSET_INTRO_SCENE3_BG_GFX}"
     "${PFR_ASSET_INTRO_SCENE3_BG_MAP}"
     "${PFR_ASSET_INTRO_SCENE3_GENGAR_ANIM_GFX}"
     "${PFR_ASSET_INTRO_SCENE3_GENGAR_ANIM_MAP}"
     "${PFR_ASSET_INTRO_SCENE3_GRASS_PAL}"
     "${PFR_ASSET_INTRO_SCENE3_GRASS_GFX}"
     "${PFR_ASSET_INTRO_SCENE3_GENGAR_STATIC_GFX}"
     "${PFR_ASSET_INTRO_SCENE3_NIDORINO_GFX}"
     "${PFR_ASSET_INTRO_SCENE3_SWIPE_PAL}"
     "${PFR_ASSET_INTRO_SCENE3_RECOIL_DUST_PAL}"
     "${PFR_ASSET_INTRO_SCENE3_SWIPE_GFX}"
     "${PFR_ASSET_INTRO_SCENE3_RECOIL_DUST_GFX}")

list(APPEND PFR_NATIVE_ASSET_FILES
     "${PFR_ASSET_TITLE_BORDER_BG}"
     "${PFR_ASSET_TITLE_COPYRIGHT_TILES}"
     "${PFR_ASSET_TITLE_COPYRIGHT_MAP}"
     "${PFR_ASSET_TITLE_UNUSED_1}"
     "${PFR_ASSET_TITLE_UNUSED_2}"
     "${PFR_ASSET_TITLE_UNUSED_3}"
     "${PFR_ASSET_TITLE_UNUSED_4}"
     "${PFR_ASSET_TITLE_UNUSED_5}"
     "${PFR_ASSET_TITLE_UNUSED_6}"
     "${PFR_ASSET_TITLE_SLASH}"
     "${PFR_ASSET_TITLE_BLANK_SPRITE}"
     "${PFR_ASSET_TITLE_LOGO_PAL}"
     "${PFR_ASSET_TITLE_LOGO_TILES}"
     "${PFR_ASSET_TITLE_LOGO_MAP}"
     "${PFR_ASSET_TITLE_BOX_ART_PAL}"
     "${PFR_ASSET_TITLE_BOX_ART_TILES}"
     "${PFR_ASSET_TITLE_BOX_ART_MAP}"
     "${PFR_ASSET_TITLE_BACKGROUND_PAL}"
     "${PFR_ASSET_TITLE_BORDER_BG_MAP}"
     "${PFR_ASSET_TITLE_FLAMES_PAL}"
     "${PFR_ASSET_TITLE_FLAMES}"
     "${PFR_ASSET_TITLE_BLANK_FLAMES}"
     "${PFR_ASSET_TITLE_SLASH_PAL}"
     "${PFR_ASSET_TITLE_LG_LOGO_PAL}"
     "${PFR_ASSET_TITLE_LG_LOGO_TILES}"
     "${PFR_ASSET_TITLE_LG_LOGO_MAP}"
     "${PFR_ASSET_TITLE_LG_BOX_ART_PAL}"
     "${PFR_ASSET_TITLE_LG_BOX_ART_TILES}"
     "${PFR_ASSET_TITLE_LG_BOX_ART_MAP}"
     "${PFR_ASSET_TITLE_LG_BACKGROUND_PAL}"
     "${PFR_ASSET_TITLE_LG_SLASH_PAL}"
     "${PFR_ASSET_TITLE_LG_BORDER_BG_MAP}"
     "${PFR_ASSET_TITLE_LEAVES_PAL}"
     "${PFR_ASSET_TITLE_LEAVES}"
     "${PFR_ASSET_TITLE_STREAK}"
     "${PFR_ASSET_MAIN_MENU_BG_PAL}"
     "${PFR_ASSET_MAIN_MENU_TEXTBOX_PAL}"
     "${PFR_ASSET_TEXT_WINDOW_STD}"
     "${PFR_ASSET_TEXTPAL_0}"
     "${PFR_ASSET_TEXTPAL_1}"
     "${PFR_ASSET_TEXTPAL_2}"
     "${PFR_ASSET_TEXTPAL_3}"
     "${PFR_ASSET_TEXTPAL_4}")

pfr_generate_native_asset(
  PFR_ASSET_FONT_DOWN_ARROWS
  "graphics/fonts/down_arrows.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/fonts/down_arrows.png"
  "${PFR_REPO_ROOT}/graphics/fonts/down_arrows.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_DOWN_ARROW_3
  "graphics/fonts/down_arrow_3.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/fonts/down_arrow_3.png"
  "${PFR_REPO_ROOT}/graphics/fonts/down_arrow_3.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_DOWN_ARROW_4
  "graphics/fonts/down_arrow_4.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/fonts/down_arrow_4.png"
  "${PFR_REPO_ROOT}/graphics/fonts/down_arrow_4.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_KEYPAD_ICONS
  "graphics/fonts/keypad_icons.4bpp"
  tile4
  "${PFR_REPO_ROOT}/graphics/fonts/keypad_icons.png"
  "${PFR_REPO_ROOT}/graphics/fonts/keypad_icons.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_LATIN_SMALL
  "graphics/fonts/latin_small.latfont"
  latfont
  "${PFR_REPO_ROOT}/graphics/fonts/latin_small.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_JAPANESE_SMALL
  "graphics/fonts/japanese_small.fwjpnfont"
  fwjpnfont
  "${PFR_REPO_ROOT}/graphics/fonts/japanese_small.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_LATIN_NORMAL
  "graphics/fonts/latin_normal.latfont"
  latfont
  "${PFR_REPO_ROOT}/graphics/fonts/latin_normal.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_JAPANESE_TALL
  "graphics/fonts/japanese_tall.fwjpnfont"
  fwjpnfont
  "${PFR_REPO_ROOT}/graphics/fonts/japanese_tall.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_JAPANESE_NORMAL
  "graphics/fonts/japanese_normal.fwjpnfont"
  fwjpnfont
  "${PFR_REPO_ROOT}/graphics/fonts/japanese_normal.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_LATIN_MALE
  "graphics/fonts/latin_male.latfont"
  latfont
  "${PFR_REPO_ROOT}/graphics/fonts/latin_male.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_JAPANESE_MALE
  "graphics/fonts/japanese_male.fwjpnfont"
  fwjpnfont
  "${PFR_REPO_ROOT}/graphics/fonts/japanese_male.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_LATIN_FEMALE
  "graphics/fonts/latin_female.latfont"
  latfont
  "${PFR_REPO_ROOT}/graphics/fonts/latin_female.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_JAPANESE_FEMALE
  "graphics/fonts/japanese_female.fwjpnfont"
  fwjpnfont
  "${PFR_REPO_ROOT}/graphics/fonts/japanese_female.png")
pfr_generate_native_asset(
  PFR_ASSET_FONT_JAPANESE_BOLD
  "graphics/fonts/japanese_bold.fwjpnfont"
  fwjpnfont
  "${PFR_REPO_ROOT}/graphics/fonts/japanese_bold.png")

set(PFR_TEXT_ASSET_FILES
    "${PFR_ASSET_FONT_DOWN_ARROWS}"
    "${PFR_ASSET_FONT_DOWN_ARROW_3}"
    "${PFR_ASSET_FONT_DOWN_ARROW_4}"
    "${PFR_ASSET_FONT_KEYPAD_ICONS}"
    "${PFR_ASSET_FONT_LATIN_SMALL}"
    "${PFR_ASSET_FONT_JAPANESE_SMALL}"
    "${PFR_ASSET_FONT_LATIN_NORMAL}"
    "${PFR_ASSET_FONT_JAPANESE_TALL}"
    "${PFR_ASSET_FONT_JAPANESE_NORMAL}"
    "${PFR_ASSET_FONT_LATIN_MALE}"
    "${PFR_ASSET_FONT_JAPANESE_MALE}"
    "${PFR_ASSET_FONT_LATIN_FEMALE}"
    "${PFR_ASSET_FONT_JAPANESE_FEMALE}"
    "${PFR_ASSET_FONT_JAPANESE_BOLD}")

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

set(PFR_GENERATED_TEXT_PRINTER "${PFR_GENERATED_DIR}/text_printer.native.c")
add_custom_command(
  OUTPUT "${PFR_GENERATED_TEXT_PRINTER}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PFR_GENERATED_DIR}"
  COMMAND
    ${CMAKE_COMMAND}
    -DPFR_SOURCE="${PFR_REPO_ROOT}/src/text_printer.c"
    -DPFR_OUTPUT="${PFR_GENERATED_TEXT_PRINTER}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_text_printer.cmake"
  DEPENDS
    "${PFR_REPO_ROOT}/src/text_printer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_text_printer.cmake")

# --------------------------------------------------------------------------
# pfr_game: original game source files compiled through the port shim headers
# --------------------------------------------------------------------------

set(PFR_GAME_SOURCES 
    ../../src/random.c 
    ../../src/malloc.c
    ../../src/string_util.c
    ${PFR_GENERATED_TEXT}
    ../../src/blend_palette.c
    ../../src/blit.c
    ${PFR_GENERATED_TEXT_PRINTER}
    ${PFR_GENERATED_STRINGS}
    ${PFR_GENERATED_INTRO}
    ../../src/bg.c
    ../../src/window.c
    ../../src/palette.c
    ../../src/sprite.c
    ../../src/math_util.c
    ../../src/trig.c
    ../../src/scanline_effect.c
    ../../src/decompress.c
    ../../src/text_window.c
    ${PFR_GENERATED_TEXT_WINDOW_GRAPHICS}
    ${PFR_GENERATED_NATIVE_ASSETS}
    ${PFR_GENERATED_TITLE_SCREEN}
    ${PFR_GENERATED_MAIN_MENU})

add_library(pfr_game OBJECT ${PFR_GAME_SOURCES})

target_include_directories(
  pfr_game
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(pfr_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

target_compile_definitions(
  pfr_game
  PRIVATE MODERN=1 FIRERED ENGLISH NDEBUG BUGFIX PFR_NATIVE=1
          _CRT_SECURE_NO_WARNINGS)

if(NOT MSVC)
  target_compile_options(pfr_game PRIVATE -fno-strict-aliasing)
  target_link_libraries(pfr_game PRIVATE m)
endif()

target_link_libraries(pfr_core PUBLIC pfr_game)

add_executable(pokefirered main.c src/platform.c)
target_include_directories(pokefirered PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pokefirered PRIVATE pfr_core raylib)

if(MSVC)
  target_compile_definitions(pokefirered PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(pokefirered PRIVATE /W4 /WX)
else()
  target_compile_options(
    pokefirered PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-strict-aliasing)
endif()

enable_testing()

add_executable(pfr_smoke tests/smoke.c)
target_include_directories(pfr_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pfr_smoke PRIVATE pfr_core)

if(MSVC)
  target_compile_definitions(pfr_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(pfr_smoke PRIVATE /W4 /WX)
else()
  target_compile_options(
    pfr_smoke PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-strict-aliasing)
endif()

add_test(NAME pfr_smoke COMMAND pfr_smoke)
add_test(
  NAME pfr_asset_title_border_bg
  COMMAND ${CMAKE_COMMAND}
          -DPFR_ASSET_ROOT=${PFR_ASSET_ROOT}
          -P
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_title_border_bg.cmake")
add_test(
  NAME pfr_asset_title_press_start_blink
  COMMAND ${CMAKE_COMMAND}
          -DPFR_ASSET_ROOT=${PFR_ASSET_ROOT}
          -P
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_title_press_start_blink.cmake")
add_test(
  NAME pfr_asset_title_flames_palette
  COMMAND ${CMAKE_COMMAND}
          -DPFR_ASSET_ROOT=${PFR_ASSET_ROOT}
          -P
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_title_flames_palette.cmake")
add_test(
  NAME pfr_headless_boot
  COMMAND pokefirered --mode game --headless --frames 1600 --quit-on-title)
add_test(
  NAME pfr_headless_main_menu
  COMMAND pokefirered --mode game --headless --frames 2300 --auto-press-start-frame 1860 --auto-press-start-frame 1880 --auto-press-start-frame 1900 --quit-on-main-menu)
add_test(NAME pfr_headless_demo COMMAND pokefirered --mode demo --headless --frames 3)
add_test(NAME pfr_headless_sandbox COMMAND pokefirered --mode sandbox --headless --frames 3)

add_executable(pfr_integration tests/integration.c)
target_include_directories(pfr_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pfr_integration PRIVATE pfr_core)

add_executable(pfr_title_capture tests/title_capture.c)
target_include_directories(pfr_title_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pfr_title_capture PRIVATE pfr_core)

set(PFR_STARTUP_FRAME_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/startup_frame_manifest.txt")
set(PFR_STARTUP_CAPTURE_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/test-output/startup-capture")
set(PFR_STARTUP_CAPTURE_OUTPUT_MANIFEST
    "${PFR_STARTUP_CAPTURE_OUTPUT_DIR}/capture_manifest.txt")

if(MSVC)
  target_compile_definitions(pfr_integration PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(pfr_integration PRIVATE /W4 /WX)
  target_compile_definitions(pfr_title_capture PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(pfr_title_capture PRIVATE /W4 /WX)
else()
  target_compile_options(
    pfr_integration PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-strict-aliasing)
  target_compile_options(
    pfr_title_capture PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-strict-aliasing)
endif()

add_test(NAME pfr_integration COMMAND pfr_integration)
add_test(
  NAME pfr_startup_capture
  COMMAND pfr_title_capture
          --mode
          game
          --output-dir
          ${PFR_STARTUP_CAPTURE_OUTPUT_DIR}
          --manifest-out
          ${PFR_STARTUP_CAPTURE_OUTPUT_MANIFEST}
          --frame-manifest
          ${PFR_STARTUP_FRAME_MANIFEST})

if(WIN32)
  set(PFR_CLANG_FORMAT
      "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-format.exe")
else()
  find_program(PFR_CLANG_FORMAT NAMES clang-format REQUIRED)
endif()

file(
  GLOB_RECURSE PFR_FORMAT_FILES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp)
file(
  GLOB PFR_FORMAT_FILES_TOP CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
list(APPEND PFR_FORMAT_FILES ${PFR_FORMAT_FILES_TOP})

add_custom_target(
  format-native
  COMMAND ${PFR_CLANG_FORMAT} -i --style=file:${CMAKE_CURRENT_SOURCE_DIR}/.clang-format ${PFR_FORMAT_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM)
