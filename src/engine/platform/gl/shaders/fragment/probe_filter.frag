#version 410 core
// Prefilters a captured room for glossy reflections: one face of one mip level of the filtered cube map, each texel
// the GGX-weighted mean of the capture around its direction (importance sampled, reading the capture's own mips to
// keep it smooth). Level 0 is the capture itself; the roughest level is the most blurred.
in vec2 vUV;
uniform samplerCube uSrc;
uniform int uFace;       // 0..5: +X -X +Y -Y +Z -Z
uniform float uRough;    // 0 mirror .. 1 matt (the level over the highest level)
uniform float uSrcSize;  // the capture's side in texels
out vec4 outColor;

vec3 faceDir(int face, vec2 uv) {
    float u = uv.x * 2.0 - 1.0, v = uv.y * 2.0 - 1.0;
    if (face == 0) return vec3(1.0, -v, -u);
    if (face == 1) return vec3(-1.0, -v, u);
    if (face == 2) return vec3(u, 1.0, v);
    if (face == 3) return vec3(u, -1.0, -v);
    if (face == 4) return vec3(u, -v, 1.0);
    return vec3(-u, -v, -1.0);
}

float radicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

void main() {
    vec3 n = normalize(faceDir(uFace, vUV));
    if (uRough <= 0.0) {
        outColor = vec4(textureLod(uSrc, n, 0.0).rgb, 1.0);
        return;
    }
    float a = uRough * uRough;
    vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tx = normalize(cross(up, n));
    vec3 ty = cross(n, tx);
    const int kSamples = 96;
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        vec2 xi = vec2(float(i) / float(kSamples), radicalInverse(uint(i)));
        float phi = 6.2831853 * xi.x;
        float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
        float sinT = sqrt(max(1.0 - cosT * cosT, 0.0));
        vec3 h = tx * (sinT * cos(phi)) + ty * (sinT * sin(phi)) + n * cosT;
        vec3 l = reflect(-n, h);
        float nl = dot(n, l);
        if (nl <= 0.0) continue;
        // The solid angle one sample stands for against one source texel picks the source mip.
        float nh = max(dot(n, h), 0.0);
        float d = nh * nh * (a * a - 1.0) + 1.0;
        float pdf = (a * a) / (3.14159265 * d * d) * 0.25;  // D * NdotH / (4 VdotH) with V = N
        float saTexel = 4.0 * 3.14159265 / (6.0 * uSrcSize * uSrcSize);
        float saSample = 1.0 / (float(kSamples) * pdf + 1e-4);
        float mip = max(0.5 * log2(saSample / saTexel) + 1.0, 0.0);
        sum += textureLod(uSrc, l, mip).rgb * nl;
        total += nl;
    }
    outColor = vec4(total > 0.0 ? sum / total : textureLod(uSrc, n, 0.0).rgb, 1.0);
}
