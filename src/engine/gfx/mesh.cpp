#include "engine/gfx/mesh.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "engine/gfx/model.h"

namespace mythos {
namespace {
// Plain 3D point used by the geometry helpers.
struct P3 {
  float x, y, z;
};

// The batch in `out` with this texture and flags, created on first use. The
// reference is invalidated by the next push to `out`.
Batch& BatchFor(std::vector<Batch>& out, const std::string& texture,
                const bool emissive, const bool alpha_test,
                const float opacity = 1.0f) {
  for (auto& existing : out) {
    if (existing.texture == texture && existing.emissive == emissive &&
        existing.alpha_test == alpha_test && existing.opacity == opacity) {
      return existing;
    }
  }
  out.push_back(Batch{});
  Batch& b = out.back();
  b.texture = texture;
  b.emissive = emissive;
  b.alpha_test = alpha_test;
  b.opacity = opacity;
  return b;
}

void PushVertex(Batch& b, const P3& p, const float nx, const float ny,
                const float nz, const float u, const float v) {
  b.verts.push_back({.px = p.x,
                     .py = p.y,
                     .pz = p.z,
                     .nx = nx,
                     .ny = ny,
                     .nz = nz,
                     .u = u,
                     .v = v});
}

// Two triangles for the quad p0..p3 with the given normal; texture coordinates
// run 0..u_max by 0..v_max (p0 at the origin, p1 along u, p3 along v). Culling
// is off, so winding does not matter.
void AddQuad(Batch& b, const P3 p0, const P3 p1, const P3 p2, const P3 p3,
             const float nx, const float ny, const float nz, const float u_max,
             const float v_max) {
  PushVertex(b, p0, nx, ny, nz, 0, 0);
  PushVertex(b, p1, nx, ny, nz, u_max, 0);
  PushVertex(b, p2, nx, ny, nz, u_max, v_max);
  PushVertex(b, p0, nx, ny, nz, 0, 0);
  PushVertex(b, p2, nx, ny, nz, u_max, v_max);
  PushVertex(b, p3, nx, ny, nz, 0, v_max);
}

// Faces of AddBox, for leaving out those where a box joins another.
enum BoxFace : unsigned {
  kFacePosX = 1,
  kFaceNegX = 2,
  kFaceTop = 4,
  kFaceBottom = 8,
  kFacePosZ = 16,
  kFaceNegZ = 32,
};

// An axis-aligned box, minus the faces in `skip`, with uv tiled by face size
// (one repeat per meter).
void AddBox(Batch& b, const float x0, const float y0, const float z0,
            const float x1, const float y1, const float z1,
            const unsigned skip = 0) {
  if (!(skip & kFacePosX))
    AddQuad(b, {.x = x1, .y = y0, .z = z0}, {.x = x1, .y = y0, .z = z1},
            {.x = x1, .y = y1, .z = z1}, {.x = x1, .y = y1, .z = z0}, 1, 0, 0,
            z1 - z0, y1 - y0);
  if (!(skip & kFaceNegX))
    AddQuad(b, {.x = x0, .y = y0, .z = z1}, {.x = x0, .y = y0, .z = z0},
            {.x = x0, .y = y1, .z = z0}, {.x = x0, .y = y1, .z = z1}, -1, 0, 0,
            z1 - z0, y1 - y0);
  if (!(skip & kFaceTop))
    AddQuad(b, {.x = x0, .y = y1, .z = z0}, {.x = x1, .y = y1, .z = z0},
            {.x = x1, .y = y1, .z = z1}, {.x = x0, .y = y1, .z = z1}, 0, 1, 0,
            x1 - x0, z1 - z0);
  if (!(skip & kFaceBottom))
    AddQuad(b, {.x = x0, .y = y0, .z = z1}, {.x = x1, .y = y0, .z = z1},
            {.x = x1, .y = y0, .z = z0}, {.x = x0, .y = y0, .z = z0}, 0, -1, 0,
            x1 - x0, z1 - z0);
  if (!(skip & kFacePosZ))
    AddQuad(b, {.x = x0, .y = y0, .z = z1}, {.x = x1, .y = y0, .z = z1},
            {.x = x1, .y = y1, .z = z1}, {.x = x0, .y = y1, .z = z1}, 0, 0, 1,
            x1 - x0, y1 - y0);
  if (!(skip & kFaceNegZ))
    AddQuad(b, {.x = x0, .y = y0, .z = z0}, {.x = x1, .y = y0, .z = z0},
            {.x = x1, .y = y1, .z = z0}, {.x = x0, .y = y1, .z = z0}, 0, 0, -1,
            x1 - x0, y1 - y0);
}

// Height of the door-shaped hole, the top of the lintel, the thickness of a
// post, and how far the posts stand above the opening.
constexpr float kDoorOpeningHeight = 2.02f;
constexpr float kLintelTop = 2.14f;
constexpr float kPostThickness = 0.10f;
constexpr float kPostAboveOpening = 0.06f;
constexpr float kWindowSill = 0.90f, kWindowHead = 2.05f, kWindowJamb = 0.10f;

// Wooden posts and a header around an opening in the cell at (fx, fz). The
// passage runs along x when `along_x` (you walk through in the x direction).
// `inset` is how far the posts sit from the sides of the cell; `top_y` is where
// the header ends: the lintel line for door cells, the ceiling for the open
// doorways between a hallway and its rooms.
void AddDoorFrame(std::vector<Batch>& out, const float fx, const float fz,
                  const bool along_x, const float inset, const float top_y) {
  Batch& wood = BatchFor(out, "door", false, false);
  const float post_near = inset, post_far = inset + kPostThickness;
  const float post_top = kDoorOpeningHeight + kPostAboveOpening;
  if (along_x) {
    AddBox(wood, fx + 0.30f, 0, fz + post_near, fx + 0.70f, post_top,
           fz + post_far);
    AddBox(wood, fx + 0.30f, 0, fz + 1 - post_far, fx + 0.70f, post_top,
           fz + 1 - post_near);
    AddBox(wood, fx + 0.25f, kDoorOpeningHeight, fz + post_near, fx + 0.75f,
           top_y, fz + 1 - post_near);
  } else {
    AddBox(wood, fx + post_near, 0, fz + 0.30f, fx + post_far, post_top,
           fz + 0.70f);
    AddBox(wood, fx + 1 - post_far, 0, fz + 0.30f, fx + 1 - post_near, post_top,
           fz + 0.70f);
    AddBox(wood, fx + post_near, kDoorOpeningHeight, fz + 0.25f,
           fx + 1 - post_near, top_y, fz + 0.75f);
  }
}

// True if the passage through the door cell at (x, y) runs along the x axis,
// which is when it has solid walls to the north and south.
bool DoorPassageAlongX(const Level& level, const int x, const int y) {
  const TileSet& t = level.Tiles();
  return t.Solid(level.At(x, y - 1)) && t.Solid(level.At(x, y + 1));
}

// True for an open passage cell between two thick walls (not a door cell): a
// room entrance. Thin walls do not count, since they have no depth to form a
// passage.
bool IsDoorwayCell(const Level& level, const int x, const int y) {
  const TileSet& t = level.Tiles();
  const char c = level.At(x, y);
  if (t.Solid(c) || t.IsDoor(c)) return false;
  const auto thick = [&](const int cx, const int cy) {
    return t.Solid(level.At(cx, cy)) && !t.Thin(level.At(cx, cy));
  };
  const bool walls_x = thick(x - 1, y) && thick(x + 1, y);
  const bool walls_z = thick(x, y - 1) && thick(x, y + 1);
  return walls_x != walls_z;
}

// Two crossed vertical quads of width `w` and height `h` centerd at (cx, cz),
// lifted by `y0`, so a sprite reads from every angle.
void AddCross(Batch& b, const float cx, const float cz, const float w,
              const float h, const float y0) {
  const float hw = w * 0.5f;
  AddQuad(b, {.x = cx - hw, .y = y0, .z = cz}, {.x = cx + hw, .y = y0, .z = cz},
          {.x = cx + hw, .y = y0 + h, .z = cz},
          {.x = cx - hw, .y = y0 + h, .z = cz}, 0, 0, 1, 1, 1);
  AddQuad(b, {.x = cx, .y = y0, .z = cz - hw}, {.x = cx, .y = y0, .z = cz + hw},
          {.x = cx, .y = y0 + h, .z = cz + hw},
          {.x = cx, .y = y0 + h, .z = cz - hw}, 1, 0, 0, 1, 1);
}

// Bakes a model into the world: rotates it by `yaw_deg` about the vertical
// axis, then moves it to (center_x, lift, center_z). The model's local +z is
// its front: yaw 0 faces +z, 90 faces +x, 180 faces -z, 270 faces -x.
void AppendModel(std::vector<Batch>& world, const Model& model,
                 const double centre_x, const double centre_z,
                 const double yaw_deg, const double lift) {
  const double yaw_rad = yaw_deg * std::numbers::pi / 180.0;
  const auto cos_yaw = static_cast<float>(std::cos(yaw_rad));
  const auto sin_yaw = static_cast<float>(std::sin(yaw_rad));

  const auto origin_x = static_cast<float>(centre_x),
             origin_z = static_cast<float>(centre_z);
  for (const auto& model_batch : model.batches) {
    Batch& dst = BatchFor(world, model_batch.texture, model_batch.emissive,
                          model_batch.alpha_test, model_batch.opacity);
    for (const auto& v : model_batch.verts) {
      const float wx = origin_x + v.px * cos_yaw + v.pz * sin_yaw;
      const float wy = v.py + static_cast<float>(lift);
      const float wz = origin_z - v.px * sin_yaw + v.pz * cos_yaw;
      const float nx = v.nx * cos_yaw + v.nz * sin_yaw;
      const float ny = v.ny;
      const float nz = -v.nx * sin_yaw + v.nz * cos_yaw;
      dst.verts.push_back({.px = wx,
                           .py = wy,
                           .pz = wz,
                           .nx = nx,
                           .ny = ny,
                           .nz = nz,
                           .u = v.u,
                           .v = v.v});
    }
  }
}

// Turns the cells of one level into batches, one method per kind of cell.
class SceneBuilder {
 public:
  // `level` must outlive the builder.
  SceneBuilder(const Level& level, const bool outdoor)
      : level_(level), tiles_(level.Tiles()), outdoor_(outdoor) {
    geometry_.world.reserve(32);
    geometry_.props.reserve(8);
  }

