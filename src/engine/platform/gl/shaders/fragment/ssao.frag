#version 410 core
// Screen-space ambient occlusion from the depth buffer: for each pixel, a ring
// of points above the surface is tested against the depth around it, and the
// more of them lie behind nearer geometry the darker the crease or corner.
// Output: 1 = open, lower = occluded (in all three channels, so the bloom blur
// pass can smooth it).
in vec2 vUV;
uniform sampler2D uDepth;
uniform vec2 uTan;      // tan(half fov) * aspect, tan(half fov)
uniform vec2 uNearFar;
uniform vec2 uTexel;    // one texel of the depth texture, in uv
uniform float uRadius;  // metres
out vec4 outColor;

float linearZ(float d) {
    float zn = d * 2.0 - 1.0;
    return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - zn * (uNearFar.y - uNearFar.x));
}

vec3 viewPos(vec2 uv) {
    float z = linearZ(texture(uDepth, uv).r);
    return vec3((uv * 2.0 - 1.0) * uTan * z, -z);
}

void main() {
    float d = texture(uDepth, vUV).r;
    if (d >= 0.99999) { outColor = vec4(1.0); return; }  // sky
    vec3 p = viewPos(vUV);
    // The normal from the neighbours, taking on each axis the side that is closer in depth (no smear over edges).
    vec3 pr = viewPos(vUV + vec2(uTexel.x, 0.0)), pl = viewPos(vUV - vec2(uTexel.x, 0.0));
    vec3 pu = viewPos(vUV + vec2(0.0, uTexel.y)), pd = viewPos(vUV - vec2(0.0, uTexel.y));
    vec3 dx = abs(pr.z - p.z) < abs(p.z - pl.z) ? pr - p : p - pl;
    vec3 dy = abs(pu.z - p.z) < abs(p.z - pd.z) ? pu - p : p - pd;
    vec3 n = normalize(cross(dx, dy));
    if (dot(n, p) > 0.0) n = -n;  // facing the eye

    vec3 t = normalize(cross(n, abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(n, t);
    // Per-pixel rotation of the ring (interleaved gradient noise), undone by the blur.
    float rot = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    const int N = 12;
    float occ = 0.0;
    for (int i = 0; i < N; ++i) {
        float u = (float(i) + 0.5) / float(N);
        float h = mix(0.15, 1.0, u);                // height above the surface
        float s = sqrt(max(1.0 - h * h, 0.0));
        float phi = float(i) * 2.39996323 + rot;
        vec3 dir = t * cos(phi) * s + b * sin(phi) * s + n * h;
        vec3 sp = p + dir * uRadius * mix(0.2, 1.0, u * u);
        if (sp.z > -uNearFar.x) continue;
        vec2 uv = (sp.xy / (-sp.z)) / uTan * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
        float scene = linearZ(texture(uDepth, uv).r);
        float gap = -sp.z - scene;  // > 0: the scene surface is nearer than the sample
        float range = smoothstep(0.0, 1.0, uRadius / max(abs(-p.z - scene), 1e-3));
        occ += (gap > 0.06 ? 1.0 : 0.0) * range;
    }
    float ao = 1.0 - occ / float(N);
    outColor = vec4(vec3(pow(ao, 1.5)), 1.0);
}
