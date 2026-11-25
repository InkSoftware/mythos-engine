#include "engine/gfx/lightmap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "engine/gfx/mesh.h"
#include "engine/world/tiles.h"

namespace mythos {
namespace {
constexpr double kPlaneTolerance = 0.03;
constexpr size_t kMaxTexels = 8000000;

// The in-cell planes of layers 6..13: the axis (0 x, 2 z), the sign of the
// normal and how far into the cell the plane is.
struct PlaneLayer {
  int axis;
  double sign;
  double at;
};
constexpr PlaneLayer kPlaneLayers[8] = {{0, 1, 0.5 + kThinWallThickness / 2},
                                        {0, -1, 0.5 - kThinWallThickness / 2},
                                        {2, 1, 0.5 + kThinWallThickness / 2},
                                        {2, -1, 0.5 - kThinWallThickness / 2},
                                        {0, 1, 0.15},
                                        {0, -1, 0.85},
                                        {2, 1, 0.15},
                                        {2, -1, 0.85}};

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
  if (Empty() || face < 0 || face >= kLightmapLayers) return {0, 0, 0, 0};
  const size_t k = Offset(*this, face, std::clamp(x, 0, Width() - 1),
                          std::clamp(y, 0, Height() - 1));
  return {rgba[k], rgba[k + 1], rgba[k + 2], rgba[k + 3]};
}

namespace {
// Bilinear inside one tile of `layer` (cell cx, cz), the coordinates (u, v)
// in 0..1 of the tile's `rows` rows, clamped to texel centres.
std::array<float, 4> TileSample(const Lightmap& m, const int layer,
                                const int cx, const int cz, const double u,
                                const double v, const double rows) {
  const double tx = std::clamp(u * m.texels, 0.5, m.texels - 0.5);
  const double ty = std::clamp(v * rows, 0.5, rows - 0.5);
  const double gx = cx * m.texels + tx - 0.5, gy = cz * m.wall_rows + ty - 0.5;
  const int x0 = static_cast<int>(std::floor(gx)),
            y0 = static_cast<int>(std::floor(gy));
  const auto fx = static_cast<float>(gx - x0), fy = static_cast<float>(gy - y0);
  std::array<float, 4> out{};
  const auto a = m.Texel(layer, x0, y0), b = m.Texel(layer, x0 + 1, y0),
             c = m.Texel(layer, x0, y0 + 1), d = m.Texel(layer, x0 + 1, y0 + 1);
  for (size_t k = 0; k < 4; ++k) {
    const float top = a[k] + (b[k] - a[k]) * fx;
    const float bottom = c[k] + (d[k] - c[k]) * fx;
    out[k] = top + (bottom - top) * fy;
  }
  return out;
}
}

std::array<float, 3> Lightmap::Sample(const double x, const double y,
                                      const double z, const double nx,
                                      const double ny, const double nz,
                                      bool* const covered) const {
  if (covered != nullptr) *covered = false;
  if (Empty()) return {0, 0, 0};
  const int face = FaceOf(nx, ny, nz);
  const bool vertical = face < 2 || face >= 4;
  if (vertical && (y < -kPlaneTolerance || y > kWallHeight + kPlaneTolerance))
    return {0, 0, 0};
  // The cell the surface looks into, nudged off the plane.
  const int cx = static_cast<int>(std::floor(x + nx * 0.02));
  const int cz = static_cast<int>(std::floor(z + nz * 0.02));
  if (cx >= 0 && cz >= 0 && cx < cells_x && cz < cells_z) {
    // The point must lie on the tile's plane, so furniture is left out.
    const double plane = face == 0   ? cx
                         : face == 1 ? cx + 1.0
                         : face == 2 ? 0.0
                         : face == 3 ? kWallHeight
                         : face == 4 ? cz
                                     : cz + 1.0;
    const double along = face < 2 ? x : (face < 4 ? y : z);
    if (std::fabs(along - plane) <= kPlaneTolerance) {
      double u = 0, v = 0, rows = wall_rows;
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
      const auto out = TileSample(*this, face, cx, cz, u, v, rows);
      if (out[3] > 0.5f) {
        if (covered != nullptr) *covered = true;
        return {out[0], out[1], out[2]};
      }
    }
  }
  // A face inside a cell (a thin wall's, a doorway's jamb): in the cell the
  // point is in, on a plane at a fixed depth into it.
  if (vertical) {
    const int tx = static_cast<int>(std::floor(x));
    const int tz = static_cast<int>(std::floor(z));
    if (tx < 0 || tz < 0 || tx >= cells_x || tz >= cells_z) return {0, 0, 0};
    for (int layer = 6; layer < kLightmapLayers; ++layer) {
      const PlaneLayer& pl = kPlaneLayers[layer - 6];
      if (pl.sign * (pl.axis == 0 ? nx : nz) <= 0 ||
          std::fabs(pl.axis == 0 ? nx : nz) < 0.5)
        continue;
      const double plane = (pl.axis == 0 ? tx : tz) + pl.at;
      const double along = pl.axis == 0 ? x : z;
      if (std::fabs(along - plane) > kPlaneTolerance) continue;
      const double u = pl.axis == 0 ? z - tz : x - tx;
      const auto out =
          TileSample(*this, layer, tx, tz, u, y / kWallHeight, wall_rows);
      if (out[3] > 0.5f) {
        if (covered != nullptr) *covered = true;
        return {out[0], out[1], out[2]};
      }
    }
  }
  return {0, 0, 0};
}

std::array<float, 3> LightmapStack::Sample(const double x, const double y,
                                           const double z, const double nx,
                                           const double ny, const double nz,
                                           bool* const covered) const {
  if (covered != nullptr) *covered = false;
  if (Empty()) return {0, 0, 0};
  const double step = pitch > 0 ? pitch : kFloorPitch;
  // The ceiling of a floor is a hair under the next floor's level.
  const auto floor_index = static_cast<int64_t>(std::floor((y + 0.004) / step));
  if (floor_index < 0 || std::cmp_greater_equal(floor_index, floors.size()))
    return {0, 0, 0};
  return floors[static_cast<size_t>(floor_index)].Sample(
      x, y - static_cast<double>(floor_index) * step, z, nx, ny, nz, covered);
}

Lightmap BakeLightmap(const Level& level, const std::vector<ProbeLamp>& lamps,
                      const LightmapOptions& options) {
  Lightmap m;
  if (level.Width() <= 0 || level.Height() <= 0) return m;
  m.cells_x = level.Width();
  m.cells_z = level.Height();
  int t = std::max(2, options.texels_per_m);
  const auto size_for = [&](const int texels) {
    return static_cast<size_t>(kLightmapLayers) * m.cells_x * texels *
           m.cells_z * static_cast<size_t>(std::lround(kWallHeight * texels));
  };
  while (t > 2 && size_for(t) > kMaxTexels) t /= 2;
  m.texels = t;
  m.wall_rows = static_cast<int>(std::lround(kWallHeight * t));
  m.rgba.assign(
      static_cast<size_t>(kLightmapLayers) * m.Width() * m.Height() * 4, 0.0f);

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
  // Faces inside a cell: a thin wall's four, a doorway's four jamb planes,
  // along the whole cell.
  for (int cz = 0; cz < m.cells_z; ++cz) {
    for (int cx = 0; cx < m.cells_x; ++cx) {
      const char glyph = level.At(cx, cz);
      const bool thin = level.Tiles().Thin(glyph);
      const bool door = level.Tiles().IsDoor(glyph);
      if (!thin && !door) continue;
      for (int layer = 6; layer < kLightmapLayers; ++layer) {
        if ((layer < 10) != thin && (layer >= 10) != door) continue;
        if (layer < 10 && !thin) continue;
        if (layer >= 10 && !door) continue;
        const PlaneLayer& pl = kPlaneLayers[layer - 6];
        const bool along_x = pl.axis == 2;  // the tile runs along x
        const double n[3] = {pl.axis == 0 ? pl.sign : 0.0, 0.0,
                             pl.axis == 2 ? pl.sign : 0.0};
        for (int j = 0; j < m.wall_rows; ++j) {
          for (int i = 0; i < m.texels; ++i) {
            const double a = (i + 0.5) / m.texels;
            const double py = (j + 0.5) / m.wall_rows * kWallHeight;
            const double px = along_x ? cx + a : cx + pl.at;
            const double pz = along_x ? cz + pl.at : cz + a;
            double r = options.ambient[0], g = options.ambient[1],
                   b = options.ambient[2];
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
                Offset(m, layer, cx * m.texels + i, cz * m.wall_rows + j);
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

namespace {
uint16_t ToHalf(const float f) {
  uint32_t b = 0;
  std::memcpy(&b, &f, 4);
  const uint32_t sign = (b >> 16) & 0x8000u;
  int exp = static_cast<int>((b >> 23) & 0xFF) - 127 + 15;
  uint32_t mant = b & 0x7FFFFFu;
  if (((b >> 23) & 0xFF) == 0xFF)
    return static_cast<uint16_t>(sign | 0x7C00u | (mant ? 0x200u : 0));
  if (exp >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
  if (exp <= 0) {
    if (exp < -10) return static_cast<uint16_t>(sign);
    mant |= 0x800000u;
    const int shift = 14 - exp;
    return static_cast<uint16_t>(sign |
                                 ((mant + (1u << (shift - 1))) >> shift));
  }
  return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) |
                               ((mant + 0x1000u) >> 13));
}

float FromHalf(const uint16_t h) {
  const uint32_t sign = (h & 0x8000u) << 16;
  const uint32_t exp = (h >> 10) & 0x1F, mant = h & 0x3FFu;
  uint32_t b = 0;
  if (exp == 0) {
    if (mant == 0) {
      b = sign;
    } else {  // subnormal
      int e = -1;
      uint32_t m = mant;
      do {
        ++e;
        m <<= 1;
      } while ((m & 0x400u) == 0);
      b = sign | (static_cast<uint32_t>(127 - 15 - e) << 23) |
          ((m & 0x3FFu) << 13);
    }
  } else if (exp == 31) {
    b = sign | 0x7F800000u | (mant << 13);
  } else {
    b = sign | ((exp + 127 - 15) << 23) | (mant << 13);
  }
  float f = 0;
  std::memcpy(&f, &b, 4);
  return f;
}

void PutU32(std::string& s, const uint32_t v) {
  for (int i = 0; i < 4; ++i)
    s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

uint32_t GetU32(const std::string& s, const size_t at) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i)
    v |= static_cast<uint32_t>(
             static_cast<unsigned char>(s[at + static_cast<size_t>(i)]))
         << (8 * i);
  return v;
}
}

std::string SerializeLightmap(const Lightmap& map) {
  std::string s = "MYLM1";
  PutU32(s, static_cast<uint32_t>(map.cells_x));
  PutU32(s, static_cast<uint32_t>(map.cells_z));
  PutU32(s, static_cast<uint32_t>(map.texels));
  PutU32(s, static_cast<uint32_t>(map.wall_rows));
  for (const float f : map.rgba) {
    const uint16_t h = ToHalf(f);
    s.push_back(static_cast<char>(h & 0xFF));
    s.push_back(static_cast<char>(h >> 8));
  }
  return s;
}

bool ParseLightmap(const std::string& bytes, Lightmap* const out,
                   std::string* const error) {
  const auto fail = [&](const char* why) {
    if (error != nullptr) *error = why;
    return false;
  };
  if (bytes.size() < 21 || bytes.compare(0, 5, "MYLM1") != 0)
    return fail("not a lightmap file");
  Lightmap m;
  m.cells_x = static_cast<int>(GetU32(bytes, 5));
  m.cells_z = static_cast<int>(GetU32(bytes, 9));
  m.texels = static_cast<int>(GetU32(bytes, 13));
  m.wall_rows = static_cast<int>(GetU32(bytes, 17));
  if (m.cells_x < 1 || m.cells_z < 1 || m.texels < 1 || m.wall_rows < 1 ||
      m.cells_x > 4096 || m.cells_z > 4096 || m.texels > 64 ||
      m.wall_rows > 256)
    return fail("bad sizes");
  const size_t count = static_cast<size_t>(kLightmapLayers) *
                       static_cast<size_t>(m.Width()) *
                       static_cast<size_t>(m.Height()) * 4;
  if (count > 4 * kMaxTexels || bytes.size() != 21 + count * 2)
    return fail("the size does not match the data");
  m.rgba.resize(count);
  for (size_t i = 0; i < count; ++i) {
    const auto lo = static_cast<unsigned char>(bytes[21 + i * 2]);
    const auto hi = static_cast<unsigned char>(bytes[22 + i * 2]);
    m.rgba[i] = FromHalf(static_cast<uint16_t>(lo | (hi << 8)));
  }
  *out = std::move(m);
  return true;
}
}