  SceneGeometry Build(const ModelLibrary& models) {
    for (int y = 0; y < level_.Height(); ++y) {
      for (int x = 0; x < level_.Width(); ++x) AddCell(x, y);
    }
    AddFurniture(models);
    AddDecals();
    return std::move(geometry_);
  }

 private:
  struct DecalPoint {
    double x, y, u, v;
  };

  static void ClipDecal(std::vector<DecalPoint>& poly, const bool x_axis,
                        const double bound, const bool lower) {
    std::vector<DecalPoint> out;
    if (poly.empty()) return;
    const auto coord = [&](const DecalPoint& p) { return x_axis ? p.x : p.y; };
    DecalPoint prev = poly.back();
    bool prev_in = lower ? coord(prev) >= bound : coord(prev) <= bound;
    for (const DecalPoint& cur : poly) {
      const bool cur_in = lower ? coord(cur) >= bound : coord(cur) <= bound;
      if (cur_in != prev_in) {
        const double t = (bound - coord(prev)) / (coord(cur) - coord(prev));
        out.push_back(
            {prev.x + t * (cur.x - prev.x), prev.y + t * (cur.y - prev.y),
             prev.u + t * (cur.u - prev.u), prev.v + t * (cur.v - prev.v)});
      }
      if (cur_in) out.push_back(cur);
      prev = cur;
      prev_in = cur_in;
    }
    poly = std::move(out);
  }

  struct ThinBox {
    float x0, z0, x1, z1;
    unsigned skip;
  };

  struct DecalBox {
    float x0, y0, z0, x1, y1, z1;
    unsigned skip;
  };

  std::vector<ThinBox> ThinBoxes(const int x, const int y) const {
    const float a = 0.5f - kThinWallThickness / 2;
    const float b = 0.5f + kThinWallThickness / 2;
    const unsigned arms = level_.ThinArms(x, y);
    const auto cap = [&](const unsigned face, const int nx, const int ny) {
      return tiles_.Solid(level_.At(nx, ny)) ? face : 0u;
    };
    std::vector<ThinBox> boxes = {
        {a, a, b, b,
         ((arms & 1u) ? kFaceNegZ : 0u) | ((arms & 2u) ? kFacePosZ : 0u) |
             ((arms & 4u) ? kFaceNegX : 0u) | ((arms & 8u) ? kFacePosX : 0u)}};
    if (arms & 1u)
      boxes.push_back({a, 0, b, a, kFacePosZ | cap(kFaceNegZ, x, y - 1)});
    if (arms & 2u)
      boxes.push_back({a, b, b, 1, kFaceNegZ | cap(kFacePosZ, x, y + 1)});
    if (arms & 4u)
      boxes.push_back({0, a, a, b, kFacePosX | cap(kFaceNegX, x - 1, y)});
    if (arms & 8u)
      boxes.push_back({b, a, 1, b, kFaceNegX | cap(kFacePosX, x + 1, y)});
    return boxes;
  }

