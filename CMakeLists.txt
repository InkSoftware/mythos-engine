cmake_minimum_required(VERSION 3.22)
project(mythos_engine VERSION 1.0.0 LANGUAGES C CXX)

# mythos-engine: a small engine for first-person grid games on SDL3 and OpenGL 4.1 core.
#
#   mythos::engine           (no SDL, no GL, no game) grid levels, movement, player, doors, props and
#                        verbs, menus and settings, meshes, models, textures, fonts, i18n, JSON
#   mythos::engine_platform  (SDL3) window, renderer, audio mixer, input, files, clocks
#   mythos::sdl_main         link this into an executable (SDL3 needs no separate main library)
#
# Use it from another project with add_subdirectory() or FetchContent:
#
#   include(FetchContent)
#   FetchContent_Declare(mythos_engine GIT_REPOSITORY <url> GIT_TAG v0.1.0)
#   FetchContent_MakeAvailable(mythos_engine)
#   target_link_libraries(my_game PRIVATE mythos::engine mythos::engine_platform mythos::sdl_main)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

option(MYTHOS_BUILD_PLATFORM "Build the SDL3 platform layer (mythos::engine_platform)" ON)
option(MYTHOS_BUILD_TESTS "Build the engine's unit tests" ${PROJECT_IS_TOP_LEVEL})
option(MYTHOS_FETCH_SDL      "Download SDL3 when no installed copy is found" ON)
option(MYTHOS_LTO            "Link-time optimization for Release builds of the engine libraries, when the compiler supports it" ON)
option(MYTHOS_PORTABLE       "Link SDL statically so the app runs without SDL installed (for distribution)" OFF)
set(MYTHOS_SDL_VERSION "3.2.28" CACHE STRING "SDL3 version fetched when MYTHOS_FETCH_SDL is ON")

