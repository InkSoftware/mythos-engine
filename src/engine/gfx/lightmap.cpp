#include "engine/gfx/lightmap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "engine/gfx/mesh.h"

namespace mythos {
namespace {
constexpr double kPlaneTolerance = 0.03;
constexpr size_t kMaxTexels = 8000000;

// The face a normal looks along, by its dominant axis.
int FaceOf(const double nx, const double ny, const double nz) {
  const double ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);
  if (ax >= ay && ax >= az) return nx >= 0 ? 0 : 1;
  if (ay >= az) return ny >= 0 ? 2 : 3;
  return nz >= 0 ? 4 : 5;
}

size_t Offset(const Lightmap& m, const int face, const int x, const int y) {
  return ((static_cast<size_t>(face) * static_cast<size_t>(m.Height()) +
           static_cast<size_t>(y)) *
              static_cast<size_t>(m.Width()) +
          static_cast<size_t>(x)) *
         4;
}
}

std::array<float, 4> Lightmap::Texel(const int face, const int x,
                                     const int y) const {
  if (Empty() || face < 0 || face >= kLightmapFaces) return {0, 0, 0, 0};
  const size_t k = Offset(*this, face, std::clamp(x, 0, Width() - 1),
                          std::clamp(y, 0, Height() - 1));
  return {rgba[k], rgba[k + 1], rgba[k + 2], rgba[k + 3]};
}

std::array<float, 3> Lightmap::Sample(const double x, const double y,
                                      const double z, const double nx,
                                      const double ny, const double nz,
                                      bool* const covered) const {
  if (covered != nullptr) *covered = false;
  if (Empty()) return {0, 0, 0};
  const int face = FaceOf(nx, ny, nz);
  // The cell the surface looks into, nudged off the plane.
  const int cx = static_cast<int>(std::floor(x + nx * 0.02));
  const int cz = static_cast<int>(std::floor(z + nz * 0.02));
  if (cx < 0 || cz < 0 || cx >= cells_x || cz >= cells_z) return {0, 0, 0};
  // The point must lie on the tile's plane, so furniture is left out.
  const double plane = face == 0   ? cx
                       : face == 1 ? cx + 1.0
                       : face == 2 ? 0.0
                       : face == 3 ? kWallHeight
                       : face == 4 ? cz
                                   : cz + 1.0;
  const double along = face < 2 ? x : (face < 4 ? y : z);
  if (std::fabs(along - plane) > kPlaneTolerance) return {0, 0, 0};
  double u = 0, v = 0;
  double rows = wall_rows;
  if (face < 2) {
    u = z - cz;
    v = y / kWallHeight;
  } else if (face < 4) {
    u = x - cx;
    v = z - cz;
    rows = texels;
  } else {
    u = x - cx;
    v = y / kWallHeight;
  }
  if (face < 2 || face >= 4) {
    if (y < -kPlaneTolerance || y > kWallHeight + kPlaneTolerance)
      return {0, 0, 0};
  }
  // Texel coordinates inside the tile, clamped to texel centres.
  const double tx = std::clamp(u * texels, 0.5, texels - 0.5);
  const double ty = std::clamp(v * rows, 0.5, rows - 0.5);
  const double gx = cx * texels + tx - 0.5, gy = cz * wall_rows + ty - 0.5;
  const int x0 = static_cast<int>(std::floor(gx)),
            y0 = static_cast<int>(std::floor(gy));
  const auto fx = static_cast<float>(gx - x0), fy = static_cast<float>(gy - y0);
  std::array<float, 4> out{};
  const auto a = Texel(face, x0, y0), b = Texel(face, x0 + 1, y0),
             c = Texel(face, x0, y0 + 1), d = Texel(face, x0 + 1, y0 + 1);
  for (size_t k = 0; k < 4; ++k) {
    const float top = a[k] + (b[k] - a[k]) * fx;
    const float bottom = c[k] + (d[k] - c[k]) * fx;
    out[k] = top + (bottom - top) * fy;
  }
  if (covered != nullptr) *covered = out[3] > 0.5f;
  return {out[0], out[1], out[2]};
}

