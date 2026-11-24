#version 410 core
// A pane of glass in the view: a flat tint blended over what is behind it, more opaque and sky-coloured at a grazing
// angle (a reflection), and with beads of water on it when uDrops > 0 (rain, or the dew of a misty night).
in vec3 vWorld;
in vec2 vUV;
uniform vec3 uTint;
uniform float uAlpha;
uniform float uDrops;   // 0 dry .. 1 wet
uniform float uTime;
uniform vec3 uEye;
uniform vec3 uSky;      // the sky in the glass (the cold night by default, the sun-aware sky outdoors by day)
out vec4 outColor;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    float fres = pow(1.0 - abs(dot(n, normalize(uEye - vWorld))), 3.0);
    vec3 col = mix(uTint, uSky, fres);   // the sky in the glass
    float a = uAlpha + fres * 0.30;
    if (uDrops > 0.0) {
        // Beads of water on a grid of 5 cm cells, each a jittered round drop; the wetter, the more cells hold one, and a
        // few slide down slowly.
        const float cell = 0.05;
        vec2 g = vUV / cell;
        vec2 id = floor(g);
        float h = hash(id);
        if (h < uDrops) {
            float slide = fract(hash(id + 7.0) * 13.0 + uTime * 0.02 * (0.3 + hash(id + 3.0)));
            vec2 c = vec2(0.3 + 0.4 * hash(id + 1.0), 0.3 + 0.4 * fract(h * 7.0));
            vec2 f = fract(g) - c;
            float r = 0.10 + 0.17 * hash(id + 5.0);
            float d = length(f);
            float bead = smoothstep(r, r * 0.55, d);
            float rim = smoothstep(r * 0.55, r, d) * smoothstep(r * 1.15, r, d);
            col = mix(col, vec3(0.8, 0.86, 0.94), bead * 0.6);
            a = max(a, bead * 0.34 + rim * 0.2 + slide * 0.0);
        }
    }
    outColor = vec4(col, clamp(a, 0.0, 0.8));
}
