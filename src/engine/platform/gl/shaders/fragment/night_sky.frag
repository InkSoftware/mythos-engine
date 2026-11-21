#version 410 core
in vec2 vUV;
uniform vec3 uFwd;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec2 uTan;      // tan of half fov, x and y
uniform float uTime;
uniform vec3 uMoonDir;
uniform float uLightning;
uniform vec3 uFogColor;
out vec4 outColor;

vec3 finishPixel(vec3 linearCol, float t);

float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1, 0)), u.x),
            mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * vnoise(p);
        p = p * 2.03 + vec2(17.3, 9.1);
        a *= 0.5;
    }
    return v;
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 dir = normalize(uFwd + ndc.x * uTan.x * uRight + ndc.y * uTan.y * uUp);
    float up = clamp(dir.y, -0.05, 1.0);

    // Gradient: a deep blue horizon fading into near-black overhead.
    vec3 col = mix(vec3(0.030, 0.045, 0.085), vec3(0.004, 0.006, 0.016), smoothstep(0.0, 0.6, up));

    // Stars: tiny cells, a few of them lit, twinkling slowly.
    vec2 cell = dir.xz / (dir.y + 0.25) * 90.0;
    float star = step(0.9965, hash2(floor(cell)));
    float tw = 0.6 + 0.4 * sin(uTime * (1.5 + 3.0 * hash2(floor(cell) + 7.0)) + hash2(floor(cell) + 3.0) * 6.28);
    float starVis = star * tw * smoothstep(0.02, 0.15, dir.y);

    // The moon: a bright disc with craters, a shaded limb and a wide halo.
    float md = dot(dir, uMoonDir);
    float disc = smoothstep(0.9982, 0.9987, md);
    float crater = 0.80 + 0.20 * fbm(dir.xz * 160.0 + 31.0);
    float limb = 0.70 + 0.30 * smoothstep(0.9982, 0.9995, md);
    vec3 moonCol = vec3(0.95, 0.93, 0.85) * 2.2 * crater * limb;
    float halo = pow(clamp(md, 0.0, 1.0), 300.0) * 0.6 + pow(clamp(md, 0.0, 1.0), 30.0) * 0.10;

    // Clouds: slow fbm sheets, faintly lit from the moon's side of the sky.
    float cloud = 0.0;
    if (dir.y > 0.01) {
        vec2 p = dir.xz / (dir.y + 0.18) * 1.4 + vec2(uTime * 0.014, uTime * 0.006);
        float f = fbm(p);
        cloud = smoothstep(0.52, 0.78, f) * smoothstep(0.01, 0.12, dir.y);
    }
    float moonGlow = clamp(md * 0.5 + 0.5, 0.0, 1.0);
    vec3 cloudCol = mix(vec3(0.020, 0.026, 0.042), vec3(0.075, 0.085, 0.110), moonGlow);

    col += vec3(0.75, 0.78, 0.85) * starVis * (1.0 - cloud);
    col += vec3(0.55, 0.60, 0.72) * halo * (1.0 - cloud * 0.7);
    col = mix(col, moonCol, disc * (1.0 - cloud * 0.85));
    col = mix(col, cloudCol, cloud);

    // Lightning lights the clouds from within, and the whole sky a little.
    col += vec3(0.55, 0.62, 0.90) * uLightning * (0.35 + 0.65 * cloud);

    // Haze toward the horizon so the sky meets the fogged ground.
    col = mix(col, uFogColor, smoothstep(0.16, -0.05, dir.y));
    outColor = vec4(finishPixel(col, uTime), 1.0);
}
