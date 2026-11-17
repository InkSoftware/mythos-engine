#version 410 core
in vec2 aPos;
in vec2 aUV;
out vec2 vUV;
uniform float uFlipY;  // 1 flips the image vertically (the overlay Surface has row 0 on top)
void main() {
    vUV = vec2(aUV.x, mix(aUV.y, 1.0 - aUV.y, uFlipY));
    gl_Position = vec4(aPos, 0.0, 1.0);
}
