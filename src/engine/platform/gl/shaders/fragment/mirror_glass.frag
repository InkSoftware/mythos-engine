#version 410 core
// The glass of a mirror: the picture the reflected pass drew, found by where this pixel is on the screen.
uniform sampler2D uTex;
uniform vec2 uRes;      // size of the main target
uniform vec3 uTint;
uniform float uAlpha;
out vec4 outColor;
void main() {
    vec3 c = texture(uTex, gl_FragCoord.xy / uRes).rgb * uTint;
    outColor = vec4(c, uAlpha);
}
