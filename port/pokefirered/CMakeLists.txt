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
    src/task.c)

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
  target_compile_options(pfr_core PRIVATE -Wall -Wextra -Werror -Wpedantic -Wno-strict-prototypes)
  target_link_libraries(pfr_core PRIVATE m)
endif()

# --------------------------------------------------------------------------
# pfr_game: original game source files compiled through the port shim headers
# --------------------------------------------------------------------------

set(PFR_GAME_SOURCES 
    ../../src/random.c 
    ../../src/malloc.c
    ../../src/string_util.c
    ../../src/text.c
    ../../src/strings.c
    ../../src/intro.c
    ../../src/bg.c
    ../../src/window.c
    ../../src/palette.c
    ../../src/sprite.c
    ../../src/math_util.c
    ../../src/trig.c
    ../../src/scanline_effect.c
    ../../src/decompress.c)

add_library(pfr_game OBJECT ${PFR_GAME_SOURCES})

# Port headers first (shadow originals), then original repo headers.
target_include_directories(
  pfr_game
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(pfr_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

target_compile_definitions(
  pfr_game
  PRIVATE MODERN=1 FIRERED ENGLISH NDEBUG _CRT_SECURE_NO_WARNINGS)

if(NOT MSVC)
  target_link_libraries(pfr_game PRIVATE m)
endif()

# Link pfr_game into pfr_core so all downstream targets get both.
target_link_libraries(pfr_core PUBLIC pfr_game)

if(POKEFIRERED_BUILD_ASSET_TOOLS)
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
    target_compile_options(pfr_preproc PRIVATE /W4 /WX /wd4244 /wd4267 /wd4996)
  else()
    target_compile_options(pfr_preproc PRIVATE -Wall -Wextra -Werror)
  endif()
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

add_executable(pokefirered main.c src/platform.c)
target_include_directories(pokefirered PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pokefirered PRIVATE pfr_core raylib)

if(MSVC)
  target_compile_definitions(pokefirered PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(pokefirered PRIVATE /W4 /WX)
else()
  target_compile_options(pokefirered PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

enable_testing()

add_executable(pfr_smoke tests/smoke.c)
target_include_directories(pfr_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pfr_smoke PRIVATE pfr_core)

if(MSVC)
  target_compile_definitions(pfr_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(pfr_smoke PRIVATE /W4 /WX)
else()
  target_compile_options(pfr_smoke PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

add_test(NAME pfr_smoke COMMAND pfr_smoke)
add_test(NAME pfr_headless_boot COMMAND pokefirered --demo --headless --frames 3 --quit-on-title)

add_executable(pfr_integration tests/integration.c)
target_include_directories(pfr_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pfr_integration PRIVATE pfr_core)

if(MSVC)
  target_compile_definitions(pfr_integration PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(pfr_integration PRIVATE /W4 /WX)
else()
  target_compile_options(pfr_integration PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

add_test(NAME pfr_integration COMMAND pfr_integration)

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
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.h;*.hpp;*.c;*.cpp")
file(
  GLOB PFR_FORMAT_FILES_TOP CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
list(APPEND PFR_FORMAT_FILES ${PFR_FORMAT_FILES_TOP})

add_custom_target(
  format-native
  COMMAND ${PFR_CLANG_FORMAT} -i --style=file:${CMAKE_CURRENT_SOURCE_DIR}/.clang-format ${PFR_FORMAT_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM)
