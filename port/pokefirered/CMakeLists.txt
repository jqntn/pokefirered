cmake_minimum_required(VERSION 3.15)

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "$<$<NOT:$<CONFIG:Debug>>:MultiThreaded>")

project(pokefirered LANGUAGES C)

add_executable(pokefirered "main.c")

FetchContent_Declare(raylib URL "https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz")
FetchContent_MakeAvailable(raylib)

target_link_libraries(pokefirered PRIVATE raylib)

if(MSVC)
    target_compile_options(pokefirered PRIVATE /W4 /WX)
else()
    target_compile_options(pokefirered PRIVATE -Wall -Wextra -Werror)
endif()
