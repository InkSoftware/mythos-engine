// Matrix helpers implemented over GLM, which stays out of the public header.
#include "engine/core/mat4.h"

#include <algorithm>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace mythos {
namespace {
glm::mat4 Load(const float* const m) { return glm::make_mat4(m); }

void Store(const glm::mat4& g, float* const m) {
  std::copy_n(glm::value_ptr(g), 16, m);
}
}

void Mat4Identity(float* const m) { Store(glm::mat4(1.0f), m); }

void Mat4Perspective(const float fovy, const float aspect, const float zn,
                     const float zf, float* const m) {
  Store(glm::perspectiveRH_NO(fovy, aspect, zn, zf), m);
}

void Mat4Ortho(const float left, const float right, const float bottom,
               const float top, const float zn, const float zf,
               float* const m) {
  Store(glm::orthoRH_NO(left, right, bottom, top, zn, zf), m);
}

void Mat4LookAt(const float* const eye, const float* const center,
                const float* const up, float* const m) {
  Store(glm::lookAtRH(glm::make_vec3(eye), glm::make_vec3(center),
                      glm::make_vec3(up)),
        m);
}

void Mat4Multiply(const float* const a, const float* const b,
                  float* const out) {
  Store(Load(a) * Load(b), out);
}

// Gribb-Hartmann extraction: each plane is the fourth row plus or minus one of
// the first three rows.
void FrustumPlanes(const float* const view_proj, Frustum& out) {
  float (&planes)[6][4] = out.plane;
  // The array is column-major, so a row is read with a stride of 4.
  const auto read_row = [&](const int row_index, float* const row) {
    for (int col = 0; col < 4; ++col) {
      row[col] = view_proj[col * 4 + row_index];
    }
  };
  float row0[4], row1[4], row2[4], row3[4];
  read_row(0, row0);
  read_row(1, row1);
  read_row(2, row2);
  read_row(3, row3);
  for (int i = 0; i < 4; ++i) {
    planes[0][i] = row3[i] + row0[i];
    planes[1][i] = row3[i] - row0[i];
    planes[2][i] = row3[i] + row1[i];
    planes[3][i] = row3[i] - row1[i];
    planes[4][i] = row3[i] + row2[i];
    planes[5][i] = row3[i] - row2[i];
  }
}

bool BoxInFrustum(const Frustum& frustum, const float* const box_min,
                  const float* const box_max, const float dz) {
  for (const auto& plane : frustum.plane) {
    // Test only the box corner farthest along the plane normal.
    const float x = plane[0] >= 0 ? box_max[0] : box_min[0];
    const float y = plane[1] >= 0 ? box_max[1] : box_min[1];
    const float z = (plane[2] >= 0 ? box_max[2] : box_min[2]) + dz;
    if (plane[0] * x + plane[1] * y + plane[2] * z + plane[3] < 0.0f) {
      return false;
    }
  }
  return true;
}

void CubeFaceView(const int face, const float* const eye, float* const view) {
  static const float kDirs[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                    {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  static const float kUps[6][3] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1},
                                   {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
  const float center[3] = {eye[0] + kDirs[face][0], eye[1] + kDirs[face][1],
                           eye[2] + kDirs[face][2]};
  Mat4LookAt(eye, center, kUps[face], view);
}
}
