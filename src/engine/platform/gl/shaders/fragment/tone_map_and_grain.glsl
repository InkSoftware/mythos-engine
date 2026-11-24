uniform vec2 uRes;
uniform float uGrain;
uniform float uExposure;  // linear light multiplier before the curve (1 = unchanged)
uniform int uTonemap;     // 0 ACES, 1 filmic (Hable), 2 clamp
uniform float uVignette;  // 0 none, 1 the engine's look
uniform int uHdr;         // 1: a probe capture, linear light out (no exposure, curve, vignette or grain)
float hashT(vec2 p, float t) {
    return fract(sin(dot(p, vec2(12.9898, 78.233)) + t * 17.0) * 43758.5453);
}
vec3 hableCurve(vec3 x) {
    return ((x * (0.15 * x + 0.05) + 0.004) / (x * (0.15 * x + 0.5) + 0.06)) - 0.02 / 0.30;
}
vec3 finishPixel(vec3 linearCol, float t) {
    if (uHdr == 1) return linearCol;
    vec3 x = linearCol * uExposure;
    vec3 col;
    if (uTonemap == 1) {
        col = clamp(hableCurve(x * 2.0) / hableCurve(vec3(11.2)), 0.0, 1.0);
    } else if (uTonemap == 2) {
        col = clamp(x, 0.0, 1.0);
    } else {
        // ACES filmic approximation (Narkowicz).
        col = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    }
    col = pow(col, vec3(1.0 / 2.2));
    vec2 q = gl_FragCoord.xy / uRes * 2.0 - 1.0;
    col *= mix(1.0, 0.55 + 0.45 * smoothstep(1.55, 0.35, length(q)), uVignette);   // vignette
    col += (hashT(gl_FragCoord.xy, t) - 0.5) * 0.02 * uGrain; // film grain (a setting)
    return col;
}
