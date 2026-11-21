# Generates a .cpp defining the mythos::shaders strings from the files in SHADER_DIR.
# Script mode: cmake -DSHADER_DIR=<dir> -DOUT=<file> -P EmbedShaders.cmake
set(_map
        kWorldGeometryVert=vertex/world_geometry.vert
        kFullscreenQuadVert=vertex/fullscreen_quad.vert
        kToneMapAndGrainFrag=fragment/tone_map_and_grain.glsl
        kShadowDistanceFrag=fragment/shadow_distance.frag
        kWorldLightingFrag=fragment/world_lighting.frag
        kNightSkyFrag=fragment/night_sky.frag
        kLensAndFxaaFrag=fragment/lens_and_fxaa.frag
        kMirrorGlassVert=vertex/mirror_glass.vert
        kMirrorGlassFrag=fragment/mirror_glass.frag
        kGlassPaneVert=vertex/glass_pane.vert
        kGlassPaneFrag=fragment/glass_pane.frag
        kOverlayFrag=fragment/overlay.frag
        kBloomBlurFrag=fragment/bloom_blur.frag)
set(_out "// Generated from src/engine/platform/gl/shaders/ by cmake/EmbedShaders.cmake. Do not edit.\n#include \"engine/platform/gl/scene_shaders.h\"\n\nnamespace mythos::shaders {\n")
foreach (_e IN LISTS _map)
    string(REGEX MATCH "^([^=]+)=(.*)$" _ "${_e}")
    set(_name "${CMAKE_MATCH_1}")
    file(READ "${SHADER_DIR}/${CMAKE_MATCH_2}" _text)
    string(LENGTH "${_text}" _len)
    string(APPEND _out "\nconst char* const ${_name} =\n")
    set(_pos 0)
    while (_pos LESS _len)
        # MSVC limits one string literal to ~16 KB; adjacent literals concatenate.
        string(SUBSTRING "${_text}" ${_pos} 6000 _chunk)
        string(APPEND _out "    R\"GLSL(${_chunk})GLSL\"\n")
        math(EXPR _pos "${_pos} + 6000")
    endwhile ()
    string(APPEND _out "    ;\n")
endforeach ()
string(APPEND _out "\n}  // namespace mythos::shaders\n")
file(WRITE "${OUT}.tmp" "${_out}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUT}.tmp" "${OUT}")
