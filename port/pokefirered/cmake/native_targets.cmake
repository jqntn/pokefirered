function(pfr_define_native_tool_targets)
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
    target_include_directories(
      pfr_preproc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools/msvc/include)
    target_compile_definitions(pfr_preproc PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  add_executable(pfr_native_assettool tools/native_asset_tool.c)
  target_link_libraries(pfr_native_assettool PRIVATE raylib)
  pfr_apply_native_warnings(pfr_native_assettool)

  add_executable(
    mid2agb
    ../../tools/mid2agb/agb.cpp
    ../../tools/mid2agb/error.cpp
    ../../tools/mid2agb/main.cpp
    ../../tools/mid2agb/midi.cpp
    ../../tools/mid2agb/tables.cpp)

  add_executable(
    wav2agb
    ../../tools/wav2agb/converter.cpp
    ../../tools/wav2agb/wav_file.cpp
    ../../tools/wav2agb/wav2agb.cpp)
endfunction()

function(pfr_define_native_runtime_targets)
  set(PFR_CORE_SOURCES
      src/audio.c
      src/audio_runtime.c
      src/m4a_host_overrides.c
      src/m4a_port_include.c
      src/m4a_port.c
      src/core.c
      src/dma.c
      src/dma3.c
      src/demo.c
      src/gpu_regs.c
      src/main_runtime.c
      src/renderer.c
      src/scripted_input.c
      src/storage.c
      src/sandbox.c
      src/stubs.c
      src/syscall.c
      src/task.c
      src/text_runtime.c
      ${PFR_GENERATED_AUDIO_ASSETS})

  add_library(pfr_core STATIC ${PFR_CORE_SOURCES})
  target_include_directories(
    pfr_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
           ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
  target_compile_definitions(pfr_core PRIVATE PFR_PORT)
  pfr_apply_native_warnings(pfr_core)

  if(NOT MSVC)
    target_link_libraries(pfr_core PRIVATE m)
  endif()

  set(PFR_GAME_SOURCES
      ../../src/random.c
      ../../src/malloc.c
      src/sound_runtime.c
      src/m4a_tables_port_include.c
      ../../src/string_util.c
      ${PFR_GENERATED_TEXT}
      ../../src/blend_palette.c
      ../../src/blit.c
      ../../src/text_printer.c
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
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
  target_compile_definitions(
    pfr_game
    PRIVATE MODERN=1
            FIRERED
            ENGLISH
            NDEBUG
            BUGFIX
            PFR_PORT
            $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

  if(NOT MSVC)
    target_link_libraries(pfr_game PRIVATE m)
  endif()

  target_link_libraries(pfr_core PUBLIC pfr_game raylib)

  pfr_add_port_executable(pokefirered main.c src/platform.c)
endfunction()
