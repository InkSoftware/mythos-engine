#version 410 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uDir;         // one blur step, in uv (a texel along x, or along y)
uniform float uThreshold;  // > 0: first pass, keep only what is brighter than this (the picture is already tone mapped)
out vec4 outColor;

vec3 bright(vec3 c) {
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    return c * (max(l - uThreshold, 0.0) / max(l, 1e-4));
}

vec3 tap(vec2 uv) {
    vec3 c = texture(uTex, uv).rgb;
    return uThreshold > 0.0 ? bright(c) : c;
}

// A 9-tap gaussian along uDir. The first pass also minifies (the source is larger than this target), which the linear
// filter and the wide step turn into a soft box.
void main() {
    const float w[5] = float[](0.2270270, 0.1945946, 0.1216216, 0.0540540, 0.0162162);
    vec3 c = tap(vUV) * w[0];
    for (int i = 1; i < 5; ++i) {
        c += tap(vUV + uDir * float(i)) * w[i];
        c += tap(vUV - uDir * float(i)) * w[i];
    }
    outColor = vec4(c, 1.0);
}
