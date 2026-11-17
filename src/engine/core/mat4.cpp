#include "engine/core/mat4.h"

#include <cmath>

namespace mythos {

void mat4Identity(float* m) {
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

void mat4Perspective(float fovy, float aspect, float zn, float zf, float* m) {
    const float f = 1.0f / std::tan(fovy * 0.5f);
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = 2.0f * zf * zn / (zn - zf);
}

void mat4LookAt(const float* eye, const float* center, const float* up, float* m) {
    float f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
    float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& v : f) v /= fl;
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (float& v : s) v /= sl;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    mat4Identity(m);
    m[0] = s[0];
    m[4] = s[1];
    m[8] = s[2];
    m[1] = u[0];
    m[5] = u[1];
    m[9] = u[2];
    m[2] = -f[0];
    m[6] = -f[1];
    m[10] = -f[2];
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
}


void mat4Multiply(const float* a, const float* b, float* out) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = sum;
        }
}

void frustumPlanes(const float* m, Frustum& out) {
    float (&planes)[6][4] = out.plane;
    auto row = [&](int i, float* r) {
        for (int c = 0; c < 4; ++c) r[c] = m[c * 4 + i];
    };
    float r0[4], r1[4], r2[4], r3[4];
    row(0, r0);
    row(1, r1);
    row(2, r2);
    row(3, r3);
    for (int c = 0; c < 4; ++c) {
        planes[0][c] = r3[c] + r0[c];  // left
        planes[1][c] = r3[c] - r0[c];  // right
        planes[2][c] = r3[c] + r1[c];  // bottom
        planes[3][c] = r3[c] - r1[c];  // top
        planes[4][c] = r3[c] + r2[c];  // near
        planes[5][c] = r3[c] - r2[c];  // far
    }
}

bool boxInFrustum(const Frustum& frustum, const float* bmin, const float* bmax, float dz) {
    for (int i = 0; i < 6; ++i) {
        const float* p = frustum.plane[i];
        const float x = p[0] >= 0 ? bmax[0] : bmin[0];
        const float y = p[1] >= 0 ? bmax[1] : bmin[1];
        const float z = (p[2] >= 0 ? bmax[2] : bmin[2]) + dz;
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.0f) return false;
    }
    return true;
}

void cubeFaceView(int face, const float* eye, float* view) {
    static const float dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const float ups[6][3] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    const float center[3] = {eye[0] + dirs[face][0], eye[1] + dirs[face][1], eye[2] + dirs[face][2]};
    mat4LookAt(eye, center, ups[face], view);
}

}  // namespace mythos