  void AddGlassDecal(const Decal& d) {
    if (!level_.InBounds(d.x, d.y)) return;
    const char glyph = level_.At(d.x, d.y);
    if (tiles_[glyph].empty || tiles_.IsDoor(glyph) || !tiles_.Solid(glyph) ||
        !tiles_.Thin(glyph) || !tiles_[glyph].window)
      return;

    const bool north = d.face == DecalFace::kGlassNorth;
    const bool south = d.face == DecalFace::kGlassSouth;
    const bool west = d.face == DecalFace::kGlassWest;
    const bool horizontal = north || south;
    if (horizontal != ((level_.ThinArms(d.x, d.y) & 12u) != 0)) return;
    const double sign = (south || west) ? -1 : 1;
    const P3 normal = north   ? P3{0, 0, -1}
                      : south ? P3{0, 0, 1}
                      : west  ? P3{-1, 0, 0}
                              : P3{1, 0, 0};
    const double angle = d.rotation * std::numbers::pi / 180.0;
    const double c = std::cos(angle), s = std::sin(angle);
    const auto corner = [&](const double a, const double b, const double u,
                            const double v) {
      return DecalPoint{d.offset_u + a * c - b * s, d.offset_v + a * s + b * c,
                        u, v};
    };
    const double w = d.width / 2, h = d.height / 2;
    std::vector<DecalPoint> poly = {corner(-w, -h, 0, 0), corner(w, -h, 1, 0),
                                    corner(w, h, 1, 1), corner(-w, h, 0, 1)};
    ClipDecal(poly, true, kWindowJamb - 0.5, true);
    ClipDecal(poly, true, 0.5 - kWindowJamb, false);
    ClipDecal(poly, false, kWindowSill - kWallHeight / 2, true);
    ClipDecal(poly, false, kWindowHead - kWallHeight / 2, false);

    Batch batch;
    batch.texture = d.texture;
    const auto push = [&](const DecalPoint& p) {
      const float along = static_cast<float>(0.5 + sign * p.x);
      const float y = static_cast<float>(kWallHeight / 2 + p.y);
      const P3 pos = horizontal ? P3{d.x + along, y, d.y + 0.5f}
                                : P3{d.x + 0.5f, y, d.y + along};
      PushVertex(batch, pos, normal.x, normal.y, normal.z,
                 static_cast<float>(p.u), static_cast<float>(p.v));
    };
    for (size_t i = 1; i + 1 < poly.size(); ++i) {
      const DecalPoint& a = poly[0];
      const DecalPoint& b = poly[i];
      const DecalPoint& c = poly[i + 1];
      if ((b.x - a.x) * (c.y - a.y) == (b.y - a.y) * (c.x - a.x)) continue;
      push(a);
      push(b);
      push(c);
    }
    if (!batch.verts.empty()) geometry_.decals.push_back(std::move(batch));
  }

  void AddThinDecal(const Decal& d) {
    if (!level_.InBounds(d.x, d.y)) return;
    const char glyph = level_.At(d.x, d.y);
    if (tiles_[glyph].empty || tiles_.IsDoor(glyph) || !tiles_.Solid(glyph) ||
        !tiles_.Thin(glyph))
      return;

    const bool north = d.face == DecalFace::kThinNorth;
    const bool south = d.face == DecalFace::kThinSouth;
    const bool west = d.face == DecalFace::kThinWest;
    const bool horizontal = north || south;
    const unsigned face = north   ? kFaceNegZ
                          : south ? kFacePosZ
                          : west  ? kFaceNegX
                                  : kFacePosX;
    const P3 normal = north   ? P3{0, 0, -1}
                      : south ? P3{0, 0, 1}
                      : west  ? P3{-1, 0, 0}
                              : P3{1, 0, 0};
    const double sign = (south || west) ? -1 : 1;
    using Point = DecalPoint;
    const double angle = d.rotation * std::numbers::pi / 180.0;
    const double c = std::cos(angle), s = std::sin(angle);
    const auto corner = [&](const double a, const double b, const double u,
                            const double v) {
      return Point{d.offset_u + a * c - b * s, d.offset_v + a * s + b * c, u,
                   v};
    };
    const double w = d.width / 2, h = d.height / 2;
    const std::vector<Point> quad = {corner(-w, -h, 0, 0), corner(w, -h, 1, 0),
                                     corner(w, h, 1, 1), corner(-w, h, 0, 1)};
    Batch batch;
    batch.texture = d.texture;
    const bool window = tiles_[glyph].window;
    const std::vector<ThinBox> thin_boxes = ThinBoxes(d.x, d.y);
    std::vector<DecalBox> boxes;
    for (const ThinBox& box : thin_boxes) {
      if (window) {
        boxes.push_back(
            {box.x0, 0, box.z0, box.x1, kWindowSill, box.z1, box.skip});
        boxes.push_back({box.x0, kWindowHead, box.z0, box.x1, kWallHeight,
                         box.z1, box.skip});
      } else {
        boxes.push_back(
            {box.x0, 0, box.z0, box.x1, kWallHeight, box.z1, box.skip});
      }
    }
    if (window) {
      const float a = 0.5f - kThinWallThickness / 2;
      const float b = 0.5f + kThinWallThickness / 2;
      const float lo = kWindowJamb, hi = 1.0f - kWindowJamb;
      const float mid = 0.5f * (kWindowSill + kWindowHead);
      if (level_.ThinArms(d.x, d.y) & 12u) {
        boxes.push_back({0, kWindowSill, a, lo, kWindowHead, b, 0});
        boxes.push_back({hi, kWindowSill, a, 1, kWindowHead, b, 0});
        boxes.push_back({lo, kWindowSill, a - 0.03f, hi, kWindowSill + 0.04f,
                         b + 0.03f, 0});
        boxes.push_back({0.5f - 0.02f, kWindowSill, 0.5f - 0.03f, 0.5f + 0.02f,
                         kWindowHead, 0.5f + 0.03f, 0});
        boxes.push_back(
            {lo, mid - 0.02f, 0.5f - 0.03f, hi, mid + 0.02f, 0.5f + 0.03f, 0});
      } else {
        boxes.push_back({a, kWindowSill, 0, b, kWindowHead, lo, 0});
        boxes.push_back({a, kWindowSill, hi, b, kWindowHead, 1, 0});
        boxes.push_back({a - 0.03f, kWindowSill, lo, b + 0.03f,
                         kWindowSill + 0.04f, hi, 0});
        boxes.push_back({0.5f - 0.03f, kWindowSill, 0.5f - 0.02f, 0.5f + 0.03f,
                         kWindowHead, 0.5f + 0.02f, 0});
        boxes.push_back(
            {0.5f - 0.03f, mid - 0.02f, lo, 0.5f + 0.03f, mid + 0.02f, hi, 0});
      }
    }
    struct Rect {
      double x0, y0, x1, y1;
    };
    for (size_t bi = 0; bi < boxes.size(); ++bi) {
      const DecalBox& box = boxes[bi];
      if (box.skip & face) continue;
      const double lo = horizontal ? box.x0 : box.z0;
      const double hi = horizontal ? box.x1 : box.z1;
      const double u0 = sign > 0 ? lo - 0.5 : 0.5 - hi;
      const double u1 = sign > 0 ? hi - 0.5 : 0.5 - lo;
      const float plane = north   ? box.z0
                          : south ? box.z1
                          : west  ? box.x0
                                  : box.x1;
      std::vector<Rect> regions = {
          {u0, box.y0 - kWallHeight / 2, u1, box.y1 - kWallHeight / 2}};
      if (window)
        for (size_t j = 0; j < boxes.size(); ++j) {
          if (j == bi) continue;
          const DecalBox& other = boxes[j];
          const float near = north   ? other.z0
                             : south ? other.z1
                             : west  ? other.x0
                                     : other.x1;
          const float far = north   ? other.z1
                            : south ? other.z0
                            : west  ? other.x1
                                    : other.x0;
          const bool negative = north || west;
          const bool covers = negative ? (near < plane && far >= plane)
                                       : (near > plane && far <= plane);
          const bool duplicate =
              near == plane && j < bi && !(other.skip & face);
          if (!covers && !duplicate) continue;
          const double o_lo = horizontal ? other.x0 : other.z0;
          const double o_hi = horizontal ? other.x1 : other.z1;
          const Rect cut = {
              sign > 0 ? o_lo - 0.5 : 0.5 - o_hi, other.y0 - kWallHeight / 2,
              sign > 0 ? o_hi - 0.5 : 0.5 - o_lo, other.y1 - kWallHeight / 2};
          std::vector<Rect> remaining;
          for (const Rect& r : regions) {
            const double x0 = std::max(r.x0, cut.x0),
                         x1 = std::min(r.x1, cut.x1);
            const double y0 = std::max(r.y0, cut.y0),
                         y1 = std::min(r.y1, cut.y1);
            if (x0 >= x1 || y0 >= y1) {
              remaining.push_back(r);
              continue;
            }
            if (r.x0 < x0) remaining.push_back({r.x0, r.y0, x0, r.y1});
            if (x1 < r.x1) remaining.push_back({x1, r.y0, r.x1, r.y1});
            if (r.y0 < y0) remaining.push_back({x0, r.y0, x1, y0});
            if (y1 < r.y1) remaining.push_back({x0, y1, x1, r.y1});
          }
          regions = std::move(remaining);
        }
      const auto push = [&](const Point& p) {
        const float along = static_cast<float>(0.5 + sign * p.x);
        const float y = static_cast<float>(kWallHeight / 2 + p.y);
        const P3 pos = horizontal ? P3{d.x + along, y, d.y + plane}
                                  : P3{d.x + plane, y, d.y + along};
        PushVertex(batch, pos, normal.x, normal.y, normal.z,
                   static_cast<float>(p.u), static_cast<float>(p.v));
      };
      for (const Rect& r : regions) {
        std::vector<Point> poly = quad;
        ClipDecal(poly, true, r.x0, true);
        ClipDecal(poly, true, r.x1, false);
        ClipDecal(poly, false, r.y0, true);
        ClipDecal(poly, false, r.y1, false);
        for (size_t i = 1; i + 1 < poly.size(); ++i) {
          const Point& a = poly[0];
          const Point& b = poly[i];
          const Point& c = poly[i + 1];
          if ((b.x - a.x) * (c.y - a.y) == (b.y - a.y) * (c.x - a.x)) continue;
          push(a);
          push(b);
          push(c);
        }
      }
    }
    if (!batch.verts.empty()) geometry_.decals.push_back(std::move(batch));
  }

