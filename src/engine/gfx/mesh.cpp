#include "engine/gfx/mesh.h"

#include <cmath>

#include "engine/gfx/model.h"

namespace mythos {
namespace {

struct P3 {
    float x, y, z;
};

Batch& batchFor(std::vector<Batch>& out, const std::string& tex, bool emissive, bool alphaTest) {
    for (auto& b : out)
        if (b.texture == tex && b.emissive == emissive && b.alphaTest == alphaTest) return b;
    out.push_back(Batch{});
    Batch& b = out.back();
    b.texture = tex;
    b.emissive = emissive;
    b.alphaTest = alphaTest;
    return b;
}

void pushVertex(Batch& b, const P3& p, float nx, float ny, float nz, float u, float v) {
    b.verts.push_back({p.x, p.y, p.z, nx, ny, nz, u, v});
}

// Two triangles for the quad p0..p3 (uv spans 0..uMax by 0..vMax). Culling is off, so winding does not matter.
void addQuad(Batch& b, P3 p0, P3 p1, P3 p2, P3 p3, float nx, float ny, float nz, float uMax, float vMax) {
    pushVertex(b, p0, nx, ny, nz, 0, 0);
    pushVertex(b, p1, nx, ny, nz, uMax, 0);
    pushVertex(b, p2, nx, ny, nz, uMax, vMax);
    pushVertex(b, p0, nx, ny, nz, 0, 0);
    pushVertex(b, p2, nx, ny, nz, uMax, vMax);
    pushVertex(b, p3, nx, ny, nz, 0, vMax);
}

// An axis-aligned box, all six faces, uv tiled by face size.
void addBox(Batch& b, float x0, float y0, float z0, float x1, float y1, float z1) {
    addQuad(b, {x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}, 1, 0, 0, z1 - z0, y1 - y0);
    addQuad(b, {x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, -1, 0, 0, z1 - z0, y1 - y0);
    addQuad(b, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, 0, 1, 0, x1 - x0, z1 - z0);
    addQuad(b, {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}, 0, -1, 0, x1 - x0, z1 - z0);
    addQuad(b, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, 0, 0, 1, x1 - x0, y1 - y0);
    addQuad(b, {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, 0, 0, -1, x1 - x0, y1 - y0);
}

constexpr float kDoorOpeningH = 2.02f;  // the door-shaped hole
constexpr float kLintelTop = 2.14f;

// Wooden posts and a header around an opening. The passage runs through the cell along
// `alongX` (true: you walk through in the x direction). `inset` is how far the posts sit
// from the sides of the cell; `topY` is where the header ends (the lintel line for door
// cells, the ceiling for the open doorways between the hallway and the rooms).
void addDoorFrame(std::vector<Batch>& out, float fx, float fz, bool alongX, float inset, float topY) {
    Batch& wood = batchFor(out, "door", false, false);
    const float i0 = inset, i1 = inset + 0.10f;
    const float postTop = kDoorOpeningH + 0.06f;
    if (alongX) {  // walls to the north and south; posts at the z sides of the opening
        addBox(wood, fx + 0.30f, 0, fz + i0, fx + 0.70f, postTop, fz + i1);
        addBox(wood, fx + 0.30f, 0, fz + 1 - i1, fx + 0.70f, postTop, fz + 1 - i0);
        addBox(wood, fx + 0.25f, kDoorOpeningH, fz + i0, fx + 0.75f, topY, fz + 1 - i0);
    } else {
        addBox(wood, fx + i0, 0, fz + 0.30f, fx + i1, postTop, fz + 0.70f);
        addBox(wood, fx + 1 - i1, 0, fz + 0.30f, fx + 1 - i0, postTop, fz + 0.70f);
        addBox(wood, fx + i0, kDoorOpeningH, fz + 0.25f, fx + 1 - i0, topY, fz + 0.75f);
    }
}

// True if the passage through the door cell at (x, y) runs along the x axis
// (solid wall to the north and south).
bool doorPassageAlongX(const Level& level, int x, int y) {
    const TileSet& t = level.tiles();
    return t.solid(level.at(x, y - 1)) && t.solid(level.at(x, y + 1));
}

// An open passage cell between two walls: a doorway without a door (room entrances).
bool isDoorwayCell(const Level& level, int x, int y) {
    const TileSet& t = level.tiles();
    const char c = level.at(x, y);
    if (t.solid(c) || t.isDoor(c)) return false;
    const bool wallsX = t.solid(level.at(x - 1, y)) && t.solid(level.at(x + 1, y));
    const bool wallsZ = t.solid(level.at(x, y - 1)) && t.solid(level.at(x, y + 1));
    return wallsX != wallsZ;
}

// Two crossed vertical quads, so an item reads from every angle.
void addCross(Batch& b, float cx, float cz, float w, float h, float y0) {
    const float hw = w * 0.5f;
    addQuad(b, {cx - hw, y0, cz}, {cx + hw, y0, cz}, {cx + hw, y0 + h, cz}, {cx - hw, y0 + h, cz}, 0, 0, 1, 1, 1);
    addQuad(b, {cx, y0, cz - hw}, {cx, y0, cz + hw}, {cx, y0 + h, cz + hw}, {cx, y0 + h, cz - hw}, 1, 0, 0, 1, 1);
}

// Bakes a model into the world: rotate by yaw about the vertical axis, then move to (cx, lift, cz).
// Local +z is the model's front; yaw 0 faces +z, 90 faces +x, 180 faces -z, 270 faces -x.
void appendModel(std::vector<Batch>& world, const Model& model, double cx, double cz, double yawDeg, double lift) {
    const double a = yawDeg * 3.14159265358979323846 / 180.0;
    const float c = static_cast<float>(std::cos(a));
    const float s = static_cast<float>(std::sin(a));
    // Local axes in the world: right = (c, 0, -s), up = (0, 1, 0), forward = (s, 0, c).
    const float ox = static_cast<float>(cx), oz = static_cast<float>(cz);
    for (const auto& mb : model.batches) {
        Batch& dst = batchFor(world, mb.texture, mb.emissive, mb.alphaTest);
        for (const auto& v : mb.verts) {
            const float wx = ox + v.px * c + v.pz * s;
            const float wy = v.py + static_cast<float>(lift);
            const float wz = oz - v.px * s + v.pz * c;
            const float nx = v.nx * c + v.nz * s;
            const float ny = v.ny;
            const float nz = -v.nx * s + v.nz * c;
            dst.verts.push_back({wx, wy, wz, nx, ny, nz, v.u, v.v});
        }
    }
}

}  // namespace

SceneGeometry buildSceneGeometry(const Level& level, const ModelLibrary& models, bool outdoor) {
    SceneGeometry g;
    g.world.reserve(32);  // batchFor hands out references, so avoid reallocating mid-build
    g.props.reserve(8);
    const float H = kWallHeight;
    const TileSet& tiles = level.tiles();
    for (int y = 0; y < level.height(); ++y) {
        for (int x = 0; x < level.width(); ++x) {
            const char c = level.at(x, y);
            const float fx = static_cast<float>(x), fz = static_cast<float>(y);
            if (tiles[c].empty) continue;  // open air: only the furniture placed there is drawn

            if (tiles.isDoor(c)) {
                // A doorway assembly: wall body around a door-shaped hole, wooden posts and a
                // lintel. The swinging panel itself is drawn per frame by the renderer.
                const bool alongX = doorPassageAlongX(level, x, y);
                addQuad(batchFor(g.world, "wood", false, false),
                        {fx, 0, fz}, {fx + 1, 0, fz}, {fx + 1, 0, fz + 1}, {fx, 0, fz + 1}, 0, 1, 0, 1, 1);
                if (!outdoor)
                    addQuad(batchFor(g.world, "ceiling", false, false),
                            {fx, H, fz}, {fx, H, fz + 1}, {fx + 1, H, fz + 1}, {fx + 1, H, fz}, 0, -1, 0, 1, 1);
                // The wall body takes the texture of the wall the door sits in.
                const char side = alongX ? level.at(x, y - 1) : level.at(x - 1, y);
                Batch& wall = batchFor(g.world, tiles.wallTexture(side), false, false);
                if (alongX) {
                    addBox(wall, fx, 0, fz, fx + 1, H, fz + 0.15f);
                    addBox(wall, fx, 0, fz + 0.85f, fx + 1, H, fz + 1);
                    addBox(wall, fx, kLintelTop, fz + 0.15f, fx + 1, H, fz + 0.85f);
                } else {
                    addBox(wall, fx, 0, fz, fx + 0.15f, H, fz + 1);
                    addBox(wall, fx + 0.85f, 0, fz, fx + 1, H, fz + 1);
                    addBox(wall, fx + 0.15f, kLintelTop, fz, fx + 0.85f, H, fz + 1);
                }
                addDoorFrame(g.world, fx, fz, alongX, 0.15f, kLintelTop);
                // A passage end that runs into solid wall is capped with a black face: through
                // an open door you must see darkness, not whatever the background pass paints
                // there (outdoors that would be the night sky showing through the house).
                Batch& dark = batchFor(g.world, "void", false, false);
                if (alongX) {
                    if (tiles.solid(level.at(x - 1, y)))
                        addQuad(dark, {fx, 0, fz}, {fx, 0, fz + 1}, {fx, H, fz + 1}, {fx, H, fz}, -1, 0, 0, 1, H);
                    if (tiles.solid(level.at(x + 1, y)))
                        addQuad(dark, {fx + 1, 0, fz + 1}, {fx + 1, 0, fz}, {fx + 1, H, fz}, {fx + 1, H, fz + 1}, 1, 0, 0, 1, H);
                } else {
                    if (tiles.solid(level.at(x, y - 1)))
                        addQuad(dark, {fx + 1, 0, fz}, {fx, 0, fz}, {fx, H, fz}, {fx + 1, H, fz}, 0, 0, -1, 1, H);
                    if (tiles.solid(level.at(x, y + 1)))
                        addQuad(dark, {fx, 0, fz + 1}, {fx + 1, 0, fz + 1}, {fx + 1, H, fz + 1}, {fx, H, fz + 1}, 0, 0, 1, 1, H);
                }
                continue;
            }

            if (tiles.solid(c)) {
                // A wall face is drawn wherever the neighbour is not a wall (open floor or
                // furniture). A face into a door cell whose passage runs towards this wall is
                // skipped: that is the darkness you see through an open door.
                const std::string& tex = tiles.wallTexture(c);
                // Pictures are one object, not a repeating material: stretch them
                // once over the wall instead of tiling them kWallHeight times.
                const float vMax = tiles[c].wallIsObject ? 1.0f : H;
                Batch& b = batchFor(g.world, tex, false, false);
                auto faceOpen = [&](int nx, int ny) {
                    const char n = level.at(nx, ny);
                    if (tiles.solid(n)) return false;
                    if (tiles.isOpenDoor(n)) {
                        const bool px = doorPassageAlongX(level, nx, ny);
                        if (px && ny == y) return false;   // this wall sits at the end of the passage
                        if (!px && nx == x) return false;
                    }
                    return true;
                };
                if (faceOpen(x + 1, y))
                    addQuad(b, {fx + 1, 0, fz}, {fx + 1, 0, fz + 1}, {fx + 1, H, fz + 1}, {fx + 1, H, fz}, -1, 0, 0, 1, vMax);
                if (faceOpen(x - 1, y))
                    addQuad(b, {fx, 0, fz + 1}, {fx, 0, fz}, {fx, H, fz}, {fx, H, fz + 1}, 1, 0, 0, 1, vMax);
                if (faceOpen(x, y + 1))
                    addQuad(b, {fx + 1, 0, fz + 1}, {fx, 0, fz + 1}, {fx, H, fz + 1}, {fx + 1, H, fz + 1}, 0, 0, -1, 1, vMax);
                if (faceOpen(x, y - 1))
                    addQuad(b, {fx, 0, fz}, {fx + 1, 0, fz}, {fx + 1, H, fz}, {fx, H, fz}, 0, 0, 1, 1, vMax);
                continue;
            }

            addQuad(batchFor(g.world, tiles.floorTexture(c), false, false),
                    {fx, 0, fz}, {fx + 1, 0, fz}, {fx + 1, 0, fz + 1}, {fx, 0, fz + 1}, 0, 1, 0, 1, 1);
            if (!outdoor)  // outdoors the sky is drawn instead of a ceiling
                addQuad(batchFor(g.world, "ceiling", false, false),
                        {fx, H, fz}, {fx, H, fz + 1}, {fx + 1, H, fz + 1}, {fx + 1, H, fz}, 0, -1, 0, 1, 1);

            // Room entrances get a wooden door frame (on the room side of a two-cell opening).
            if (!outdoor && isDoorwayCell(level, x, y)) {
                const bool alongZ = tiles.solid(level.at(x - 1, y)) && tiles.solid(level.at(x + 1, y));
                const bool prevIsDoorway = alongZ ? isDoorwayCell(level, x, y - 1) : isDoorwayCell(level, x - 1, y);
                // A passage that leads up to a real door already has that door's frame.
                const bool nextToDoor = alongZ ? (tiles.isDoor(level.at(x, y - 1)) || tiles.isDoor(level.at(x, y + 1)))
                                               : (tiles.isDoor(level.at(x - 1, y)) || tiles.isDoor(level.at(x + 1, y)));
                if (!prevIsDoorway && !nextToDoor) addDoorFrame(g.world, fx, fz, !alongZ, 0.02f, H);
            }

            const TileInfo& tile = tiles[c];
            if (tile.sprite.empty()) continue;
            addCross(batchFor(g.props, tile.sprite, false, true), fx + 0.5f, fz + 0.5f, tile.spriteSize,
                     tile.spriteSize, tile.spriteLift);
        }
    }

    // Furniture and props: models from assets/models, placed on their cells (props may sit
    // off centre and above the floor).
    for (const auto& p : level.furniture()) {
        const Model* m = models.find(p.model);
        if (!m) continue;  // unknown model names are skipped rather than crashing the game
        appendModel(g.world, *m, p.x + 0.5 + p.offX, p.y + 0.5 + p.offZ, p.yawDeg, p.lift);
    }
    return g;
}

std::vector<Batch> buildDoorPanel() {
    std::vector<Batch> out;
    Batch& b = batchFor(out, "door", false, false);
    const float w = kDoorPanelWidth, h = kDoorPanelHeight, t = 0.025f;
    // The two large faces carry the whole door image (planks and the brass knob).
    addQuad(b, {0, 0, t}, {w, 0, t}, {w, h, t}, {0, h, t}, 0, 0, 1, 1, 1);
    addQuad(b, {0, 0, -t}, {w, 0, -t}, {w, h, -t}, {0, h, -t}, 0, 0, -1, 1, 1);
    // Thin edges: hinge side, knob side, top.
    addQuad(b, {0, 0, -t}, {0, 0, t}, {0, h, t}, {0, h, -t}, -1, 0, 0, 0.05f, h);
    addQuad(b, {w, 0, -t}, {w, 0, t}, {w, h, t}, {w, h, -t}, 1, 0, 0, 0.05f, h);
    addQuad(b, {0, h, -t}, {w, h, -t}, {w, h, t}, {0, h, t}, 0, 1, 0, 1, 0.05f);
    return out;
}

}  // namespace mythos
