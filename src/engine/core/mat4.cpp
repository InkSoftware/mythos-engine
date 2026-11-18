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

// Gribb-Hartmann: each frustum plane is the last row of the view-projection matrix plus or minus another row.
void frustumPlanes(const float* viewProj, Frustum& out) {
    float (&planes)[6][4] = out.plane;
    auto readRow = [&](int rowIndex, float* row) {
        for (int col = 0; col < 4; ++col) row[col] = viewProj[col * 4 + rowIndex];  // column-major
    };
    float row0[4], row1[4], row2[4], row3[4];
    readRow(0, row0);
    readRow(1, row1);
    readRow(2, row2);
    readRow(3, row3);
    for (int i = 0; i < 4; ++i) {
        planes[0][i] = row3[i] + row0[i];  // left
        planes[1][i] = row3[i] - row0[i];  // right
        planes[2][i] = row3[i] + row1[i];  // bottom
        planes[3][i] = row3[i] - row1[i];  // top
        planes[4][i] = row3[i] + row2[i];  // near
        planes[5][i] = row3[i] - row2[i];  // far
    }
}

bool boxInFrustum(const Frustum& frustum, const float* boxMin, const float* boxMax, float dz) {
    for (int i = 0; i < 6; ++i) {
        const float* plane = frustum.plane[i];
        // The box corner furthest along the plane normal: if even it is behind the plane, the box is outside.
        const float x = plane[0] >= 0 ? boxMax[0] : boxMin[0];
        const float y = plane[1] >= 0 ? boxMax[1] : boxMin[1];
        const float z = (plane[2] >= 0 ? boxMax[2] : boxMin[2]) + dz;
        if (plane[0] * x + plane[1] * y + plane[2] * z + plane[3] < 0.0f) return false;
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
