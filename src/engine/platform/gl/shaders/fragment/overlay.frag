#version 410 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 outColor;
void main() {
    outColor = texture(uTex, vUV);
}
