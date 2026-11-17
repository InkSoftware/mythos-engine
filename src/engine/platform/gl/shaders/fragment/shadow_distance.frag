#version 410 core
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec3 uLightPos;
uniform int uAlphaTest;
out vec4 outColor;
void main() {
    if (uAlphaTest == 1 && texture(uTex, vUV).a < 0.5) discard;
    outColor = vec4(length(vWorld - uLightPos), 0.0, 0.0, 1.0);
}