  char ThinQuarterRoom(const int x, const int y, const int dx,
                       const int dy) const {
    const auto open = [&](const char c) {
      return !tiles_.Solid(c) && !tiles_[c].empty;
    };
    for (const char c : {level_.At(x, y + dy), level_.At(x + dx, y),
                         level_.At(x + dx, y + dy)}) {
      if (open(c)) return c;
    }
    return '\0';
  }

  void AddDecals() {
    for (const Decal& d : level_.Decals()) {
      if (d.face == DecalFace::kGlassNorth ||
          d.face == DecalFace::kGlassSouth || d.face == DecalFace::kGlassWest ||
          d.face == DecalFace::kGlassEast) {
        AddGlassDecal(d);
        continue;
      }
      if (d.face == DecalFace::kThinNorth || d.face == DecalFace::kThinSouth ||
          d.face == DecalFace::kThinWest || d.face == DecalFace::kThinEast) {
        AddThinDecal(d);
        continue;
      }
      if (d.face == DecalFace::kFloor || d.face == DecalFace::kCeiling) {
        const char glyph = level_.At(d.x, d.y);
        const TileInfo& tile = tiles_[glyph];
        if (tile.empty || (tiles_.Solid(glyph) && !tiles_.Thin(glyph) &&
                           !tiles_.IsDoor(glyph)))
          continue;
        if (d.face == DecalFace::kCeiling && outdoor_) continue;
        if (!tiles_.Solid(glyph) && !tiles_.IsDoor(glyph) &&
            ((d.face == DecalFace::kFloor && tile.open_floor) ||
             (d.face == DecalFace::kCeiling && tile.open_ceiling)))
          continue;
      } else {
        int nx = d.x, ny = d.y;
        if (d.face == DecalFace::kNorth) --ny;
        if (d.face == DecalFace::kSouth) ++ny;
        if (d.face == DecalFace::kWest) --nx;
        if (d.face == DecalFace::kEast) ++nx;
        const char receiver = level_.At(nx, ny);
        if (!level_.InBounds(nx, ny) || tiles_[receiver].empty ||
            !tiles_.Solid(receiver) || tiles_.Thin(receiver) ||
            tiles_.IsDoor(receiver) || !WallFaceOpen(nx, ny, d.x, d.y))
          continue;
      }
      const float x = static_cast<float>(d.x), z = static_cast<float>(d.y);
      P3 center{}, u{}, v{}, normal{};
      switch (d.face) {
        case DecalFace::kNorth:
          center = {x + 0.5f, kWallHeight / 2, z};
          u = {1, 0, 0};
          v = {0, 1, 0};
          normal = {0, 0, -1};
          break;
        case DecalFace::kSouth:
          center = {x + 0.5f, kWallHeight / 2, z + 1};
          u = {-1, 0, 0};
          v = {0, 1, 0};
          normal = {0, 0, 1};
          break;
        case DecalFace::kWest:
          center = {x, kWallHeight / 2, z + 0.5f};
          u = {0, 0, -1};
          v = {0, 1, 0};
          normal = {-1, 0, 0};
          break;
        case DecalFace::kEast:
          center = {x + 1, kWallHeight / 2, z + 0.5f};
          u = {0, 0, 1};
          v = {0, 1, 0};
          normal = {1, 0, 0};
          break;
        case DecalFace::kFloor:
          center = {x + 0.5f, 0, z + 0.5f};
          u = {1, 0, 0};
          v = {0, 0, 1};
          normal = {0, 1, 0};
          break;
        case DecalFace::kCeiling:
          center = {x + 0.5f, kWallHeight, z + 0.5f};
          u = {1, 0, 0};
          v = {0, 0, 1};
          normal = {0, -1, 0};
          break;
        case DecalFace::kThinNorth:
        case DecalFace::kThinSouth:
        case DecalFace::kThinWest:
        case DecalFace::kThinEast:
        case DecalFace::kGlassNorth:
        case DecalFace::kGlassSouth:
        case DecalFace::kGlassWest:
        case DecalFace::kGlassEast:
          break;
      }
      const double angle = d.rotation * std::numbers::pi / 180.0;
      const double c = std::cos(angle), s = std::sin(angle);
      const auto point = [&](const double a, const double b, const double tu,
                             const double tv) {
        return DecalPoint{d.offset_u + a * c - b * s,
                          d.offset_v + a * s + b * c, tu, tv};
      };
      const double w = d.width / 2, h = d.height / 2;
      std::vector<DecalPoint> poly = {point(-w, -h, 0, 0), point(w, -h, 1, 0),
                                      point(w, h, 1, 1), point(-w, h, 0, 1)};
      const double vertical =
          d.face == DecalFace::kFloor || d.face == DecalFace::kCeiling
              ? 0.5
              : kWallHeight / 2;
      ClipDecal(poly, true, -0.5, true);
      ClipDecal(poly, true, 0.5, false);
      ClipDecal(poly, false, -vertical, true);
      ClipDecal(poly, false, vertical, false);
      Batch batch;
      batch.texture = d.texture;
      const auto push = [&](const DecalPoint& p) {
        const float du = static_cast<float>(p.x), dv = static_cast<float>(p.y);
        PushVertex(
            batch,
            {center.x + u.x * du + v.x * dv, center.y + u.y * du + v.y * dv,
             center.z + u.z * du + v.z * dv},
            normal.x, normal.y, normal.z, static_cast<float>(p.u),
            static_cast<float>(p.v));
      };
      const auto emit = [&](const std::vector<DecalPoint>& p) {
        for (size_t i = 1; i + 1 < p.size(); ++i) {
          const auto& a = p[0];
          const auto& b = p[i];
          const auto& c = p[i + 1];
          if ((b.x - a.x) * (c.y - a.y) == (b.y - a.y) * (c.x - a.x)) continue;
          push(a);
          push(b);
          push(c);
        }
      };
      const char glyph = level_.At(d.x, d.y);
      if ((d.face == DecalFace::kFloor || d.face == DecalFace::kCeiling) &&
          tiles_.Solid(glyph) && tiles_.Thin(glyph) && !tiles_.IsDoor(glyph)) {
        for (int dz : {-1, 1})
          for (int dx : {-1, 1}) {
            if (!ThinQuarterRoom(d.x, d.y, dx, dz)) continue;
            auto quarter = poly;
            ClipDecal(quarter, true, dx < 0 ? -0.5 : 0, true);
            ClipDecal(quarter, true, dx < 0 ? 0 : 0.5, false);
            ClipDecal(quarter, false, dz < 0 ? -0.5 : 0, true);
            ClipDecal(quarter, false, dz < 0 ? 0 : 0.5, false);
            emit(quarter);
          }
      } else {
        emit(poly);
      }
      if (!batch.verts.empty()) geometry_.decals.push_back(std::move(batch));
    }
  }
  // Short for the wall quads below. The jamb constants are the wall left either
  // side of a door's hole, where the far jamb starts (1 - kDoorJamb), and how
  // far the posts of a room entrance sit from the cell sides.
  static constexpr float kH = kWallHeight;
  static constexpr float kDoorJamb = 0.15f;
  static constexpr float kDoorJambFar = 0.85f;
  static constexpr float kRoomFrameInset = 0.02f;

