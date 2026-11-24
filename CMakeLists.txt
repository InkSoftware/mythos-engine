cmake_minimum_required(VERSION 3.22)

# Oldest macOS the binaries run on: the releases Apple still patches (the current one and the two before it,
# so 15 while 27 is current; raise it as Apple drops one). Without this the build targets the SDK's own OS, i.e.
# only the newest macOS. It must be set before project(), so it only applies when the engine is the top-level
# project: a game sets its own.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")
endif()

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
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

option(MYTHOS_BUILD_PLATFORM "Build the SDL3 platform layer (mythos::engine_platform)" ON)
option(MYTHOS_BUILD_EDITOR   "Build the level editor (mythos::editor, and MythosEditor when top level; fetches Dear ImGui)" ${PROJECT_IS_TOP_LEVEL})
option(MYTHOS_BUILD_TEMPLATE_GAME "Build template-game/, the starting point for a new game (it keeps the engine's API honest)" ${PROJECT_IS_TOP_LEVEL})
option(MYTHOS_BUILD_TESTS "Build the engine's unit tests" ${PROJECT_IS_TOP_LEVEL})
option(MYTHOS_FETCH_SDL      "Download SDL3 when no installed copy is found" ON)
option(MYTHOS_LTO            "Link-time optimization for Release builds of the engine libraries, when the compiler supports it" ON)
option(MYTHOS_WARNINGS_AS_ERRORS "Treat warnings in the engine's own code as errors (CI sets it; third-party code is unaffected)" OFF)
option(MYTHOS_PORTABLE       "Link SDL statically so the app runs without SDL installed (for distribution)" OFF)
set(MYTHOS_SDL_VERSION "3.2.28" CACHE STRING "SDL3 version fetched when MYTHOS_FETCH_SDL is ON")
set(MYTHOS_IMGUI_TAG "v1.92.9-docking" CACHE STRING "The Dear ImGui tag (docking branch) the level editor is built on")