# ---------------------------------------------------------------- mythos::engine
add_library(mythos_engine STATIC
    src/engine/audio/sound_bank.cpp
    src/engine/core/fileio.cpp
    src/engine/core/json.cpp
    src/engine/core/mat4.cpp
    src/engine/core/vfs.cpp
    src/engine/data/achievements.cpp
    src/engine/data/credits.cpp
    src/engine/data/engine_strings.cpp
    src/engine/data/i18n.cpp
    src/engine/data/translation.cpp
    src/engine/gfx/font.cpp
    src/engine/gfx/materials.cpp
    src/engine/gfx/mesh.cpp
    src/engine/gfx/model.cpp
    src/engine/gfx/normalmap.cpp
    src/engine/gfx/pbr.cpp
    src/engine/gfx/stb_image_impl.cpp
    src/engine/gfx/stb_truetype_impl.cpp
    src/engine/gfx/text.cpp
    src/engine/gfx/textures.cpp
    src/engine/ui/menu.cpp
    src/engine/ui/settings.cpp
    src/engine/ui/settings_ui.cpp
    src/engine/ui/ui.cpp
    src/engine/world/interact.cpp
    src/engine/world/level.cpp
    src/engine/world/mirror.cpp
    src/engine/world/movement.cpp
    src/engine/world/body.cpp
    src/engine/world/player.cpp
    src/engine/world/room_doors.cpp
    src/engine/world/tiles.cpp
)
add_library(mythos::engine ALIAS mythos_engine)
# The engine's own assets (fonts, common sounds and textures): a game merges them under its own.
set(MYTHOS_ENGINE_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets" CACHE INTERNAL "mythos-engine's assets folder")
if(MYTHOS_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT MYTHOS_IPO_OK OUTPUT MYTHOS_IPO_WHY LANGUAGES CXX)
    if(MYTHOS_IPO_OK)
        set_target_properties(mythos_engine PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    endif()
endif()
target_include_directories(mythos_engine PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

# URL downloads: extracted files get the time of extraction (CMP0135's NEW behaviour; the option needs CMake 3.24).
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    set(MYTHOS_FETCH_TIMESTAMP DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
else()
    set(MYTHOS_FETCH_TIMESTAMP "")
endif()

# nlohmann/json: the JSON value type (an installed copy is used when there is one).
find_package(nlohmann_json 3.12 CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    message(STATUS "fetching nlohmann/json")
    include(FetchContent)
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    FetchContent_Declare(nlohmann_json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz ${MYTHOS_FETCH_TIMESTAMP})
    FetchContent_MakeAvailable(nlohmann_json)
endif()
target_link_libraries(mythos_engine PUBLIC nlohmann_json::nlohmann_json)

# GLM: the generic matrix maths behind core/mat4 (header-only).
find_package(glm 1.0.3 CONFIG QUIET)
if(NOT TARGET glm::glm)
    message(STATUS "fetching glm")
    include(FetchContent)
    FetchContent_Declare(glm URL https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz ${MYTHOS_FETCH_TIMESTAMP})
    FetchContent_MakeAvailable(glm)
endif()
target_link_libraries(mythos_engine PRIVATE glm::glm)
# stb_truetype (public domain) rasterises the TrueType fonts; stb_image decodes texture overrides.
target_include_directories(mythos_engine PRIVATE third_party/stb)
if(MSVC)
    set_source_files_properties(src/engine/gfx/stb_truetype_impl.cpp src/engine/gfx/stb_image_impl.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(src/engine/gfx/stb_truetype_impl.cpp src/engine/gfx/stb_image_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
target_compile_features(mythos_engine PUBLIC cxx_std_20)
if(MSVC)
    # Sources and headers are UTF-8 (Greek strings, the settings page's glyphs); say so, for the games that include them too.
    target_compile_options(mythos_engine PUBLIC /utf-8)
endif()

function(mythos_engine_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
mythos_engine_warnings(mythos_engine)

# ---------------------------------------------------------------- mythos::engine_platform (SDL3)
if(MYTHOS_BUILD_PLATFORM)
    # MYTHOS_PORTABLE links SDL statically (always fetched), so the app runs without SDL installed.
    if(MYTHOS_PORTABLE)
        set(MYTHOS_FETCH_SDL ON)
    else()
        find_package(SDL3 CONFIG QUIET)
    endif()
    if(NOT TARGET SDL3::SDL3 AND NOT TARGET SDL3-static)
        if(MYTHOS_FETCH_SDL)
            message(STATUS "fetching SDL ${MYTHOS_SDL_VERSION}")
            include(FetchContent)
            set(SDL_STATIC ON CACHE BOOL "" FORCE)
            set(SDL_SHARED OFF CACHE BOOL "" FORCE)
            set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
            set(MYTHOS_SDL_URL https://github.com/libsdl-org/SDL/releases/download/release-${MYTHOS_SDL_VERSION}/SDL3-${MYTHOS_SDL_VERSION}.tar.gz)
            if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
                FetchContent_Declare(SDL3 URL ${MYTHOS_SDL_URL} DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
            else()
                FetchContent_Declare(SDL3 URL ${MYTHOS_SDL_URL})
            endif()
            FetchContent_MakeAvailable(SDL3)
        else()
            message(FATAL_ERROR "SDL3 not found. Install it or set MYTHOS_FETCH_SDL=ON.")
        endif()
    endif()

    if(TARGET SDL3::SDL3)
        set(MYTHOS_SDL_LIB SDL3::SDL3)
    else()
        set(MYTHOS_SDL_LIB SDL3-static)
    endif()

    # The GLSL lives in src/engine/platform/gl/shaders/ and is embedded into a generated source.
    set(MYTHOS_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/platform/gl/shaders")
    set(MYTHOS_SHADER_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/scene_shaders_data.cpp")
    file(GLOB MYTHOS_SHADER_FILES CONFIGURE_DEPENDS "${MYTHOS_SHADER_DIR}/*/*")
    add_custom_command(
        OUTPUT "${MYTHOS_SHADER_CPP}"
        COMMAND ${CMAKE_COMMAND} -DSHADER_DIR=${MYTHOS_SHADER_DIR} -DOUT=${MYTHOS_SHADER_CPP}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
        DEPENDS ${MYTHOS_SHADER_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
        COMMENT "Embedding shaders")

    # OpenGL entry points are loaded at run time through SDL, so no GL library is linked here.
    add_library(mythos_engine_platform STATIC
        src/engine/platform/app.cpp
        src/engine/platform/apply_settings.cpp
        src/engine/platform/audio.cpp
        src/engine/platform/gl/gl_api.cpp
        src/engine/platform/gl/body_instances.cpp
        src/engine/platform/gl/gl_objects.cpp
        src/engine/platform/gl/scene_renderer.cpp
        ${MYTHOS_SHADER_CPP}
        src/engine/platform/gl/scene_shadows.cpp
        src/engine/platform/gl/text_renderer.cpp
        src/engine/platform/input.cpp
        src/engine/platform/window.cpp)
    add_library(mythos::engine_platform ALIAS mythos_engine_platform)
    if(MYTHOS_LTO AND MYTHOS_IPO_OK)
        set_target_properties(mythos_engine_platform PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    endif()
    target_link_libraries(mythos_engine_platform PUBLIC mythos_engine ${MYTHOS_SDL_LIB})
    if(MSVC)
        target_compile_options(mythos_engine_platform PRIVATE /W4)
    else()
        target_compile_options(mythos_engine_platform PRIVATE -Wall -Wextra)
    endif()

    # SDL3 has no SDL3main library: a game includes <SDL3/SDL_main.h> in the source file that
    # defines main(). This target stays so games can keep linking mythos::sdl_main; it adds SDL.
    add_library(mythos_sdl_main INTERFACE)
    add_library(mythos::sdl_main ALIAS mythos_sdl_main)
    target_link_libraries(mythos_sdl_main INTERFACE ${MYTHOS_SDL_LIB})

    # The kind of executable a game should build on this platform: a GUI app on Windows
    # (no console window), an .app bundle on macOS, a plain binary elsewhere.
    set(MYTHOS_EXE_KIND "")
    if(WIN32)
        set(MYTHOS_EXE_KIND WIN32)
    elseif(APPLE)
        set(MYTHOS_EXE_KIND MACOSX_BUNDLE)
    endif()
    if(NOT PROJECT_IS_TOP_LEVEL)  # a top-level build has no parent scope to hand it to
        set(MYTHOS_EXE_KIND "${MYTHOS_EXE_KIND}" PARENT_SCOPE)
    endif()
endif()

# ---------------------------------------------------------------- mythos_pack
# Packs a game's assets folder into one file (see src/engine/core/vfs.h). A game's build runs it:
#   mythos_pack <out.pak> <assets dir> <key text>
add_executable(mythos_pack tools/pack/mythos_pack.cpp)
target_link_libraries(mythos_pack PRIVATE mythos_engine)
mythos_engine_warnings(mythos_pack)

# ---------------------------------------------------------------- tests
if(MYTHOS_BUILD_TESTS)
    enable_testing()
    add_executable(mythos_engine_tests tests/test_main.cpp)
    target_link_libraries(mythos_engine_tests PRIVATE mythos_engine)
    target_compile_definitions(mythos_engine_tests PRIVATE MYTHOS_TEST_ASSET_DIR="${MYTHOS_ENGINE_ASSET_DIR}")
    add_test(NAME engine COMMAND mythos_engine_tests)
endif()