  // Dispatches on the kind of cell. An empty (open air) tile draws nothing but
  // any furniture placed there.
  void AddCell(const int x, const int y) {
    const char glyph = level_.At(x, y);
    if (tiles_[glyph].empty) {
      return;
    }
    if (tiles_.IsDoor(glyph)) return AddDoorCell(x, y);
    if (tiles_.Solid(glyph)) return AddWallCell(x, y, glyph);
    AddFloorCell(x, y, glyph);
  }

  // Floor and, indoors, ceiling of the cell at (fx, fz); the sky is drawn
  // instead of a ceiling outdoors. Either can be left out for a stairwell.
  void AddFloorAndCeiling(const float fx, const float fz,
                          const std::string& floor_texture,
                          const bool floor = true, const bool ceiling = true) {
    if (floor)
      AddQuad(BatchFor(geometry_.world, floor_texture, false, false),
              {.x = fx, .y = 0, .z = fz}, {.x = fx + 1, .y = 0, .z = fz},
              {.x = fx + 1, .y = 0, .z = fz + 1},
              {.x = fx, .y = 0, .z = fz + 1}, 0, 1, 0, 1, 1);
    if (!outdoor_ && ceiling) {
      AddQuad(BatchFor(geometry_.world, "ceiling", false, false),
              {.x = fx, .y = kH, .z = fz}, {.x = fx, .y = kH, .z = fz + 1},
              {.x = fx + 1, .y = kH, .z = fz + 1},
              {.x = fx + 1, .y = kH, .z = fz}, 0, -1, 0, 1, 1);
    }
  }

  // A doorway assembly: a wall body around a door-shaped hole, wooden posts and
  // a lintel. The body takes the texture of the wall the door sits in, and the
  // thickness of that wall (a thin wall's door is as thin as the wall). The
  // swinging panel itself is drawn per frame by the renderer.
  void AddDoorCell(const int x, const int y) {
    const auto fx = static_cast<float>(x), fz = static_cast<float>(y);
    std::vector<Batch>& world = geometry_.world;
    const bool along_x = DoorPassageAlongX(level_, x, y);
    AddFloorAndCeiling(fx, fz, "wood");

    const char side = along_x ? level_.At(x, y - 1) : level_.At(x - 1, y);
    Batch& wall = BatchFor(world, tiles_.WallTexture(side), false, false);

    const bool thin = tiles_.Thin(side);
    const float t0 = thin ? 0.5f - kThinWallThickness / 2 : 0.0f;
    const float t1 = thin ? 0.5f + kThinWallThickness / 2 : 1.0f;
    if (along_x) {
      AddBox(wall, fx + t0, 0, fz, fx + t1, kH, fz + kDoorJamb);
      AddBox(wall, fx + t0, 0, fz + kDoorJambFar, fx + t1, kH, fz + 1);
      AddBox(wall, fx + t0, kLintelTop, fz + kDoorJamb, fx + t1, kH,
             fz + kDoorJambFar);
    } else {
      AddBox(wall, fx, 0, fz + t0, fx + kDoorJamb, kH, fz + t1);
      AddBox(wall, fx + kDoorJambFar, 0, fz + t0, fx + 1, kH, fz + t1);
      AddBox(wall, fx + kDoorJamb, kLintelTop, fz + t0, fx + kDoorJambFar, kH,
             fz + t1);
    }
    AddDoorFrame(world, fx, fz, along_x, kDoorJamb, kLintelTop);
    AddDarkCaps(x, y, along_x);
  }

