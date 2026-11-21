#version 410 core
// A pane of glass in the view: a flat tint blended over what is behind it.
uniform vec3 uTint;
uniform float uAlpha;
out vec4 outColor;
void main() {
    outColor = vec4(uTint, uAlpha);
}
