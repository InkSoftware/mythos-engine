cmake_minimum_required(VERSION 3.22)

# macOS: set the deployment target before project().
if (APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "")
endif ()

project(template_game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif ()

# The engine. Inside the engine's checkout (add_subdirectory from its top
# level) the targets already exist. Standalone, this takes the folder above
# this one; in a copy of this template outside the engine, point
# MYTHOS_ENGINE_DIR at a checkout or replace this with a GIT_REPOSITORY.
if (NOT TARGET mythos_engine)
    set(MYTHOS_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "mythos-engine checkout")
    include(FetchContent)
    FetchContent_Declare(mythos_engine SOURCE_DIR "${MYTHOS_ENGINE_DIR}")
    set(MYTHOS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MYTHOS_BUILD_EDITOR OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mythos_engine)
endif ()

# The rules: no SDL, so they are tested headless.
add_library(template_game_rules STATIC src/game.cpp)
target_include_directories(template_game_rules PUBLIC src)
target_link_libraries(template_game_rules PUBLIC mythos::engine)

# The game. MYTHOS_EXE_KIND is WIN32 / MACOSX_BUNDLE / plain, set by the engine.
if (TARGET mythos::engine_platform)
    add_executable(template_game ${MYTHOS_EXE_KIND} src/main.cpp)
    target_link_libraries(template_game PRIVATE template_game_rules mythos::engine_platform mythos::sdl_main)
    # Development paths: the engine's assets and this game's. A shipped game
    # packs or copies them next to the executable instead (see README.md).
    target_compile_definitions(template_game PRIVATE
            TEMPLATE_ENGINE_ASSETS="${MYTHOS_ENGINE_ASSET_DIR}"
            TEMPLATE_ENGINE_SHADERS="${MYTHOS_ENGINE_SHADER_DIR}"
            TEMPLATE_GAME_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif ()

# The baked light for --baked: the engine's bake tool turns the lamps file into
# a probe grid beside the level. (Checked in too, so a build without the tool
# still runs; the test re-bakes it into the build folder.)
if (TARGET mythos_bake_probes)
    add_test(NAME template_game_bake_probes
            COMMAND mythos_bake_probes
                    ${CMAKE_CURRENT_SOURCE_DIR}/assets/levels/hall.txt
                    ${CMAKE_CURRENT_SOURCE_DIR}/assets/levels/hall.lamps
                    ${CMAKE_CURRENT_BINARY_DIR}/hall.probes
                    --tiles ${CMAKE_CURRENT_SOURCE_DIR}/assets/data/tiles.json)
    set_tests_properties(template_game_bake_probes PROPERTIES PASS_REGULAR_EXPRESSION "baked 2 lamp")
endif ()

# Headless tests of the rules (ctest).
add_executable(template_game_tests tests/game_test.cpp)
target_link_libraries(template_game_tests PRIVATE template_game_rules)
target_compile_definitions(template_game_tests PRIVATE
        TEMPLATE_ENGINE_ASSETS="${MYTHOS_ENGINE_ASSET_DIR}"
        TEMPLATE_GAME_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets")
enable_testing()
add_test(NAME template_game_tests COMMAND template_game_tests)