  // Caps a passage end that runs into solid wall with a black face. Through an
  // open door you must see darkness, not whatever the background pass paints
  // there (outdoors the night sky would show through the house).
  void AddDarkCaps(const int x, const int y, const bool along_x) {
    const auto fx = static_cast<float>(x), fz = static_cast<float>(y);
    Batch& dark = BatchFor(geometry_.world, "void", false, false);
    if (along_x) {
      if (tiles_.Solid(level_.At(x - 1, y))) {
        AddQuad(dark, {.x = fx, .y = 0, .z = fz},
                {.x = fx, .y = 0, .z = fz + 1}, {.x = fx, .y = kH, .z = fz + 1},
                {.x = fx, .y = kH, .z = fz}, -1, 0, 0, 1, kH);
      }
      if (tiles_.Solid(level_.At(x + 1, y))) {
        AddQuad(dark, {.x = fx + 1, .y = 0, .z = fz + 1},
                {.x = fx + 1, .y = 0, .z = fz}, {.x = fx + 1, .y = kH, .z = fz},
                {.x = fx + 1, .y = kH, .z = fz + 1}, 1, 0, 0, 1, kH);
      }
    } else {
      if (tiles_.Solid(level_.At(x, y - 1))) {
        AddQuad(dark, {.x = fx + 1, .y = 0, .z = fz},
                {.x = fx, .y = 0, .z = fz}, {.x = fx, .y = kH, .z = fz},
                {.x = fx + 1, .y = kH, .z = fz}, 0, 0, -1, 1, kH);
      }
      if (tiles_.Solid(level_.At(x, y + 1))) {
        AddQuad(dark, {.x = fx, .y = 0, .z = fz + 1},
                {.x = fx + 1, .y = 0, .z = fz + 1},
                {.x = fx + 1, .y = kH, .z = fz + 1},
                {.x = fx, .y = kH, .z = fz + 1}, 0, 0, 1, 1, kH);
      }
    }
  }

  // Whether the face of the wall at (wall_x, wall_y) toward the neighbor (nx,
  // ny) is visible: it is wherever the neighbor is not a wall. A face into an
  // open door cell whose passage runs toward this wall is skipped; that is the
  // darkness seen through an open door.
  [[nodiscard]] bool WallFaceOpen(const int wall_x, const int wall_y,
                                  const int nx, const int ny) const {
    const char neighbour = level_.At(nx, ny);
    if (tiles_.Solid(neighbour)) return false;
    if (tiles_.IsOpenDoor(neighbour)) {
      const bool passage_along_x = DoorPassageAlongX(level_, nx, ny);
      if (passage_along_x && ny == wall_y) {
        return false;
      }
      if (!passage_along_x && nx == wall_x) return false;
    }
    return true;
  }

  // A thin wall: a post at the middle of the cell and an arm toward every
  // solid or door neighbor, so walls join up, with the floor and ceiling left
  // showing either side of it in the floor of the room on that side. A window
  // is the same wall with a hole in it, a sill, a frame and glass. The arms
  // bits of Level::ThinArms are 1 north (-z), 2 south (+z), 4 west (-x) and 8
  // east (+x).
  void AddThinWallCell(const int x, const int y, const char glyph) {
    const auto fx = static_cast<float>(x), fz = static_cast<float>(y);
    const float a = 0.5f - kThinWallThickness / 2;
    const float b = 0.5f + kThinWallThickness / 2;
    const unsigned arms = level_.ThinArms(x, y);
    Batch& wall =
        BatchFor(geometry_.world, tiles_.WallTexture(glyph), false, false);
    const bool window = tiles_[glyph].window;
    // Arms to the west or east.
    const bool along_x = (arms & 12u) != 0;

    // The post and the arms are one wall, so the faces where they meet, and
    // where an arm reaches the next wall cell, are skipped: they would show as
    // hollow boxes wherever the wall is cut open. Boxes are in cell-local x and
    // z.
    const std::vector<ThinBox> boxes = ThinBoxes(x, y);
    // A plain wall is whole; a window is whole below the sill and above the
    // head, and between them only the ends of the cell stand (the jambs) and
    // the opening is glazed.
    if (!window) {
      for (const ThinBox& bx : boxes)
        AddBox(wall, fx + bx.x0, 0, fz + bx.z0, fx + bx.x1, kH, fz + bx.z1,
               bx.skip);
    } else {
      constexpr float kSill = kWindowSill, kHead = kWindowHead,
                      kJamb = kWindowJamb;
      for (const ThinBox& bx : boxes) {
        AddBox(wall, fx + bx.x0, 0, fz + bx.z0, fx + bx.x1, kSill, fz + bx.z1,
               bx.skip);
        AddBox(wall, fx + bx.x0, kHead, fz + bx.z0, fx + bx.x1, kH, fz + bx.z1,
               bx.skip);
      }
      Batch& wood = BatchFor(geometry_.world, "door", false, false);
      Batch& glass = BatchFor(geometry_.world, "glass", false, false);
      const float lo = kJamb, hi = 1.0f - kJamb;
      // The wall runs along x: the opening spans x and its faces look along z.
      // The sill is the wooden board proud of the wall; the mullion and the
      // transom cross the glass.
      if (along_x) {
        AddBox(wall, fx, kSill, fz + a, fx + lo, kHead, fz + b);
        AddBox(wall, fx + hi, kSill, fz + a, fx + 1, kHead, fz + b);
        AddQuad(glass, {.x = fx + lo, .y = kSill, .z = fz + 0.5f},
                {.x = fx + hi, .y = kSill, .z = fz + 0.5f},
                {.x = fx + hi, .y = kHead, .z = fz + 0.5f},
                {.x = fx + lo, .y = kHead, .z = fz + 0.5f}, 0, 0, 1, 1, 1);
        AddBox(wood, fx + lo, kSill, fz + a - 0.03f, fx + hi, kSill + 0.04f,
               fz + b + 0.03f);
        AddBox(wood, fx + 0.5f - 0.02f, kSill, fz + 0.5f - 0.03f,
               fx + 0.5f + 0.02f, kHead, fz + 0.5f + 0.03f);
        AddBox(wood, fx + lo, 0.5f * (kSill + kHead) - 0.02f, fz + 0.5f - 0.03f,
               fx + hi, 0.5f * (kSill + kHead) + 0.02f, fz + 0.5f + 0.03f);
      } else {
        AddBox(wall, fx + a, kSill, fz, fx + b, kHead, fz + lo);
        AddBox(wall, fx + a, kSill, fz + hi, fx + b, kHead, fz + 1);
        AddQuad(glass, {.x = fx + 0.5f, .y = kSill, .z = fz + lo},
                {.x = fx + 0.5f, .y = kSill, .z = fz + hi},
                {.x = fx + 0.5f, .y = kHead, .z = fz + hi},
                {.x = fx + 0.5f, .y = kHead, .z = fz + lo}, 1, 0, 0, 1, 1);
        AddBox(wood, fx + a - 0.03f, kSill, fz + lo, fx + b + 0.03f,
               kSill + 0.04f, fz + hi);
        AddBox(wood, fx + 0.5f - 0.03f, kSill, fz + 0.5f - 0.02f,
               fx + 0.5f + 0.03f, kHead, fz + 0.5f + 0.02f);
        AddBox(wood, fx + 0.5f - 0.03f, 0.5f * (kSill + kHead) - 0.02f, fz + lo,
               fx + 0.5f + 0.03f, 0.5f * (kSill + kHead) + 0.02f, fz + hi);
      }
    }

    // Floor and ceiling in the four quarters of the cell. Each takes the room
    // beside it, or in the pocket of a corner where both edge neighbors are
    // wall, the room diagonally across.
    struct Quarter {
      float qx, qz;
      int dx, dy;
    };
    const Quarter quarters[4] = {
        {0, 0, -1, -1}, {0.5f, 0, 1, -1}, {0, 0.5f, -1, 1}, {0.5f, 0.5f, 1, 1}};
    for (const Quarter& q : quarters) {
      const char open_glyph = ThinQuarterRoom(x, y, q.dx, q.dy);
      if (open_glyph == '\0') continue;
      const std::string floor =
          tiles_.IsDoor(open_glyph) ? "wood" : tiles_.FloorTexture(open_glyph);
      const float qx = fx + q.qx, qz = fz + q.qz;
      AddQuad(BatchFor(geometry_.world, floor, false, false),
              {.x = qx, .y = 0, .z = qz}, {.x = qx + 0.5f, .y = 0, .z = qz},
              {.x = qx + 0.5f, .y = 0, .z = qz + 0.5f},
              {.x = qx, .y = 0, .z = qz + 0.5f}, 0, 1, 0, 0.5f, 0.5f);
      if (!outdoor_) {
        AddQuad(BatchFor(geometry_.world, "ceiling", false, false),
                {.x = qx, .y = kH, .z = qz}, {.x = qx, .y = kH, .z = qz + 0.5f},
                {.x = qx + 0.5f, .y = kH, .z = qz + 0.5f},
                {.x = qx + 0.5f, .y = kH, .z = qz}, 0, -1, 0, 0.5f, 0.5f);
      }
    }
  }

