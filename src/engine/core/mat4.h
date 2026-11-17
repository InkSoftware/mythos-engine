#pragma once
// Column-major 4x4 matrices as float[16], and the view frustum, for the renderer: no GL here, so
// the maths is tested headless. A view looks down -z with y up (right-handed, like OpenGL).
namespace mythos {

void mat4Identity(float* m);
// A perspective projection (vertical field of view in radians, depth range zn..zf).
void mat4Perspective(float fovy, float aspect, float zn, float zf, float* m);
// A view matrix for an eye looking at `center`, with `up` roughly up.
void mat4LookAt(const float* eye, const float* center, const float* up, float* m);
// out = a * b.
void mat4Multiply(const float* a, const float* b, float* out);

// The view of one face of a cube map around `eye`: faces 0..5 are +X -X +Y -Y +Z -Z, with the
// ups OpenGL's cube sampling expects, so a direction looked up in the cube finds the pixel drawn
// along it.
void cubeFaceView(int face, const float* eye, float* view);

// The six planes of a view frustum, normals pointing inwards: a point is inside when
// a*x + b*y + c*z + d >= 0 for all of them (plane[i] = {a, b, c, d}).
struct Frustum {
    float plane[6][4];
};

// The frustum of a view-projection matrix (Gribb / Hartmann).
void frustumPlanes(const float* viewProj, Frustum& out);

// False when the box (shifted by dz along z) lies wholly outside one plane.
bool boxInFrustum(const Frustum& frustum, const float* bmin, const float* bmax, float dz = 0.0f);

}  // namespace mythos