# ---------------------------------------------------------------- mythos::engine
add_library(mythos_engine STATIC
    src/engine/audio/sound_bank.cpp
    src/engine/audio/spatial.cpp
    src/engine/core/cvar.cpp
    src/engine/core/fileio.cpp
    src/engine/core/json.cpp
    src/engine/core/mat4.cpp
    src/engine/core/tween.cpp
    src/engine/core/vfs.cpp
    src/engine/data/achievements.cpp
    src/engine/data/credits.cpp
    src/engine/data/engine_strings.cpp
    src/engine/data/i18n.cpp
    src/engine/data/save.cpp
    src/engine/data/translation.cpp
    src/engine/editor/adapter.cpp
    src/engine/editor/level_doc.cpp
    src/engine/editor/project.cpp
    src/engine/gfx/anim_state.cpp
    src/engine/gfx/font.cpp
    src/engine/gfx/gltf.cpp
    src/engine/gfx/grade.cpp
    src/engine/gfx/lod.cpp
    src/engine/gfx/materials.cpp
    src/engine/gfx/mesh.cpp
    src/engine/gfx/skeleton.cpp
    src/engine/gfx/sky.cpp
    src/engine/gfx/model.cpp
    src/engine/gfx/normalmap.cpp
    src/engine/gfx/particles.cpp
    src/engine/gfx/pbr.cpp
    src/engine/gfx/stb_image_impl.cpp
    src/engine/gfx/stb_truetype_impl.cpp
    src/engine/gfx/text.cpp
    src/engine/gfx/textures.cpp
    src/engine/ui/input_map.cpp
    src/engine/ui/debug_console.cpp
    src/engine/ui/debug_overlay.cpp
    src/engine/ui/menu.cpp
    src/engine/ui/paging.cpp
    src/engine/ui/settings.cpp
    src/engine/ui/settings_cvars.cpp
    src/engine/ui/settings_ui.cpp
    src/engine/ui/ui.cpp
    src/engine/world/ai.cpp
    src/engine/world/interact.cpp
    src/engine/world/level.cpp
    src/engine/world/mirror.cpp
    src/engine/world/movement.cpp
    src/engine/world/body.cpp
    src/engine/world/free_cam.cpp
    src/engine/world/camera_fx.cpp
    src/engine/world/player.cpp
    src/engine/world/room_doors.cpp
    src/engine/world/sound_path.cpp
    src/engine/world/save_state.cpp
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
target_compile_features(mythos_engine PUBLIC cxx_std_23)
if(MSVC)
    # Sources and headers are UTF-8 (Greek strings, the settings page's glyphs); say so, for the games that include them too.
    target_compile_options(mythos_engine PUBLIC /utf-8)
endif()

if(MSVC)
    add_compile_options(/MP)  # compile the files of a project in parallel
endif()
function(mythos_engine_werror target)
    if(MYTHOS_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        else()
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
function(mythos_engine_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    mythos_engine_werror(${target})
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
            if(APPLE)
                # SDL's Cocoa backend still uses APIs Apple has deprecated (kUTType*, CVDisplayLink*, ...);
                # that is SDL's code, not ours, so keep it from burying our own warnings.
                target_compile_options(SDL3-static PRIVATE -Wno-deprecated-declarations)
            endif()
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
    mythos_engine_werror(mythos_engine_platform)

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

# ---------------------------------------------------------------- mythos::editor (the level editor)
# A native level editor on SDL3, OpenGL and Dear ImGui (docking): a 3D viewport, outliner, inspector, tile
# palette, asset browser and a game preview. It knows the engine's level files and tile legend; a game adds
# its own things through an mythos::editor::Adapter (src/engine/editor/adapter.h) and runs the editor with
# RunEditor() from its own main(). MythosEditor is the editor with no game attached.
if(MYTHOS_BUILD_PLATFORM AND MYTHOS_BUILD_EDITOR)
    include(FetchContent)
    FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG ${MYTHOS_IMGUI_TAG}
                         GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui)  # Dear ImGui has no CMakeLists: this only downloads it
    add_library(mythos_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(mythos_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(mythos_imgui PUBLIC mythos_engine_platform ${CMAKE_DL_LIBS})
    if(APPLE)
        target_link_libraries(mythos_imgui PUBLIC "-framework OpenGL")
    endif()
    target_compile_definitions(mythos_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    if(MSVC)
        target_compile_options(mythos_imgui PRIVATE /w)
    else()
        target_compile_options(mythos_imgui PRIVATE -w)
    endif()

    add_library(mythos_editor STATIC
        src/engine/platform/editor/editor_app.cpp
        src/engine/platform/editor/run.cpp
        src/engine/platform/editor/ui_panels.cpp
        src/engine/platform/editor/viewport.cpp)
    add_library(mythos::editor ALIAS mythos_editor)
    target_link_libraries(mythos_editor PUBLIC mythos_engine mythos_engine_platform mythos_imgui)
    if(MSVC)
        target_compile_options(mythos_editor PRIVATE /W4)
    else()
        target_compile_options(mythos_editor PRIVATE -Wall -Wextra)
    endif()
    mythos_engine_werror(mythos_editor)

    if(PROJECT_IS_TOP_LEVEL)
        add_executable(mythos_editor_app ${MYTHOS_EXE_KIND} tools/editor/main.cpp)
        set_target_properties(mythos_editor_app PROPERTIES OUTPUT_NAME "MythosEditor"
            MACOSX_BUNDLE_BUNDLE_NAME "MythosEditor" MACOSX_BUNDLE_GUI_IDENTIFIER "dev.mythos.editor")
        target_link_libraries(mythos_editor_app PRIVATE mythos_editor mythos::sdl_main)
        mythos_engine_werror(mythos_editor_app)
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
    mythos_engine_warnings(mythos_engine_tests)
    target_compile_definitions(mythos_engine_tests PRIVATE MYTHOS_TEST_ASSET_DIR="${MYTHOS_ENGINE_ASSET_DIR}")
    add_test(NAME engine COMMAND mythos_engine_tests)
endif()

# ---------------------------------------------------------------- template game
# A small game on the engine, the starting point for new ones. Built with the
# engine so a change that breaks it is seen at once (see CLAUDE.md).
if(MYTHOS_BUILD_TEMPLATE_GAME)
    enable_testing()
    add_subdirectory(template-game)
endif()
