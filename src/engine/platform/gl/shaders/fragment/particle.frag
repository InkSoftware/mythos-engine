#version 410 core
// A particle sprite, drawn without a texture: its shape is made here.
in vec2 vUV;
in vec4 vColor;
flat in float vShape;
out vec4 outColor;

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);
}

void main() {
    vec2 p = vUV * 2.0 - 1.0;
    float r = length(p);
    float a;
    if (vShape > 1.5) {          // smoke: a ragged edge
        float n = valueNoise(p * 2.6 + 3.1) * 0.65 + valueNoise(p * 5.3) * 0.35;
        a = (1.0 - smoothstep(0.25, 1.0, r + (n - 0.5) * 0.8)) * 0.85;
    } else if (vShape > 0.5) {   // spark: a hot core and a short falloff
        a = exp(-r * r * 7.0);
    } else {                     // soft: a round puff
        float s = clamp(1.0 - r, 0.0, 1.0);
        a = s * s;
    }
    a *= vColor.a;
    if (a < 0.004) discard;
    outColor = vec4(vColor.rgb, a);
}