  // A solid cell: a quad on each side that faces open space.
  void AddWallCell(const int x, const int y, const char glyph) {
    if (tiles_.Thin(glyph)) return AddThinWallCell(x, y, glyph);
    const auto fx = static_cast<float>(x), fz = static_cast<float>(y);

    // A picture is one object, not a repeating material: stretch it once over
    // the wall instead of tiling it kWallHeight times.
    const float v_max = tiles_[glyph].wall_is_object ? 1.0f : kH;
    Batch& b =
        BatchFor(geometry_.world, tiles_.WallTexture(glyph), false, false);
    if (WallFaceOpen(x, y, x + 1, y)) {
      AddQuad(b, {.x = fx + 1, .y = 0, .z = fz},
              {.x = fx + 1, .y = 0, .z = fz + 1},
              {.x = fx + 1, .y = kH, .z = fz + 1},
              {.x = fx + 1, .y = kH, .z = fz}, -1, 0, 0, 1, v_max);
    }
    if (WallFaceOpen(x, y, x - 1, y)) {
      AddQuad(b, {.x = fx, .y = 0, .z = fz + 1}, {.x = fx, .y = 0, .z = fz},
              {.x = fx, .y = kH, .z = fz}, {.x = fx, .y = kH, .z = fz + 1}, 1,
              0, 0, 1, v_max);
    }
    if (WallFaceOpen(x, y, x, y + 1)) {
      AddQuad(b, {.x = fx + 1, .y = 0, .z = fz + 1},
              {.x = fx, .y = 0, .z = fz + 1}, {.x = fx, .y = kH, .z = fz + 1},
              {.x = fx + 1, .y = kH, .z = fz + 1}, 0, 0, -1, 1, v_max);
    }
    if (WallFaceOpen(x, y, x, y - 1)) {
      AddQuad(b, {.x = fx, .y = 0, .z = fz}, {.x = fx + 1, .y = 0, .z = fz},
              {.x = fx + 1, .y = kH, .z = fz}, {.x = fx, .y = kH, .z = fz}, 0,
              0, 1, 1, v_max);
    }
  }

  // An open cell: floor, ceiling, the entrance frame of a room doorway, and the
  // tile's sprite if it has one.
  void AddFloorCell(const int x, const int y, const char glyph) {
    const auto fx = static_cast<float>(x), fz = static_cast<float>(y);
    AddFloorAndCeiling(fx, fz, tiles_.FloorTexture(glyph),
                       !tiles_[glyph].open_floor, !tiles_[glyph].open_ceiling);
    if (!outdoor_ && IsDoorwayCell(level_, x, y)) AddRoomEntranceFrame(x, y);

    const TileInfo& tile = tiles_[glyph];
    if (tile.sprite.empty()) return;
    AddCross(BatchFor(geometry_.props, tile.sprite, false, true), fx + 0.5f,
             fz + 0.5f, tile.sprite_size, tile.sprite_size, tile.sprite_lift);
  }

  // Gives a room entrance a wooden door frame, on the room side of a two-cell
  // opening. A passage that leads up to a real door already has that door's
  // frame, and so does the second cell of a deep opening.
  void AddRoomEntranceFrame(const int x, const int y) {
    const bool along_z =
        tiles_.Solid(level_.At(x - 1, y)) && tiles_.Solid(level_.At(x + 1, y));
    const bool prev_is_doorway = along_z ? IsDoorwayCell(level_, x, y - 1)
                                         : IsDoorwayCell(level_, x - 1, y);

    const bool next_to_door = along_z ? (tiles_.IsDoor(level_.At(x, y - 1)) ||
                                         tiles_.IsDoor(level_.At(x, y + 1)))
                                      : (tiles_.IsDoor(level_.At(x - 1, y)) ||
                                         tiles_.IsDoor(level_.At(x + 1, y)));
    if (!prev_is_doorway && !next_to_door) {
      AddDoorFrame(geometry_.world, static_cast<float>(x),
                   static_cast<float>(y), !along_z, kRoomFrameInset, kH);
    }
  }

  // Furniture models from the level, placed on their cells. Props may sit off
  // center and above the floor. Unknown model names are skipped rather than
  // failing.
  void AddFurniture(const ModelLibrary& models) {
    for (const auto& placement : level_.Furniture()) {
      const Model* model = models.Find(placement.model);
      if (!model) {
        continue;
      }

      AppendModel(geometry_.world, *model, placement.x + 0.5 + placement.off_x,
                  placement.y + 0.5 + placement.off_z, placement.yaw_deg,
                  placement.lift);
    }
  }

