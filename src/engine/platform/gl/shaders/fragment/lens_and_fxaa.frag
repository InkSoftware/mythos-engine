#version 410 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uBlur;   // how far the picture moved this frame, in uv; (0, 0) = no blur
uniform float uLens;  // 0 off, 1 on
uniform float uFxaa;  // 1: smooth edges (not while blurring)
uniform vec2 uTexel;  // one texel of the scene, in uv
uniform sampler2D uBloomTex;  // the blurred bright parts of the picture, at a quarter size
uniform float uBloom;         // how much of it is added back (0 = none)
uniform float uTape;     // 1: the camcorder look (a game turns it off where it does not fit)
uniform float uTapeTime; // seconds on the tape: the timecode and the noise
uniform float uAspect;   // window width / height
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

vec3 sampleLensPlain(vec2 uv) {
    if (uLens < 0.5) return fetch(uv);
    vec2 c = uv - 0.5;
    float k = dot(c, c) * 0.020;  // fringing grows with the square of the distance from the centre
    return vec3(fetch(0.5 + c * (1.0 + k)).r, fetch(uv).g, fetch(0.5 + c * (1.0 - k)).b);
}


vec3 sceneColor(vec2 uv) { return sampleLensPlain(uv); }

// ---- The camcorder (VHS tape) look ----
float hash11(float n) { return fract(sin(n * 127.1) * 43758.5453); }
float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

vec3 sceneColor(vec2 uv);

// The picture as a tape holds it: sharp brightness, smeared colour.
vec3 tapeSample(vec2 uv) {
    float t = uTapeTime;
    // Slow wobble of the whole picture, and a tracking error that tears the bottom.
    uv.x += sin(uv.y * 9.0 + t * 1.7) * 0.0012 + (hash11(floor(t * 12.0)) - 0.5) * 0.0015;
    float band = smoothstep(0.10, 0.0, uv.y);  // vUV y = 0 is the bottom of the picture
    float row = floor(uv.y * 240.0);
    uv.x += band * (hash21(vec2(row, floor(t * 20.0))) - 0.5) * 0.05;
    // A tracking band that rolls up now and then.
    float cycle = fract(t / 9.0);
    float roll = cycle < 0.12 ? cycle / 0.12 : 2.0;
    float rd = uv.y - roll * 1.2 + 0.1;
    if (abs(rd) < 0.03) uv.x += sin(rd * 400.0 + t * 30.0) * 0.01 * (1.0 - abs(rd) / 0.03);
    vec3 c0 = sceneColor(uv);
    float y = luma(c0);
    vec3 smear = vec3(0.0);
    for (int i = -3; i <= 3; ++i) smear += sceneColor(uv + vec2(float(i) * 0.0022, 0.0));
    smear /= 7.0;
    vec3 bled = smear - vec3(luma(smear));  // the colour, smeared sideways
    vec3 col = vec3(y) + bled * 0.85;
    col.r = mix(col.r, sceneColor(uv + vec2(-0.0025, 0.0)).r, 0.5);
    col.b = mix(col.b, sceneColor(uv + vec2(0.0025, 0.0)).b, 0.5);
    return col;
}

// Seven segments: a top, b upper right, c lower right, d bottom, e lower left,
// f upper left, g middle (bits 0..6). p is inside the cell, 0..1 each way, y up.
float segBox(vec2 p, vec2 c, vec2 h) { vec2 d = abs(p - c) - h; return step(max(d.x, d.y), 0.0); }
float glyph(vec2 p, int m) {
    float r = 0.0;
    if ((m & 1) != 0) r = max(r, segBox(p, vec2(0.5, 0.92), vec2(0.34, 0.06)));
    if ((m & 2) != 0) r = max(r, segBox(p, vec2(0.9, 0.72), vec2(0.07, 0.24)));
    if ((m & 4) != 0) r = max(r, segBox(p, vec2(0.9, 0.28), vec2(0.07, 0.24)));
    if ((m & 8) != 0) r = max(r, segBox(p, vec2(0.5, 0.08), vec2(0.34, 0.06)));
    if ((m & 16) != 0) r = max(r, segBox(p, vec2(0.1, 0.28), vec2(0.07, 0.24)));
    if ((m & 32) != 0) r = max(r, segBox(p, vec2(0.1, 0.72), vec2(0.07, 0.24)));
    if ((m & 64) != 0) r = max(r, segBox(p, vec2(0.5, 0.5), vec2(0.34, 0.06)));
    return r;
}
// One glyph at slot `i` of a line starting at `o` (picture height = 1 unit); a
// slot is 0.6 of the glyph height wide. mask -1 is a colon.
float slot(vec2 p, vec2 o, float h, int i, int m) {
    vec2 q = (p - o - vec2(float(i) * h * 0.6, 0.0)) / vec2(h * 0.5, h);
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) return 0.0;
    if (m < 0) return max(segBox(q, vec2(0.5, 0.3), vec2(0.18, 0.07)), segBox(q, vec2(0.5, 0.7), vec2(0.18, 0.07)));
    return glyph(q, m);
}
int digitMask(int d) {
    const int M[10] = int[](63, 6, 91, 79, 102, 109, 125, 7, 127, 111);
    return M[clamp(d, 0, 9)];
}

