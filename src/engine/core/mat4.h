// Matrix helpers on plain float arrays. Matrices are 16 floats in column-major
// order (OpenGL layout), right-handed, with clip depth -1..1.
#ifndef MYTHOS_ENGINE_CORE_MAT4_H_
#define MYTHOS_ENGINE_CORE_MAT4_H_

namespace mythos {
// Writes the identity matrix to `m`.
void Mat4Identity(float* m);

// Perspective projection; `fovy` is the vertical field of view in radians and
// `zn`, `zf` the near and far planes.
void Mat4Perspective(float fovy, float aspect, float zn, float zf, float* m);

// View matrix looking from `eye` toward `center`; each vector is 3 floats.
void Mat4LookAt(const float* eye, const float* center, const float* up,
                float* m);

// out = a * b. `out` may alias an input.
void Mat4Multiply(const float* a, const float* b, float* out);

// View matrix for cube-map `face` (0..5 in the order +x, -x, +y, -y, +z, -z)
// from `eye`, using the standard cube-map up vectors.
void CubeFaceView(int face, const float* eye, float* view);

// View frustum as six planes (left, right, bottom, top, near, far), each (a, b,
// c, d) with the inside where a*x + b*y + c*z + d >= 0. Not normalized.
struct Frustum {
  float plane[6][4];
};

// Extracts the planes from a combined view-projection matrix.
void FrustumPlanes(const float* view_proj, Frustum& out);

// Conservative test of an axis-aligned box against the frustum: false only when
// it lies fully outside one plane. `dz` shifts the box along z before testing.
bool BoxInFrustum(const Frustum& frustum, const float* box_min,
                  const float* box_max, float dz = 0.0f);
}

#endif
