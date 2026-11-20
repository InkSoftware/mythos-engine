#version 410 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uBlur;   // how far the picture moved this frame, in uv; (0, 0) = no blur
uniform float uLens;  // 0 off, 1 on
uniform float uFxaa;  // 1: smooth edges (not while blurring)
uniform vec2 uTexel;  // one texel of the scene, in uv
uniform sampler2D uBloomTex;  // the blurred bright parts of the picture, at a quarter size
uniform float uBloom;         // how much of it is added back (0 = none)
out vec4 outColor;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// A light FXAA: find the edge's direction from the corner lumas and average along it.
vec3 fxaa(vec2 uv) {
    vec3 m = texture(uTex, uv).rgb;
    vec3 nw = texture(uTex, uv + vec2(-1.0, -1.0) * uTexel).rgb;
    vec3 ne = texture(uTex, uv + vec2(1.0, -1.0) * uTexel).rgb;
    vec3 sw = texture(uTex, uv + vec2(-1.0, 1.0) * uTexel).rgb;
    vec3 se = texture(uTex, uv + vec2(1.0, 1.0) * uTexel).rgb;
    float lM = luma(m), lNW = luma(nw), lNE = luma(ne), lSW = luma(sw), lSE = luma(se);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.03, lMax * 0.125)) return m;  // no edge here
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -8.0, 8.0) * uTexel;
    vec3 a = 0.5 * (texture(uTex, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uTex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(uTex, uv + dir * -0.5).rgb + texture(uTex, uv + dir * 0.5).rgb);
    float lB = luma(b);
    return (lB < lMin || lB > lMax) ? a : b;
}

vec3 fetch(vec2 uv) { return uFxaa > 0.5 ? fxaa(uv) : texture(uTex, uv).rgb; }

vec3 sampleLens(vec2 uv) {
    if (uLens < 0.5) return fetch(uv);
    vec2 c = uv - 0.5;
    float k = dot(c, c) * 0.020;  // fringing grows with the square of the distance from the centre
    return vec3(fetch(0.5 + c * (1.0 + k)).r, fetch(uv).g, fetch(0.5 + c * (1.0 - k)).b);
}

void main() {
    vec3 col;
    if (dot(uBlur, uBlur) > 1e-9) {
        col = vec3(0.0);
        for (int i = 0; i < 12; ++i) col += sampleLens(vUV + uBlur * ((float(i) + 0.5) / 12.0 - 0.5));
        col /= 12.0;
    } else {
        col = sampleLens(vUV);
    }
    if (uBloom > 0.0) {  // light bleeding round bright things: the lamps, the lit dial, the beam on a wall
        vec3 glow = texture(uBloomTex, vUV).rgb;
        col = 1.0 - (1.0 - col) * (1.0 - glow * uBloom);  // screen blend: never past white
    }
    outColor = vec4(col, 1.0);
}