// The camcorder's display: REC and its blinking dot, PLAY and the battery on
// top, the timecode below.
vec3 tapeHud(vec2 uv, vec3 col) {
    vec2 p = vec2(uv.x * uAspect, uv.y);  // height units, y up
    float w = uAspect;
    float h = 0.045;
    float ink = 0.0;
    vec3 red = vec3(0.95, 0.12, 0.1);
    // REC and the dot (top left).
    vec2 rec = vec2(0.06 + 0.05, 1.0 - 0.06 - h);
    ink = max(ink, slot(p, rec, h, 0, 80));
    ink = max(ink, slot(p, rec, h, 1, 121));
    ink = max(ink, slot(p, rec, h, 2, 57));
    float dotOn = mod(floor(uTapeTime), 2.0) < 1.0 ? 1.0 : 0.0;
    float dot_ = step(length(p - vec2(0.06 + 0.012, 1.0 - 0.06 - h * 0.5)), h * 0.42) * dotOn;
    // PLAY and a triangle (top right, left of the battery).
    vec2 play = vec2(w - 0.40, 1.0 - 0.06 - h);
    ink = max(ink, slot(p, play, h, 0, 115));
    ink = max(ink, slot(p, play, h, 1, 56));
    ink = max(ink, slot(p, play, h, 2, 119));
    ink = max(ink, slot(p, play, h, 3, 110));
    vec2 tq = (p - (play + vec2(4.0 * h * 0.6 + 0.01, 0.0))) / h;
    ink = max(ink, step(0.0, tq.x) * step(tq.x, 0.8) * step(abs(tq.y - 0.5), 0.5 - tq.x * 0.62));
    // The battery: an outline and three bars.
    vec2 bq = p - vec2(w - 0.17, 1.0 - 0.06 - h);
    float outline = segBox(bq, vec2(0.055, h * 0.5), vec2(0.06, h * 0.5)) - segBox(bq, vec2(0.055, h * 0.5), vec2(0.06 - 0.006, h * 0.5 - 0.006));
    float nub = segBox(bq, vec2(0.12 + 0.006, h * 0.5), vec2(0.006, h * 0.18));
    float bars = 0.0;
    for (int i = 0; i < 3; ++i) bars = max(bars, segBox(bq, vec2(0.02 + float(i) * 0.036, h * 0.5), vec2(0.014, h * 0.5 - 0.012)));
    ink = max(ink, max(max(outline, nub), bars));
    // The timecode (bottom right): H:MM:SS:FF.
    int total = int(uTapeTime);
    int frames = int(fract(uTapeTime) * 25.0);
    int hh = (total / 3600) % 10, mm = (total / 60) % 60, ss = total % 60;
    vec2 tc = vec2(w - 0.06 - 11.0 * h * 0.6, 0.06);
    ink = max(ink, slot(p, tc, h, 0, digitMask(hh)));
    ink = max(ink, slot(p, tc, h, 1, -1));
    ink = max(ink, slot(p, tc, h, 2, digitMask(mm / 10)));
    ink = max(ink, slot(p, tc, h, 3, digitMask(mm % 10)));
    ink = max(ink, slot(p, tc, h, 4, -1));
    ink = max(ink, slot(p, tc, h, 5, digitMask(ss / 10)));
    ink = max(ink, slot(p, tc, h, 6, digitMask(ss % 10)));
    ink = max(ink, slot(p, tc, h, 7, -1));
    ink = max(ink, slot(p, tc, h, 8, digitMask(frames / 10)));
    ink = max(ink, slot(p, tc, h, 9, digitMask(frames % 10)));
    col = mix(col, vec3(0.92, 0.92, 0.88), ink * 0.85);
    col = mix(col, red, dot_);
    return col;
}

vec3 tapeFinish(vec2 uv, vec3 col) {
    float t = uTapeTime;
    // Scanlines: soft dark lines about every other row of a 480-line picture.
    col *= 0.90 + 0.10 * sin(uv.y * 480.0 * 3.14159);
    // Washed colour: the blacks lifted, the colour pulled in, a slight green-blue cast.
    float y = luma(col);
    col = mix(vec3(y), col, 0.78);
    col = col * 0.92 + vec3(0.025, 0.03, 0.035);
    // Tape noise: fine grain, and the odd streak that drops out across a row.
    col += (hash21(uv * vec2(900.0, 500.0) + fract(t) * 37.0) - 0.5) * 0.08;
    float row = floor(uv.y * 360.0);
    float tick = floor(t * 15.0);
    float streak = step(0.9965, hash21(vec2(row, tick)));
    float at = hash21(vec2(row, tick + 3.0));
    col = mix(col, vec3(0.85), streak * step(abs(uv.x - at), 0.12 * hash21(vec2(tick, row))) * 0.7);
    // A slight vignette.
    vec2 c = uv - 0.5;
    col *= 1.0 - dot(c, c) * 0.9;
    return clamp(tapeHud(uv, col), 0.0, 1.0);
}

vec3 sampleLens(vec2 uv) { return uTape > 0.5 ? tapeSample(uv) : sampleLensPlain(uv); }

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
    if (uTape > 0.5) col = tapeFinish(vUV, col);
    outColor = vec4(col, 1.0);
}