  const Level& level_;
  const TileSet& tiles_;
  bool outdoor_;
  SceneGeometry geometry_;
};
}

std::optional<Batch> BuildModelDecal(const Placement& placement,
                                     const ModelDecal& decal) {
  const auto finite3 = [](const std::array<float, 3>& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
  };
  const double cx = placement.x + 0.5 + placement.off_x;
  const double cz = placement.y + 0.5 + placement.off_z;
  const double limit = std::numeric_limits<float>::max();
  if (decal.texture.empty() || !finite3(decal.center) ||
      !finite3(decal.normal) || !finite3(decal.tangent) ||
      !std::isfinite(decal.width) || !std::isfinite(decal.height) ||
      decal.width <= 0 || decal.height <= 0 ||
      !std::isfinite(decal.rotation_deg) || !std::isfinite(placement.yaw_deg) ||
      !std::isfinite(cx) || !std::isfinite(cz) ||
      !std::isfinite(placement.lift) || std::abs(cx) > limit ||
      std::abs(cz) > limit || std::abs(placement.lift) > limit ||
      !std::isfinite(placement.yaw_deg * std::numbers::pi))
    return std::nullopt;

  using Vec = std::array<double, 3>;
  const auto dot = [](const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  const auto cross = [](const Vec& a, const Vec& b) -> Vec {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
  };
  Vec n{decal.normal[0], decal.normal[1], decal.normal[2]};
  Vec t{decal.tangent[0], decal.tangent[1], decal.tangent[2]};
  const double nl = std::sqrt(dot(n, n)), tl = std::sqrt(dot(t, t));
  if (nl == 0 || tl == 0) return std::nullopt;
  for (double& v : n) v /= nl;
  for (double& v : t) v /= tl;
  const double projection = dot(n, t);
  for (int i = 0; i < 3; ++i) t[i] -= projection * n[i];
  const double projected_length = std::sqrt(dot(t, t));
  if (projected_length <= 1e-6) return std::nullopt;
  for (double& v : t) v /= projected_length;
  const Vec b = cross(n, t);
  const double angle =
      std::remainder(decal.rotation_deg, 360.0) * std::numbers::pi / 180.0;
  const double c = std::cos(angle), s = std::sin(angle);
  Vec u{}, v{};
  for (int i = 0; i < 3; ++i) {
    u[i] = (c * t[i] + s * b[i]) * (decal.width * 0.5);
    v[i] = (-s * t[i] + c * b[i]) * (decal.height * 0.5);
  }
  P3 corners[4];
  constexpr int signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
  for (int i = 0; i < 4; ++i) {
    float p[3];
    for (int j = 0; j < 3; ++j) {
      const double value =
          decal.center[j] + signs[i][0] * u[j] + signs[i][1] * v[j];
      if (!std::isfinite(value) || std::abs(value) > limit) return std::nullopt;
      p[j] = static_cast<float>(value);
    }
    corners[i] = {p[0], p[1], p[2]};
  }
  Model model;
  model.batches.emplace_back();
  Batch& local = model.batches.back();
  local.texture = decal.texture;
  AddQuad(local, corners[0], corners[1], corners[2], corners[3],
          static_cast<float>(n[0]), static_cast<float>(n[1]),
          static_cast<float>(n[2]), 1, 1);
  std::vector<Batch> result;
  AppendModel(result, model, cx, cz, placement.yaw_deg, placement.lift);
  const auto& vertices = result[0].verts;
  for (const auto& vertex : vertices) {
    if (!std::isfinite(vertex.px) || !std::isfinite(vertex.py) ||
        !std::isfinite(vertex.pz))
      return std::nullopt;
  }
  // Both triangles must survive rounding to world-space vertex precision.
  for (int i : {0, 3}) {
    const auto& a = vertices[i];
    const auto& q = vertices[i + 1];
    const auto& r = vertices[i + 2];
    const Vec ab{double(q.px) - a.px, double(q.py) - a.py, double(q.pz) - a.pz};
    const Vec ac{double(r.px) - a.px, double(r.py) - a.py, double(r.pz) - a.pz};
    const Vec area = cross(ab, ac);
    if (dot(area, area) == 0) return std::nullopt;
  }
  return std::move(result[0]);
}

SceneGeometry BuildSceneGeometry(const Level& level, const ModelLibrary& models,
                                 const bool outdoor) {
  return SceneBuilder(level, outdoor).Build(models);
}

SceneGeometry BuildSceneGeometry(const Level& level, const ModelLibrary& models,
                                 const bool outdoor, const float y) {
  SceneGeometry g = BuildSceneGeometry(level, models, outdoor);
  if (y == 0.0f) return g;
  for (auto* part : {&g.world, &g.props, &g.decals})
    for (Batch& b : *part)
      for (Vertex& v : b.verts) v.py += y;
  return g;
}

void AppendSceneGeometry(SceneGeometry& into, SceneGeometry&& from) {
  for (Batch& b : from.world) into.world.push_back(std::move(b));
  for (Batch& b : from.props) into.props.push_back(std::move(b));
  for (Batch& b : from.decals) into.decals.push_back(std::move(b));
}

std::vector<Batch> BuildDoorPanel() {
  std::vector<Batch> out;
  Batch& b = BatchFor(out, "door", false, false);
  const float w = kDoorPanelWidth, h = kDoorPanelHeight;
  // Half the panel thickness. The two large faces carry the whole door image
  // (planks and the brass knob); the thin edges are the hinge side, the knob
  // side and the top.
  const float t = 0.025f;

  AddQuad(b, {.x = 0, .y = 0, .z = t}, {.x = w, .y = 0, .z = t},
          {.x = w, .y = h, .z = t}, {.x = 0, .y = h, .z = t}, 0, 0, 1, 1, 1);
  AddQuad(b, {.x = 0, .y = 0, .z = -t}, {.x = w, .y = 0, .z = -t},
          {.x = w, .y = h, .z = -t}, {.x = 0, .y = h, .z = -t}, 0, 0, -1, 1, 1);

  AddQuad(b, {.x = 0, .y = 0, .z = -t}, {.x = 0, .y = 0, .z = t},
          {.x = 0, .y = h, .z = t}, {.x = 0, .y = h, .z = -t}, -1, 0, 0, 0.05f,
          h);
  AddQuad(b, {.x = w, .y = 0, .z = -t}, {.x = w, .y = 0, .z = t},
          {.x = w, .y = h, .z = t}, {.x = w, .y = h, .z = -t}, 1, 0, 0, 0.05f,
          h);
  AddQuad(b, {.x = 0, .y = h, .z = -t}, {.x = w, .y = h, .z = -t},
          {.x = w, .y = h, .z = t}, {.x = 0, .y = h, .z = t}, 0, 1, 0, 1,
          0.05f);
  return out;
}
}
