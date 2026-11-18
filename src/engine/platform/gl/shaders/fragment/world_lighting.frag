#version 410 core
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
uniform sampler2D uTex;
uniform sampler2D uNormalTex;
uniform sampler2D uMaterialTex;  // R metal, G ambient occlusion
uniform vec3 uCam;
uniform vec3 uFlashDir;
uniform float uFlashOn;
uniform float uLighter;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uTime;
uniform int uEmissive;
uniform int uMaterials;         // 0 flat look, 1 specular + metal + occlusion, 2 also reflections
uniform int uAlphaTest;
uniform int uOutdoor;
uniform vec3 uMoonDir;
uniform vec3 uAmbientBase;
uniform vec3 uAmbientNear;
uniform int uLightCount;
uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform float uLightRadius[8];
uniform float uShadowTexel;     // angular size of one shadow-map texel (2 / face size)
uniform int uShadowCount;       // the first uShadowCount lamps cast shadows
uniform samplerCube uShadow0;
uniform samplerCube uShadow1;
uniform samplerCube uShadow2;
uniform samplerCube uShadow3;
uniform samplerCube uShadowHand;
uniform vec4 uClip;             // plane: the mirror pass drops what is behind the glass
uniform int uHandShadow;        // 1: the hand light casts shadows from uHandPos
uniform vec3 uHandPos;
uniform int uHandFaces;         // bit i: face i of the hand light's cube was drawn this frame
out vec4 outColor;

vec3 finishPixel(vec3 linearCol, float t);

float storedDistance(int i, vec3 dir) {
    if (i == 0) return texture(uShadow0, dir).r;
    if (i == 1) return texture(uShadow1, dir).r;
    if (i == 2) return texture(uShadow2, dir).r;
    if (i == 3) return texture(uShadow3, dir).r;
    return texture(uShadowHand, dir).r;  // 4: the hand light
}

