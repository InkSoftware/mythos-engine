#include "engine/core/mat4.h"

#include <algorithm>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace mythos {

namespace {
// float[16] <-> glm::mat4: both are column-major, so it is a plain copy.
glm::mat4 Load(const float* m) { return glm::make_mat4(m); }
void Store(const glm::mat4& g, float* m) {
  std::copy_n(glm::value_ptr(g), 16, m);
}
}  // namespace

void Mat4Identity(float* m) { Store(glm::mat4(1.0f), m); }

void Mat4Perspective(float fovy, float aspect, float zn, float zf, float* m) {
  Store(glm::perspectiveRH_NO(fovy, aspect, zn, zf),
        m);  // OpenGL clip space: depth -1..1
}

void Mat4LookAt(const float* eye, const float* center, const float* up,
                float* m) {
  Store(glm::lookAtRH(glm::make_vec3(eye), glm::make_vec3(center),
                      glm::make_vec3(up)),
        m);
}

void Mat4Multiply(const float* a, const float* b, float* out) {
  Store(Load(a) * Load(b), out);
}

// Gribb-Hartmann: each frustum plane is the last row of the view-projection
// matrix plus or minus another row.
void FrustumPlanes(const float* view_proj, Frustum& out) {
  float (&planes)[6][4] = out.plane;
  const auto read_row = [&](int row_index, float* row) {
    for (int col = 0; col < 4; ++col) {
      row[col] = view_proj[col * 4 + row_index];  // column-major
    }
  };
  float row0[4], row1[4], row2[4], row3[4];
  read_row(0, row0);
  read_row(1, row1);
  read_row(2, row2);
  read_row(3, row3);
  for (int i = 0; i < 4; ++i) {
    planes[0][i] = row3[i] + row0[i];  // left
    planes[1][i] = row3[i] - row0[i];  // right
    planes[2][i] = row3[i] + row1[i];  // bottom
    planes[3][i] = row3[i] - row1[i];  // top
    planes[4][i] = row3[i] + row2[i];  // near
    planes[5][i] = row3[i] - row2[i];  // far
  }
}

bool BoxInFrustum(const Frustum& frustum, const float* box_min,
                  const float* box_max, float dz) {
  for (const auto& plane : frustum.plane) {
    // The box corner furthest along the plane normal: if even it is behind the
    // plane, the box is outside.
    const float x = plane[0] >= 0 ? box_max[0] : box_min[0];
    const float y = plane[1] >= 0 ? box_max[1] : box_min[1];
    const float z = (plane[2] >= 0 ? box_max[2] : box_min[2]) + dz;
    if (plane[0] * x + plane[1] * y + plane[2] * z + plane[3] < 0.0f) {
      return false;
    }
  }
  return true;
}

void CubeFaceView(int face, const float* eye, float* view) {
  static const float kDirs[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                    {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  static const float kUps[6][3] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1},
                                   {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
  const float center[3] = {eye[0] + kDirs[face][0], eye[1] + kDirs[face][1],
                           eye[2] + kDirs[face][2]};
  Mat4LookAt(eye, center, kUps[face], view);
}

}  // namespace mythos
