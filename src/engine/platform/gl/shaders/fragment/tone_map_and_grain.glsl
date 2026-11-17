uniform vec2 uRes;
uniform float uGrain;
float hashT(vec2 p, float t) {
    return fract(sin(dot(p, vec2(12.9898, 78.233)) + t * 17.0) * 43758.5453);
}
vec3 finishPixel(vec3 linearCol, float t) {
    // ACES filmic approximation (Narkowicz).
    vec3 x = linearCol;
    vec3 col = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    col = pow(col, vec3(1.0 / 2.2));
    vec2 q = gl_FragCoord.xy / uRes * 2.0 - 1.0;
    col *= 0.55 + 0.45 * smoothstep(1.55, 0.35, length(q));   // vignette
    col += (hashT(gl_FragCoord.xy, t) - 0.5) * 0.02 * uGrain; // film grain (a setting)
    return col;
}
