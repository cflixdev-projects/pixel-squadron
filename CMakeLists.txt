cmake_minimum_required(VERSION 3.16)

project(pixel_squadron
        VERSION 1.0.0
        DESCRIPTION "A small top-down twin-stick arena shooter built with raylib"
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
#  raylib
#  Prefers an installed copy when one is present, otherwise pulls and builds
#  the tagged release straight from GitHub.
# ---------------------------------------------------------------------------
option(PIXEL_SQUADRON_FETCH_RAYLIB "Download & build raylib if not installed" ON)

find_package(raylib 5.0 QUIET)

if(NOT raylib_FOUND)
    if(NOT PIXEL_SQUADRON_FETCH_RAYLIB)
        message(FATAL_ERROR "raylib not found and fetching has been disabled")
    endif()

    message(STATUS "raylib not found locally - fetching it from GitHub")
    include(FetchContent)

    set(BUILD_EXAMPLES      OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES         OFF CACHE BOOL "" FORCE)
    set(SUPPORT_BUSY_WAIT_LOOP OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG        5.5
        GIT_SHALLOW    TRUE)

    FetchContent_MakeAvailable(raylib)
endif()

# ---------------------------------------------------------------------------
#  Game
# ---------------------------------------------------------------------------
add_executable(pixel_squadron
    src/main.c
    src/assets.c
    src/arena.c
    src/chat.c
    src/entities.c
    src/hud.c)

target_include_directories(pixel_squadron PRIVATE src)
target_link_libraries(pixel_squadron PRIVATE raylib)

if(NOT MSVC)
    target_compile_options(pixel_squadron PRIVATE
        -Wall -Wextra
        -Wno-unused-parameter)
    target_link_libraries(pixel_squadron PRIVATE m)
endif()

if(WIN32 AND NOT MINGW)
    # raylib already pulls in winmm, but be explicit for MSVC builds.
    target_link_libraries(pixel_squadron PRIVATE winmm)
endif()

# ---------------------------------------------------------------------------
#  Web (Emscripten) build
#  Compiles to WebAssembly, bakes ./assets into the virtual filesystem and
#  emits index.html / index.js / index.wasm / index.data ready for static
#  hosting (GitHub Pages friendly).
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    set_target_properties(pixel_squadron PROPERTIES
        SUFFIX      ".html"
        OUTPUT_NAME "index")

    target_link_options(pixel_squadron PRIVATE
        -sUSE_GLFW=3
        -sASYNCIFY
        -sALLOW_MEMORY_GROWTH=1
        -sFORCE_FILESYSTEM=1
        -sEXIT_RUNTIME=0
        -sGL_ENABLE_GET_PROC_ADDRESS=1
        --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        --post-js "${CMAKE_CURRENT_SOURCE_DIR}/web/compat.js"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/audio@assets/audio"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts@assets/fonts"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites@assets/sprites")

    message(STATUS "Emscripten build: web/shell.html, assets preloaded from ./assets")
endif()

# ---------------------------------------------------------------------------
#  Ship the assets alongside the binary so the build tree is runnable
#  straight from the command line. (The web build preloads them instead.)
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
    add_custom_command(TARGET pixel_squadron POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:pixel_squadron>/assets"
        COMMENT "Copying assets next to the executable")
endif()