Lightmap BakeLightmap(const Level& level, const std::vector<ProbeLamp>& lamps,
                      const LightmapOptions& options) {
  Lightmap m;
  if (level.Width() <= 0 || level.Height() <= 0) return m;
  m.cells_x = level.Width();
  m.cells_z = level.Height();
  int t = std::max(2, options.texels_per_m);
  const auto size_for = [&](const int texels) {
    return static_cast<size_t>(kLightmapFaces) * m.cells_x * texels *
           m.cells_z * static_cast<size_t>(std::lround(kWallHeight * texels));
  };
  while (t > 2 && size_for(t) > kMaxTexels) t /= 2;
  m.texels = t;
  m.wall_rows = static_cast<int>(std::lround(kWallHeight * t));
  m.rgba.assign(
      static_cast<size_t>(kLightmapFaces) * m.Width() * m.Height() * 4, 0.0f);

  const auto open = [&](const int cx, const int cz) {
    return cx >= 0 && cz >= 0 && cx < m.cells_x && cz < m.cells_z &&
           !level.PointBlocked(cx + 0.5, cz + 0.5);
  };
  // The normal of each face.
  static constexpr double kNormals[kLightmapFaces][3] = {
      {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  static constexpr int kSolidNeighbour[kLightmapFaces][2] = {
      {-1, 0}, {1, 0}, {0, 0}, {0, 0}, {0, -1}, {0, 1}};

  for (int cz = 0; cz < m.cells_z; ++cz) {
    for (int cx = 0; cx < m.cells_x; ++cx) {
      if (!open(cx, cz)) continue;
      for (int face = 0; face < kLightmapFaces; ++face) {
        if (face < 2 || face >= 4) {  // a wall: the neighbour must be solid
          const int nx = cx + kSolidNeighbour[face][0],
                    nz = cz + kSolidNeighbour[face][1];
          if (open(nx, nz)) continue;
        }
        const bool horizontal = face == 2 || face == 3;
        const int rows = horizontal ? m.texels : m.wall_rows;
        const double* n = kNormals[face];
        for (int j = 0; j < rows; ++j) {
          for (int i = 0; i < m.texels; ++i) {
            const double a = (i + 0.5) / m.texels;
            double px = 0, py = 0, pz = 0;
            if (face < 2) {
              px = face == 0 ? cx : cx + 1.0;
              pz = cz + a;
              py = (j + 0.5) / m.wall_rows * kWallHeight;
            } else if (horizontal) {
              px = cx + a;
              pz = cz + (j + 0.5) / m.texels;
              py = face == 2 ? 0.0 : kWallHeight;
            } else {
              px = cx + a;
              pz = face == 4 ? cz : cz + 1.0;
              py = (j + 0.5) / m.wall_rows * kWallHeight;
            }
            double r = options.ambient[0], g = options.ambient[1],
                   b = options.ambient[2];
            // The line is checked to a point a little into the room.
            const double sx = px + n[0] * 0.05, sz = pz + n[2] * 0.05;
            for (const ProbeLamp& lamp : lamps) {
              const double dx = lamp.x - px, dy = lamp.y - py, dz = lamp.z - pz;
              const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
              if (dist >= lamp.radius || dist < 1e-6) continue;
              const double facing = (dx * n[0] + dy * n[1] + dz * n[2]) / dist;
              if (facing <= 0) continue;
              if (!level.LineOfSight(lamp.x, lamp.z, sx, sz)) continue;
              double att = 1.0 - dist / lamp.radius;
              att *= att * (0.35 + 0.65 * facing);
              r += lamp.r * att;
              g += lamp.g * att;
              b += lamp.b * att;
            }
            const size_t k =
                Offset(m, face, cx * m.texels + i, cz * m.wall_rows + j);
            m.rgba[k] = static_cast<float>(r);
            m.rgba[k + 1] = static_cast<float>(g);
            m.rgba[k + 2] = static_cast<float>(b);
            m.rgba[k + 3] = 1.0f;
          }
        }
      }
    }
  }
  return m;
}
}
