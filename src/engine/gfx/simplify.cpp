#include "engine/gfx/simplify.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

namespace mythos {
namespace {
constexpr float kEps = 1e-4f;

// One axis-aligned quad: in the plane `axis` = `c`, spanning lo..hi along the
// two other axes `ia` (ascending), with a texture mapping that is affine in
// those two coordinates: u = gu[0] * a + gu[1] * b + uc, likewise v.
struct Rect {
  int axis = 0;
  std::array<int, 2> ia{};
  float c = 0;
  float lo[2] = {0, 0}, hi[2] = {0, 0};
  bool flip[2] = {false, false};  // the first vertex is at hi on this axis
  float n[3] = {0, 0, 0};
  float gu[2] = {0, 0}, gv[2] = {0, 0};
  float uc = 0, vc = 0;
  std::pair<int, int> column{0, 0};
};

float Coord(const Vertex& v, const int axis) {
  return axis == 0 ? v.px : (axis == 1 ? v.py : v.pz);
}

bool SamePos(const Vertex& a, const Vertex& b) {
  return std::fabs(a.px - b.px) < kEps && std::fabs(a.py - b.py) < kEps &&
         std::fabs(a.pz - b.pz) < kEps;
}

std::pair<int, int> ColumnOf(const Rect& r, const float column) {
  if (column <= 0) return {0, 0};
  float pos[3];
  pos[r.axis] = r.c;
  for (int m = 0; m < 2; ++m) pos[r.ia[m]] = r.flip[m] ? r.hi[m] : r.lo[m];
  return {static_cast<int>(std::floor(pos[0] / column)),
          static_cast<int>(std::floor(pos[2] / column))};
}

// Reads six vertices laid out as `AddQuad` does (p0 p1 p2, p0 p2 p3).
bool ParseQuad(const Vertex* v, const float column, Rect& r) {
  if (!SamePos(v[0], v[3]) || !SamePos(v[2], v[4])) return false;
  for (int k = 1; k < 6; ++k) {
    if (std::fabs(v[k].nx - v[0].nx) > kEps ||
        std::fabs(v[k].ny - v[0].ny) > kEps ||
        std::fabs(v[k].nz - v[0].nz) > kEps) {
      return false;
    }
  }
  const Vertex &p0 = v[0], &p1 = v[1], &p2 = v[2], &p3 = v[5];
  int axis = -1;
  for (int a = 0; a < 3; ++a) {
    const float c = Coord(p0, a);
    if (std::fabs(Coord(p1, a) - c) < kEps &&
        std::fabs(Coord(p2, a) - c) < kEps &&
        std::fabs(Coord(p3, a) - c) < kEps) {
      if (axis >= 0) return false;  // degenerate
      axis = a;
    }
  }
  if (axis < 0) return false;
  r.axis = axis;
  r.c = Coord(p0, axis);
  int k = 0;
  for (int a = 0; a < 3; ++a) {
    if (a != axis) r.ia[static_cast<size_t>(k++)] = a;
  }
  // p0 -> p1 along one in-plane axis, p1 -> p2 along the other.
  const float e1[2] = {Coord(p1, r.ia[0]) - Coord(p0, r.ia[0]),
                       Coord(p1, r.ia[1]) - Coord(p0, r.ia[1])};
  const float e2[2] = {Coord(p2, r.ia[0]) - Coord(p1, r.ia[0]),
                       Coord(p2, r.ia[1]) - Coord(p1, r.ia[1])};
  int m1 = -1;
  if (std::fabs(e1[0]) > kEps && std::fabs(e1[1]) < kEps) m1 = 0;
  if (std::fabs(e1[1]) > kEps && std::fabs(e1[0]) < kEps) m1 = 1;
  if (m1 < 0) return false;
  const int m2 = 1 - m1;
  if (std::fabs(e2[m2]) < kEps || std::fabs(e2[m1]) > kEps) return false;
  // p3 must close the rectangle.
  for (int m = 0; m < 2; ++m) {
    const float want =
        Coord(p0, r.ia[static_cast<size_t>(m)]) + (m == m1 ? 0.0f : e2[m2]);
    if (std::fabs(Coord(p3, r.ia[static_cast<size_t>(m)]) - want) > kEps)
      return false;
  }
  for (int m = 0; m < 2; ++m) {
    const int a = r.ia[static_cast<size_t>(m)];
    r.lo[m] = std::min(Coord(p0, a), Coord(p2, a));
    r.hi[m] = std::max(Coord(p0, a), Coord(p2, a));
    r.flip[m] = Coord(p0, a) > r.lo[m] + kEps;
  }
  r.n[0] = p0.nx;
  r.n[1] = p0.ny;
  r.n[2] = p0.nz;
  r.gu[m1] = (p1.u - p0.u) / e1[m1];
  r.gu[m2] = (p2.u - p1.u) / e2[m2];
  r.gv[m1] = (p1.v - p0.v) / e1[m1];
  r.gv[m2] = (p2.v - p1.v) / e2[m2];
  const float a0 = Coord(p0, r.ia[0]), b0 = Coord(p0, r.ia[1]);
  r.uc = p0.u - r.gu[0] * a0 - r.gu[1] * b0;
  r.vc = p0.v - r.gv[0] * a0 - r.gv[1] * b0;
  // The mapping must hold at the fourth corner too.
  const float a3 = Coord(p3, r.ia[0]), b3 = Coord(p3, r.ia[1]);
  if (std::fabs(r.gu[0] * a3 + r.gu[1] * b3 + r.uc - p3.u) > 1e-3f ||
      std::fabs(r.gv[0] * a3 + r.gv[1] * b3 + r.vc - p3.v) > 1e-3f) {
    return false;
  }
  r.column = ColumnOf(r, column);
  return true;
}

int64_t Q(const float v, const float scale = 1e4f) {
  return static_cast<int64_t>(std::llround(v * scale));
}

// The part of a texture offset that matters under a repeating wrap.
int64_t Frac(const float v) {
  const float f = v - std::floor(v);
  return Q(f) % 10000;
}

using Key = std::array<int64_t, 16>;

Key KeyOf(const Rect& r) {
  return {r.axis,
          Q(r.c),
          Q(r.n[0], 1e3f),
          Q(r.n[1], 1e3f),
          Q(r.n[2], 1e3f),
          r.flip[0],
          r.flip[1],
          Q(r.gu[0]),
          Q(r.gu[1]),
          Q(r.gv[0]),
          Q(r.gv[1]),
          Frac(r.uc),
          Frac(r.vc),
          0,
          0,
          0};
}

// Joins runs along axis `m` of the rectangles that share the other axis's
// extent and touch end to end, as long as the renderer's column stays the same.
void MergeAlong(std::vector<Rect>& rects, const int m, const float column) {
  const int o = 1 - m;
  std::ranges::sort(rects, [&](const Rect& a, const Rect& b) {
    if (std::fabs(a.lo[o] - b.lo[o]) > kEps) return a.lo[o] < b.lo[o];
    if (std::fabs(a.hi[o] - b.hi[o]) > kEps) return a.hi[o] < b.hi[o];
    return a.lo[m] < b.lo[m];
  });
  std::vector<Rect> out;
  for (const Rect& r : rects) {
    if (!out.empty()) {
      Rect& cur = out.back();
      if (std::fabs(cur.lo[o] - r.lo[o]) < kEps &&
          std::fabs(cur.hi[o] - r.hi[o]) < kEps &&
          std::fabs(cur.hi[m] - r.lo[m]) < kEps) {
        Rect merged = cur;
        merged.hi[m] = r.hi[m];
        if (column <= 0 || (ColumnOf(merged, column) == cur.column &&
                            r.column == cur.column)) {
          cur = merged;
          continue;
        }
      }
    }
    out.push_back(r);
  }
  rects = std::move(out);
}

void Emit(const Rect& r, std::vector<Vertex>& out) {
  float pos[4][3];
  // p0 at the corner the pieces had theirs, p1 along the first in-plane axis,
  // p2 opposite, p3 along the second.
  const float a0 = r.flip[0] ? r.hi[0] : r.lo[0];
  const float a1 = r.flip[0] ? r.lo[0] : r.hi[0];
  const float b0 = r.flip[1] ? r.hi[1] : r.lo[1];
  const float b1 = r.flip[1] ? r.lo[1] : r.hi[1];
  const float ab[4][2] = {{a0, b0}, {a1, b0}, {a1, b1}, {a0, b1}};
  Vertex v[4];
  for (int k = 0; k < 4; ++k) {
    pos[k][r.axis] = r.c;
    pos[k][r.ia[0]] = ab[k][0];
    pos[k][r.ia[1]] = ab[k][1];
    v[k] = Vertex{pos[k][0],
                  pos[k][1],
                  pos[k][2],
                  r.n[0],
                  r.n[1],
                  r.n[2],
                  r.gu[0] * ab[k][0] + r.gu[1] * ab[k][1] + r.uc,
                  r.gv[0] * ab[k][0] + r.gv[1] * ab[k][1] + r.vc};
  }
  for (const int k : {0, 1, 2, 0, 2, 3}) out.push_back(v[k]);
}
}

SimplifyStats SimplifyBatches(std::vector<Batch>& batches, const float column) {
  SimplifyStats stats;
  for (Batch& b : batches) {
    stats.triangles_before += b.verts.size() / 3;
    if (!b.skin.empty() || b.verts.size() < 6) {
      stats.triangles_after += b.verts.size() / 3;
      continue;
    }
    std::map<Key, std::vector<Rect>> groups;
    std::vector<Vertex> out;
    out.reserve(b.verts.size());
    size_t i = 0;
    while (i + 2 < b.verts.size()) {
      Rect r;
      if (i + 5 < b.verts.size() && ParseQuad(&b.verts[i], column, r)) {
        groups[KeyOf(r)].push_back(r);
        i += 6;
      } else {
        out.insert(out.end(), b.verts.begin() + static_cast<std::ptrdiff_t>(i),
                   b.verts.begin() + static_cast<std::ptrdiff_t>(i) + 3);
        i += 3;
      }
    }
    for (auto& [key, rects] : groups) {
      MergeAlong(rects, 0, column);
      MergeAlong(rects, 1, column);
      for (const Rect& r : rects) Emit(r, out);
    }
    b.verts = std::move(out);
    stats.triangles_after += b.verts.size() / 3;
  }
  return stats;
}

SimplifyStats SimplifyGeometry(SceneGeometry& geometry, const float column) {
  SimplifyStats a = SimplifyBatches(geometry.world, column);
  const SimplifyStats b = SimplifyBatches(geometry.props, column);
  a.triangles_before += b.triangles_before;
  a.triangles_after += b.triangles_after;
  return a;
}
}
