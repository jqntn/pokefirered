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

get_filename_component(PFR_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(PFR_CHARMAP "${PFR_REPO_ROOT}/charmap.txt")
set(PFR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PFR_ASSET_ROOT "${CMAKE_CURRENT_BINARY_DIR}/assetroot")

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  URL_HASH SHA256=AEA98ECF5BC5C5E0B789A76DE0083A21A70457050EA4CC2AEC7566935F5E258E)
FetchContent_MakeAvailable(raylib)

set(PFR_TEXT_ASSET_FILES)
set(PFR_NATIVE_ASSET_FILES)
set(PFR_INTRO_ASSET_FILES)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/native_target_helpers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/native_targets.cmake)
pfr_define_native_tool_targets()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/native_asset_helpers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/assets/fonts.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/assets/text_window.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/assets/title.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/assets/main_menu.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/assets/intro.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/generated_sources.cmake)

pfr_define_native_runtime_targets()

enable_testing()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/native_tests.cmake)
pfr_define_native_tests()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/native_dev_tools.cmake)
pfr_define_native_dev_tools()