// 1 lit, 0 in shadow. The bias follows the size of a shadow texel at that distance (and the
// slope of the surface), so shadows stay tight against what casts them; the lookup is a rotated
// disc of taps a couple of texels wide, which turns the texel blocks into a soft edge.
float shadowFactor(int i, vec3 lightPos, vec3 n) {
    vec3 toP = vWorld - lightPos;
    float d0 = max(length(toP), 0.001);
    float cosT = abs(dot(n, toP)) / d0;
    float sinT = sqrt(max(1.0 - cosT * cosT, 0.0));
    float texel = d0 * uShadowTexel;                       // world size of one texel here
    if (dot(n, toP) > 0.0) n = -n;                         // winding is arbitrary: face the lamp
    vec3 q = vWorld + n * (texel * (1.0 + 3.0 * sinT) + 0.005) - lightPos;
    float dq = length(q);
    float ref = dq - (texel * 1.5 + 0.01);
    vec3 dir = q / max(dq, 0.001);
    if (i == 4) {  // the hand cube only draws the faces its beam reaches; elsewhere there is no shadow
        vec3 a = abs(dir);
        int face = (a.x >= a.y && a.x >= a.z) ? (dir.x > 0.0 ? 0 : 1) : ((a.y >= a.z) ? (dir.y > 0.0 ? 2 : 3) : (dir.z > 0.0 ? 4 : 5));
        if (((uHandFaces >> face) & 1) == 0) return 1.0;
    }
    vec3 t = normalize(cross(dir, abs(dir.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(dir, t);
    float spin = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) * 6.2831853;
    // The taps turn by the golden angle each step: one cos / sin here, then a rotation per tap.
    const float cg = -0.7390089, sg = 0.6736956;  // cos / sin of 2.3999632
    float c = cos(spin), s = sin(spin);
    int taps = dq < 6.0 ? 12 : 6;
    float invTaps = 1.0 / float(taps);
    // Eight taps up close, where the edge is seen; half as many from afar, where it is small.
    float lit = 0.0;
    for (int k = 0; k < taps; ++k) {
        float rad = sqrt((float(k) + 0.5) * invTaps) * uShadowTexel * 3.5;
        vec3 sd = dir + (t * c + b * s) * rad;
        lit += ref > storedDistance(i, sd) ? 0.0 : 1.0;
        float c2 = c * cg - s * sg;
        s = s * cg + c * sg;
        c = c2;
    }
    // Never fully dark: light bounces, so a shadow keeps a share of the light (the soft fill).
    return mix(0.30, 1.0, lit * invTaps);
}

// Tilts the surface normal by the texture's normal map. The tangent frame comes from the screen-space
// derivatives of the position and the uv (no tangents in the vertices), so it follows any model.
float gGloss = 0.0;  // the normal map's alpha: 1 - roughness (0 = matt), set by bumpNormal
float gMetal = 0.0, gAo = 1.0;  // the material map, set by main
float gRough = 1.0;             // the roughness the specular uses
vec3 gF0 = vec3(0.04);          // reflectance at normal incidence: 0.04 for a dielectric, the albedo for a metal
vec3 bumpNormal(vec3 n) {
    vec4 map = texture(uNormalTex, vUV);
    gGloss = map.a;
    vec3 m = map.xyz * 2.0 - 1.0;
    vec3 dp1 = dFdx(vWorld), dp2 = dFdy(vWorld);
    vec2 duv1 = dFdx(vUV), duv2 = dFdy(vUV);
    vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    return normalize(T * invmax * m.x + B * invmax * m.y + n * m.z);
}

// Specular: GGX distribution, a Schlick-Smith visibility and Schlick Fresnel, times n.l. F0 is 0.04 for a
// dielectric and the albedo for a metal. Zero for a matt surface (no roughness map, no metal), so old art looks as
// it did.
vec3 specular(vec3 n, vec3 v, vec3 l) {
    if (gRough >= 1.0) return vec3(0.0);
    vec3 h = normalize(l + v);
    float nl = abs(dot(n, l)), nv = max(abs(dot(n, v)), 0.02), nh = abs(dot(n, h)), vh = clamp(dot(v, h), 0.0, 1.0);
    float a2 = gRough * gRough * gRough * gRough;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    float ndf = a2 / (3.14159265 * d * d);
    float k = (gRough + 1.0) * (gRough + 1.0) * 0.125;
    float vis = 0.25 / (mix(nl, 1.0, k) * mix(nv, 1.0, k));
    vec3 fres = gF0 + (1.0 - gF0) * pow(1.0 - vh, 5.0);
    return min(ndf * vis * nl, 8.0) * fres;
}

// What a shiny surface reflects of the room: there are no probes, so the environment is the ambient light of the
// era (brighter overhead, darker underfoot), or the night sky outdoors. Karis' analytic environment BRDF gives
// the split-sum scale and bias, and the baked occlusion dims the reflection in crevices.
vec3 environmentSpecular(vec3 n, vec3 v, vec3 ambient) {
    if (gRough >= 1.0 || uMaterials < 2) return vec3(0.0);
    float nv = max(abs(dot(n, v)), 0.02);
    vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022), c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = gRough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    float occ = clamp(pow(nv + gAo, exp2(-16.0 * gRough - 1.0)) - 1.0 + gAo, 0.0, 1.0);
    vec3 refl = reflect(-v, n);
    float sky = mix(0.55, 1.45, clamp(refl.y * 0.5 + 0.5, 0.0, 1.0));
    return ambient * sky * (gF0 * ab.x + ab.y) * occ;
}

void main() {
    if (dot(vWorld, uClip.xyz) + uClip.w < 0.0) discard;
    vec4 tex = texture(uTex, vUV);
    if (uAlphaTest == 1 && tex.a < 0.5) discard;
    vec3 albedo = tex.rgb;  // the texture is SRGB8_ALPHA8: sampling already gives linear

    float dist = length(uCam - vWorld);
    vec3 col;
    if (uEmissive == 1) {
        col = albedo * 3.0;  // screens and eyes glow
    } else {
        vec3 geo = normalize(vNormal);   // the surface itself: shadows are cast from it
        vec3 n = bumpNormal(geo);        // what the light sees, grain and mortar included
        vec2 mat = texture(uMaterialTex, vUV).rg;
        if (uMaterials == 0) {
            gGloss = 0.0;
            mat = vec2(0.0, 1.0);
        }
        gMetal = mat.r;
        gAo = mat.g;
        gRough = (gGloss < 0.02 && gMetal < 0.02) ? 1.0 : (gGloss < 0.02 ? 0.6 : clamp(1.0 - gGloss, 0.08, 1.0));
        gF0 = mix(vec3(0.04), albedo, gMetal);
        vec3 toCam = normalize(uCam - vWorld);
        float facing = abs(dot(n, toCam));  // winding of models is arbitrary

        vec3 spec = vec3(0.0);
        vec3 light = vec3(0.0);  // direct light
        vec3 ambient;
        if (uOutdoor == 1) {
            // Cold moonlight plus a faint blue sky ambient.
            float ndl = abs(dot(n, uMoonDir));
            vec3 moon = vec3(0.30, 0.38, 0.58) * 0.55;
            light = moon * ndl;
            spec += moon * specular(n, toCam, uMoonDir);
            ambient = vec3(0.040, 0.052, 0.085);
        } else {
            // A dark interior: dim ambient, slightly stronger near the player (both per era).
            ambient = uAmbientBase + uAmbientNear * clamp(1.0 - dist / 3.5, 0.0, 1.0);
        }

        // Lamps: small positional lights, fading out smoothly at their radius.
        for (int i = 0; i < uLightCount; ++i) {
            vec3 toL = uLightPos[i] - vWorld;
            float d = length(toL);
            float att = clamp(1.0 - d / uLightRadius[i], 0.0, 1.0);
            att *= att;
            float ndl = 0.35 + 0.65 * abs(dot(n, toL / max(d, 0.001)));
            float lit = (i < uShadowCount && att > 0.0) ? shadowFactor(i, uLightPos[i], geo) : 1.0;
            light += uLightColor[i] * att * ndl * lit;
            spec += uLightColor[i] * att * lit * specular(n, toCam, toL / max(d, 0.001));
        }

        // The hand light: the torch is a tight warm cone straight ahead; the lighter is a
        // small flame at the fist - wide, short, orange, and never quite steady.
        if (uFlashOn > 0.5) {
            vec3 viewDir = normalize(vWorld - uCam);
            float cone = smoothstep(mix(0.91, 0.30, uLighter), mix(0.985, 0.85, uLighter), dot(viewDir, normalize(uFlashDir)));
            float falloff = 1.0 / (1.0 + mix(0.38, 0.95, uLighter) * dist + mix(0.10, 0.55, uLighter) * dist * dist);
            float wave = sin(uTime * 23.0) * sin(uTime * 7.1);
            float flicker = mix(0.96 + 0.04 * wave, 0.82 + 0.18 * wave * sin(uTime * 31.0), uLighter);
            vec3 tint = mix(vec3(1.00, 0.80, 0.55), vec3(1.00, 0.62, 0.30), uLighter);
            float lit = 1.0;
            if (uHandShadow == 1 && cone * falloff > 0.002) lit = shadowFactor(4, uHandPos, geo);
            light += tint * mix(2.4, 2.0, uLighter) * cone * falloff * (0.35 + 0.65 * facing) * flicker * lit;
            spec += tint * mix(2.4, 2.0, uLighter) * cone * falloff * flicker * lit * specular(n, toCam, toCam);
        }
        // Ambient light is what baked occlusion dims; a metal has no diffuse colour, only reflection.
        col = albedo * (1.0 - gMetal) * (ambient * gAo + light) + spec + environmentSpecular(n, toCam, ambient * 2.5);
    }

    float fog = 1.0 - exp(-uFogDensity * dist);
    col = mix(col, uFogColor, clamp(fog, 0.0, 1.0));
    outColor = vec4(finishPixel(col, uTime), 1.0);
}
