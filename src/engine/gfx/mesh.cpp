#include "engine/gfx/mesh.h"

#include <cmath>
#include <numbers>

#include "engine/gfx/model.h"

namespace mythos {
namespace {

struct P3 {
  float x, y, z;
};

// The batch in `out` with this texture and flags, created on first use.
Batch& BatchFor(std::vector<Batch>& out, const std::string& texture,
                bool emissive, bool alpha_test) {
  for (auto& existing : out) {
    if (existing.texture == texture && existing.emissive == emissive &&
        existing.alpha_test == alpha_test) {
      return existing;
    }
  }
  out.push_back(Batch{});
  Batch& b = out.back();
  b.texture = texture;
  b.emissive = emissive;
  b.alpha_test = alpha_test;
  return b;
}

void PushVertex(Batch& b, const P3& p, float nx, float ny, float nz, float u,
                float v) {
  b.verts.push_back({.px = p.x,
                     .py = p.y,
                     .pz = p.z,
                     .nx = nx,
                     .ny = ny,
                     .nz = nz,
                     .u = u,
                     .v = v});
}

// Two triangles for the quad p0..p3 (uv spans 0..uMax by 0..vMax). Culling is
// off, so winding does not matter.
void AddQuad(Batch& b, P3 p0, P3 p1, P3 p2, P3 p3, float nx, float ny, float nz,
             float u_max, float v_max) {
  PushVertex(b, p0, nx, ny, nz, 0, 0);
  PushVertex(b, p1, nx, ny, nz, u_max, 0);
  PushVertex(b, p2, nx, ny, nz, u_max, v_max);
  PushVertex(b, p0, nx, ny, nz, 0, 0);
  PushVertex(b, p2, nx, ny, nz, u_max, v_max);
  PushVertex(b, p3, nx, ny, nz, 0, v_max);
}

// An axis-aligned box, all six faces, uv tiled by face size.
// Faces of AddBox, to leave out where a box is joined to another one.
enum BoxFace : unsigned {
  kFacePosX = 1,
  kFaceNegX = 2,
  kFaceTop = 4,
  kFaceBottom = 8,
  kFacePosZ = 16,
  kFaceNegZ = 32,
};

void AddBox(Batch& b, float x0, float y0, float z0, float x1, float y1,
            float z1, unsigned skip = 0) {
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

constexpr float kDoorOpeningHeight = 2.02f;  // the door-shaped hole
constexpr float kLintelTop = 2.14f;
constexpr float kPostThickness = 0.10f;
constexpr float kPostAboveOpening =
    0.06f;  // posts stand a little proud of the opening

// Wooden posts and a header around an opening. The passage runs through the
// cell along `alongX` (true: you walk through in the x direction). `inset` is
// how far the posts sit from the sides of the cell; `topY` is where the header
// ends (the lintel line for door cells, the ceiling for the open doorways
// between the hallway and the rooms).
void AddDoorFrame(std::vector<Batch>& out, float fx, float fz, bool along_x,
                  float inset, float top_y) {
  Batch& wood = BatchFor(out, "door", false, false);
  const float post_near = inset, post_far = inset + kPostThickness;
  const float post_top = kDoorOpeningHeight + kPostAboveOpening;
  if (along_x) {  // walls to the north and south; posts at the z sides of the
                  // opening
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

// True if the passage through the door cell at (x, y) runs along the x axis
// (solid wall to the north and south).
bool DoorPassageAlongX(const Level& level, int x, int y) {
  const TileSet& t = level.Tiles();
  return t.Solid(level.At(x, y - 1)) && t.Solid(level.At(x, y + 1));
}

// An open passage cell between two walls: a doorway without a door (room
// entrances).
bool IsDoorwayCell(const Level& level, int x, int y) {
  const TileSet& t = level.Tiles();
  const char c = level.At(x, y);
  if (t.Solid(c) || t.IsDoor(c)) return false;
  const auto thick = [&](int cx, int cy) {  // thin walls have no passage depth
    return t.Solid(level.At(cx, cy)) && !t.Thin(level.At(cx, cy));
  };
  const bool walls_x = thick(x - 1, y) && thick(x + 1, y);
  const bool walls_z = thick(x, y - 1) && thick(x, y + 1);
  return walls_x != walls_z;
}

// Two crossed vertical quads, so an item reads from every angle.
void AddCross(Batch& b, float cx, float cz, float w, float h, float y0) {
  const float hw = w * 0.5f;
  AddQuad(b, {.x = cx - hw, .y = y0, .z = cz}, {.x = cx + hw, .y = y0, .z = cz},
          {.x = cx + hw, .y = y0 + h, .z = cz},
          {.x = cx - hw, .y = y0 + h, .z = cz}, 0, 0, 1, 1, 1);
  AddQuad(b, {.x = cx, .y = y0, .z = cz - hw}, {.x = cx, .y = y0, .z = cz + hw},
          {.x = cx, .y = y0 + h, .z = cz + hw},
          {.x = cx, .y = y0 + h, .z = cz - hw}, 1, 0, 0, 1, 1);
}

// Bakes a model into the world: rotate by yaw about the vertical axis, then
// move to (cx, lift, cz). Local +z is the model's front; yaw 0 faces +z, 90
// faces +x, 180 faces -z, 270 faces -x.
void AppendModel(std::vector<Batch>& world, const Model& model, double centre_x,
                 double centre_z, double yaw_deg, double lift) {
  const double yaw_rad = yaw_deg * std::numbers::pi / 180.0;
  const float cos_yaw = static_cast<float>(std::cos(yaw_rad));
  const float sin_yaw = static_cast<float>(std::sin(yaw_rad));
  // Local axes in the world: right = (cos, 0, -sin), up = (0, 1, 0), forward =
  // (sin, 0, cos).
  const float origin_x = static_cast<float>(centre_x),
              origin_z = static_cast<float>(centre_z);
  for (const auto& model_batch : model.batches) {
    Batch& dst = BatchFor(world, model_batch.texture, model_batch.emissive,
                          model_batch.alpha_test);
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

// Turns the cells of one level into batches. One method per kind of cell.
class SceneBuilder {
 public:
  SceneBuilder(const Level& level, bool outdoor)
      : level_(level), tiles_(level.Tiles()), outdoor_(outdoor) {
    // batchFor hands out references, so avoid reallocating mid-build.
    geometry_.world.reserve(32);
    geometry_.props.reserve(8);
  }

  SceneGeometry Build(const ModelLibrary& models) {
    for (int y = 0; y < level_.Height(); ++y) {
      for (int x = 0; x < level_.Width(); ++x) AddCell(x, y);
    }
    AddFurniture(models);
    return std::move(geometry_);
  }

 private:
  static constexpr float kH = kWallHeight;  // short for the wall quads below
  static constexpr float kDoorJamb =
      0.15f;  // wall left either side of a door's hole
  static constexpr float kDoorJambFar =
      0.85f;  // where the far jamb starts (1 - kDoorJamb)
  static constexpr float kRoomFrameInset =
      0.02f;  // posts of a room entrance hug the cell sides

  void AddCell(int x, int y) {
    const char glyph = level_.At(x, y);
    if (tiles_[glyph].empty) {
      return;  // open air: only the furniture placed there is drawn
    }
    if (tiles_.IsDoor(glyph)) return AddDoorCell(x, y);
    if (tiles_.Solid(glyph)) return AddWallCell(x, y, glyph);
    AddFloorCell(x, y, glyph);
  }

  // Floor and (indoors) ceiling of one cell. The sky is drawn instead of a
  // ceiling outdoors.
  void AddFloorAndCeiling(float fx, float fz, const std::string& floor_texture,
                          bool floor = true, bool ceiling = true) {
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

  // A doorway assembly: wall body around a door-shaped hole, wooden posts and a
  // lintel. The swinging panel itself is drawn per frame by the renderer.
  void AddDoorCell(int x, int y) {
    const auto fx = static_cast<float>(x), fz = static_cast<float>(y);
    std::vector<Batch>& world = geometry_.world;
    const bool along_x = DoorPassageAlongX(level_, x, y);
    AddFloorAndCeiling(fx, fz, "wood");
    // The wall body takes the texture of the wall the door sits in.
    const char side = along_x ? level_.At(x, y - 1) : level_.At(x - 1, y);
    Batch& wall = BatchFor(world, tiles_.WallTexture(side), false, false);
    // A door in a thin wall is as thin as the wall; in a thick one it is a
    // full cell deep.
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

  // A passage end that runs into solid wall is capped with a black face:
  // through an open door you must see darkness, not whatever the background
  // pass paints there (outdoors that would be the night sky showing through the
  // house).
  void AddDarkCaps(int x, int y, bool along_x) {
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

  // Is the face of the wall at (wallX, wallY) towards the neighbour (nx, ny)
  // visible? Drawn wherever the neighbour is not a wall (open floor or
  // furniture). A face into a door cell whose passage runs towards this wall is
  // skipped: that is the darkness you see through an open door.
  bool WallFaceOpen(int wall_x, int wall_y, int nx, int ny) const {
    const char neighbour = level_.At(nx, ny);
    if (tiles_.Solid(neighbour)) return false;
    if (tiles_.IsOpenDoor(neighbour)) {
      const bool passage_along_x = DoorPassageAlongX(level_, nx, ny);
      if (passage_along_x && ny == wall_y) {
        return false;  // this wall sits at the end of the passage
      }
      if (!passage_along_x && nx == wall_x) return false;
    }
    return true;
  }

  // A thin wall: a post at the middle of the cell and an arm towards every
  // solid or door neighbour (so walls join up), with the floor and ceiling left
  // showing either side of it, in the floor of the room on that side. A window
  // is the same wall with a hole in it, a sill, a frame and glass.
  void AddThinWallCell(int x, int y, char glyph) {
    const auto fx = static_cast<float>(x), fz = static_cast<float>(y);
    const float a = 0.5f - kThinWallThickness / 2;
    const float b = 0.5f + kThinWallThickness / 2;
    const unsigned arms = level_.ThinArms(x, y);
    Batch& wall =
        BatchFor(geometry_.world, tiles_.WallTexture(glyph), false, false);
    const bool window = tiles_[glyph].window;
    const bool along_x = (arms & 12u) != 0;  // arms to the west or east
    // The post and the arms are one wall, so the faces where they meet, and
    // where an arm reaches the next wall cell, stay out of the mesh: they would
    // show as hollow boxes wherever the wall is cut open.
    struct Box {
      float x0, z0, x1, z1;
      unsigned skip;
    };
    const auto cap = [&](unsigned face, int nx, int ny) {
      return tiles_.Solid(level_.At(nx, ny)) ? face : 0u;
    };
    std::vector<Box> boxes = {
        {a, a, b, b,
         ((arms & 1u) ? kFaceNegZ : 0u) | ((arms & 2u) ? kFacePosZ : 0u) |
             ((arms & 4u) ? kFaceNegX : 0u) | ((arms & 8u) ? kFacePosX : 0u)}};
    if (arms & 1u) {
      boxes.push_back(
          {a, 0, b, a, kFacePosZ | cap(kFaceNegZ, x, y - 1)});
    }
    if (arms & 2u) {
      boxes.push_back(
          {a, b, b, 1, kFaceNegZ | cap(kFacePosZ, x, y + 1)});
    }
    if (arms & 4u) {
      boxes.push_back(
          {0, a, a, b, kFacePosX | cap(kFaceNegX, x - 1, y)});
    }
    if (arms & 8u) {
      boxes.push_back(
          {b, a, 1, b, kFaceNegX | cap(kFacePosX, x + 1, y)});
    }
    if (!window) {
      for (const Box& bx : boxes)
        AddBox(wall, fx + bx.x0, 0, fz + bx.z0, fx + bx.x1, kH, fz + bx.z1,
               bx.skip);
    } else {
      // Below the sill and above the head the wall is whole; between them only
      // the ends of the cell stand (the jambs), and the opening is glazed.
      constexpr float kSill = 0.90f, kHead = 2.05f, kJamb = 0.10f;
      for (const Box& bx : boxes) {
        AddBox(wall, fx + bx.x0, 0, fz + bx.z0, fx + bx.x1, kSill, fz + bx.z1,
               bx.skip);
        AddBox(wall, fx + bx.x0, kHead, fz + bx.z0, fx + bx.x1, kH, fz + bx.z1,
               bx.skip);
      }
      Batch& wood = BatchFor(geometry_.world, "door", false, false);
      Batch& glass = BatchFor(geometry_.world, "glass", false, false);
      const float lo = kJamb, hi = 1.0f - kJamb;
      if (along_x) {  // the wall runs along x: the opening spans x, faces z
        AddBox(wall, fx, kSill, fz + a, fx + lo, kHead, fz + b);
        AddBox(wall, fx + hi, kSill, fz + a, fx + 1, kHead, fz + b);
        AddQuad(glass, {.x = fx + lo, .y = kSill, .z = fz + 0.5f},
                {.x = fx + hi, .y = kSill, .z = fz + 0.5f},
                {.x = fx + hi, .y = kHead, .z = fz + 0.5f},
                {.x = fx + lo, .y = kHead, .z = fz + 0.5f}, 0, 0, 1, 1, 1);
        AddBox(wood, fx + lo, kSill, fz + a - 0.03f, fx + hi, kSill + 0.04f,
               fz + b + 0.03f);  // the sill, proud of the wall
        AddBox(wood, fx + 0.5f - 0.02f, kSill, fz + 0.5f - 0.03f,
               fx + 0.5f + 0.02f, kHead, fz + 0.5f + 0.03f);  // the mullion
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
    // Floor and ceiling in the four quarters of the cell that look out onto a
    // room.
    struct Quarter {
      float qx, qz;
      int n1x, n1y, n2x, n2y;  // the two neighbours that border it
      int dx, dy;              // and the one diagonally beyond it
    };
    const Quarter quarters[4] = {
        {0, 0, x, y - 1, x - 1, y, x - 1, y - 1},
        {0.5f, 0, x, y - 1, x + 1, y, x + 1, y - 1},
        {0, 0.5f, x, y + 1, x - 1, y, x - 1, y + 1},
        {0.5f, 0.5f, x, y + 1, x + 1, y, x + 1, y + 1}};
    for (const Quarter& q : quarters) {
      const char n1 = level_.At(q.n1x, q.n1y), n2 = level_.At(q.n2x, q.n2y);
      const char nd = level_.At(q.dx, q.dy);
      const auto open = [&](char c) {
        return !tiles_.Solid(c) && !tiles_[c].empty;
      };
      // The room beside the quarter, or (in the pocket of a corner where both
      // neighbours are wall) the room diagonally across it.
      const char open_glyph =
          open(n1) ? n1 : (open(n2) ? n2 : (open(nd) ? nd : '\0'));
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

  void AddWallCell(int x, int y, char glyph) {
    if (tiles_.Thin(glyph)) return AddThinWallCell(x, y, glyph);
    const auto fx = static_cast<float>(x), fz = static_cast<float>(y);
    // Pictures are one object, not a repeating material: stretch them once over
    // the wall instead of tiling them kWallHeight times.
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

  void AddFloorCell(int x, int y, char glyph) {
    const auto fx = static_cast<float>(x), fz = static_cast<float>(y);
    AddFloorAndCeiling(fx, fz, tiles_.FloorTexture(glyph),
                       !tiles_[glyph].open_floor, !tiles_[glyph].open_ceiling);
    if (!outdoor_ && IsDoorwayCell(level_, x, y)) AddRoomEntranceFrame(x, y);

    const TileInfo& tile = tiles_[glyph];
    if (tile.sprite.empty()) return;
    AddCross(BatchFor(geometry_.props, tile.sprite, false, true), fx + 0.5f,
             fz + 0.5f, tile.sprite_size, tile.sprite_size, tile.sprite_lift);
  }

  // Room entrances get a wooden door frame (on the room side of a two-cell
  // opening).
  void AddRoomEntranceFrame(int x, int y) {
    const bool along_z =
        tiles_.Solid(level_.At(x - 1, y)) && tiles_.Solid(level_.At(x + 1, y));
    const bool prev_is_doorway = along_z ? IsDoorwayCell(level_, x, y - 1)
                                         : IsDoorwayCell(level_, x - 1, y);
    // A passage that leads up to a real door already has that door's frame.
    const bool next_to_door = along_z ? (tiles_.IsDoor(level_.At(x, y - 1)) ||
                                         tiles_.IsDoor(level_.At(x, y + 1)))
                                      : (tiles_.IsDoor(level_.At(x - 1, y)) ||
                                         tiles_.IsDoor(level_.At(x + 1, y)));
    if (!prev_is_doorway && !next_to_door) {
      AddDoorFrame(geometry_.world, static_cast<float>(x),
                   static_cast<float>(y), !along_z, kRoomFrameInset, kH);
    }
  }

  // Furniture and props: models from assets/models, placed on their cells
  // (props may sit off centre and above the floor).
  void AddFurniture(const ModelLibrary& models) {
    for (const auto& placement : level_.Furniture()) {
      const Model* model = models.Find(placement.model);
      if (!model) {
        continue;  // unknown model names are skipped rather than crashing the
      }
      // game
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

}  // namespace

SceneGeometry BuildSceneGeometry(const Level& level, const ModelLibrary& models,
                                 bool outdoor) {
  return SceneBuilder(level, outdoor).Build(models);
}

SceneGeometry BuildSceneGeometry(const Level& level, const ModelLibrary& models,
                                 bool outdoor, float y) {
  SceneGeometry g = BuildSceneGeometry(level, models, outdoor);
  if (y == 0.0f) return g;
  for (auto* part : {&g.world, &g.props})
    for (Batch& b : *part)
      for (Vertex& v : b.verts) v.py += y;
  return g;
}

void AppendSceneGeometry(SceneGeometry& into, SceneGeometry&& from) {
  for (Batch& b : from.world) into.world.push_back(std::move(b));
  for (Batch& b : from.props) into.props.push_back(std::move(b));
}

std::vector<Batch> BuildDoorPanel() {
  std::vector<Batch> out;
  Batch& b = BatchFor(out, "door", false, false);
  const float w = kDoorPanelWidth, h = kDoorPanelHeight;
  const float t = 0.025f;  // half the panel's thickness
  // The two large faces carry the whole door image (planks and the brass knob).
  AddQuad(b, {.x = 0, .y = 0, .z = t}, {.x = w, .y = 0, .z = t},
          {.x = w, .y = h, .z = t}, {.x = 0, .y = h, .z = t}, 0, 0, 1, 1, 1);
  AddQuad(b, {.x = 0, .y = 0, .z = -t}, {.x = w, .y = 0, .z = -t},
          {.x = w, .y = h, .z = -t}, {.x = 0, .y = h, .z = -t}, 0, 0, -1, 1, 1);
  // Thin edges: hinge side, knob side, top.
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

}  // namespace mythos
