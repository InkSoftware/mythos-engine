#include "engine/core/mat4.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace mythos {

namespace {
// float[16] <-> glm::mat4: both are column-major, so it is a plain copy.
glm::mat4 load(const float* m) { return glm::make_mat4(m); }
void store(const glm::mat4& g, float* m) { std::copy_n(glm::value_ptr(g), 16, m); }
}  // namespace

void mat4Identity(float* m) { store(glm::mat4(1.0f), m); }

void mat4Perspective(float fovy, float aspect, float zn, float zf, float* m) {
    store(glm::perspectiveRH_NO(fovy, aspect, zn, zf), m);  // OpenGL clip space: depth -1..1
}

void mat4LookAt(const float* eye, const float* center, const float* up, float* m) {
    store(glm::lookAtRH(glm::make_vec3(eye), glm::make_vec3(center), glm::make_vec3(up)), m);
}

void mat4Multiply(const float* a, const float* b, float* out) { store(load(a) * load(b), out); }

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
