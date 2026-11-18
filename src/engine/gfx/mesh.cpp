#include "engine/gfx/mesh.h"

#include <cmath>

#include "engine/gfx/model.h"

namespace mythos {
namespace {

struct P3 {
    float x, y, z;
};

// The batch in `out` with this texture and flags, created on first use.
Batch& batchFor(std::vector<Batch>& out, const std::string& texture, bool emissive, bool alphaTest) {
    for (auto& existing : out)
        if (existing.texture == texture && existing.emissive == emissive && existing.alphaTest == alphaTest) return existing;
    out.push_back(Batch{});
    Batch& b = out.back();
    b.texture = texture;
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

constexpr float kDoorOpeningHeight = 2.02f;  // the door-shaped hole
constexpr float kLintelTop = 2.14f;
constexpr float kPostThickness = 0.10f;
constexpr float kPostAboveOpening = 0.06f;  // posts stand a little proud of the opening

// Wooden posts and a header around an opening. The passage runs through the cell along
// `alongX` (true: you walk through in the x direction). `inset` is how far the posts sit
// from the sides of the cell; `topY` is where the header ends (the lintel line for door
// cells, the ceiling for the open doorways between the hallway and the rooms).
void addDoorFrame(std::vector<Batch>& out, float fx, float fz, bool alongX, float inset, float topY) {
    Batch& wood = batchFor(out, "door", false, false);
    const float postNear = inset, postFar = inset + kPostThickness;
    const float postTop = kDoorOpeningHeight + kPostAboveOpening;
    if (alongX) {  // walls to the north and south; posts at the z sides of the opening
        addBox(wood, fx + 0.30f, 0, fz + postNear, fx + 0.70f, postTop, fz + postFar);
        addBox(wood, fx + 0.30f, 0, fz + 1 - postFar, fx + 0.70f, postTop, fz + 1 - postNear);
        addBox(wood, fx + 0.25f, kDoorOpeningHeight, fz + postNear, fx + 0.75f, topY, fz + 1 - postNear);
    } else {
        addBox(wood, fx + postNear, 0, fz + 0.30f, fx + postFar, postTop, fz + 0.70f);
        addBox(wood, fx + 1 - postFar, 0, fz + 0.30f, fx + 1 - postNear, postTop, fz + 0.70f);
        addBox(wood, fx + postNear, kDoorOpeningHeight, fz + 0.25f, fx + 1 - postNear, topY, fz + 0.75f);
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
void appendModel(std::vector<Batch>& world, const Model& model, double centreX, double centreZ, double yawDeg,
                 double lift) {
    const double yawRad = yawDeg * 3.14159265358979323846 / 180.0;
    const float cosYaw = static_cast<float>(std::cos(yawRad));
    const float sinYaw = static_cast<float>(std::sin(yawRad));
    // Local axes in the world: right = (cos, 0, -sin), up = (0, 1, 0), forward = (sin, 0, cos).
    const float originX = static_cast<float>(centreX), originZ = static_cast<float>(centreZ);
    for (const auto& modelBatch : model.batches) {
        Batch& dst = batchFor(world, modelBatch.texture, modelBatch.emissive, modelBatch.alphaTest);
        for (const auto& v : modelBatch.verts) {
            const float wx = originX + v.px * cosYaw + v.pz * sinYaw;
            const float wy = v.py + static_cast<float>(lift);
            const float wz = originZ - v.px * sinYaw + v.pz * cosYaw;
            const float nx = v.nx * cosYaw + v.nz * sinYaw;
            const float ny = v.ny;
            const float nz = -v.nx * sinYaw + v.nz * cosYaw;
            dst.verts.push_back({wx, wy, wz, nx, ny, nz, v.u, v.v});
        }
    }
}

// Turns the cells of one level into batches. One method per kind of cell.
class SceneBuilder {
public:
    SceneBuilder(const Level& level, bool outdoor)
        : level_(level), tiles_(level.tiles()), outdoor_(outdoor) {
        // batchFor hands out references, so avoid reallocating mid-build.
        geometry_.world.reserve(32);
        geometry_.props.reserve(8);
    }

    SceneGeometry build(const ModelLibrary& models) {
        for (int y = 0; y < level_.height(); ++y)
            for (int x = 0; x < level_.width(); ++x) addCell(x, y);
        addFurniture(models);
        return std::move(geometry_);
    }

private:
    static constexpr float H = kWallHeight;  // short for the wall quads below
    static constexpr float kDoorJamb = 0.15f;       // wall left either side of a door's hole
    static constexpr float kDoorJambFar = 0.85f;    // where the far jamb starts (1 - kDoorJamb)
    static constexpr float kRoomFrameInset = 0.02f; // posts of a room entrance hug the cell sides

    void addCell(int x, int y) {
        const char glyph = level_.at(x, y);
        if (tiles_[glyph].empty) return;  // open air: only the furniture placed there is drawn
        if (tiles_.isDoor(glyph)) return addDoorCell(x, y);
        if (tiles_.solid(glyph)) return addWallCell(x, y, glyph);
        addFloorCell(x, y, glyph);
    }

    // Floor and (indoors) ceiling of one cell. The sky is drawn instead of a ceiling outdoors.
    void addFloorAndCeiling(float fx, float fz, const std::string& floorTexture) {
        addQuad(batchFor(geometry_.world, floorTexture, false, false),
                {fx, 0, fz}, {fx + 1, 0, fz}, {fx + 1, 0, fz + 1}, {fx, 0, fz + 1}, 0, 1, 0, 1, 1);
        if (!outdoor_)
            addQuad(batchFor(geometry_.world, "ceiling", false, false),
                    {fx, H, fz}, {fx, H, fz + 1}, {fx + 1, H, fz + 1}, {fx + 1, H, fz}, 0, -1, 0, 1, 1);
    }

    // A doorway assembly: wall body around a door-shaped hole, wooden posts and a lintel. The
    // swinging panel itself is drawn per frame by the renderer.
    void addDoorCell(int x, int y) {
        const float fx = static_cast<float>(x), fz = static_cast<float>(y);
        std::vector<Batch>& world = geometry_.world;
        const bool alongX = doorPassageAlongX(level_, x, y);
        addFloorAndCeiling(fx, fz, "wood");
        // The wall body takes the texture of the wall the door sits in.
        const char side = alongX ? level_.at(x, y - 1) : level_.at(x - 1, y);
        Batch& wall = batchFor(world, tiles_.wallTexture(side), false, false);
        if (alongX) {
            addBox(wall, fx, 0, fz, fx + 1, H, fz + kDoorJamb);
            addBox(wall, fx, 0, fz + kDoorJambFar, fx + 1, H, fz + 1);
            addBox(wall, fx, kLintelTop, fz + kDoorJamb, fx + 1, H, fz + kDoorJambFar);
        } else {
            addBox(wall, fx, 0, fz, fx + kDoorJamb, H, fz + 1);
            addBox(wall, fx + kDoorJambFar, 0, fz, fx + 1, H, fz + 1);
            addBox(wall, fx + kDoorJamb, kLintelTop, fz, fx + kDoorJambFar, H, fz + 1);
        }
        addDoorFrame(world, fx, fz, alongX, kDoorJamb, kLintelTop);
        addDarkCaps(x, y, alongX);
    }

    // A passage end that runs into solid wall is capped with a black face: through an open door
    // you must see darkness, not whatever the background pass paints there (outdoors that would
    // be the night sky showing through the house).
    void addDarkCaps(int x, int y, bool alongX) {
        const float fx = static_cast<float>(x), fz = static_cast<float>(y);
        Batch& dark = batchFor(geometry_.world, "void", false, false);
        if (alongX) {
            if (tiles_.solid(level_.at(x - 1, y)))
                addQuad(dark, {fx, 0, fz}, {fx, 0, fz + 1}, {fx, H, fz + 1}, {fx, H, fz}, -1, 0, 0, 1, H);
            if (tiles_.solid(level_.at(x + 1, y)))
                addQuad(dark, {fx + 1, 0, fz + 1}, {fx + 1, 0, fz}, {fx + 1, H, fz}, {fx + 1, H, fz + 1}, 1, 0, 0, 1, H);
        } else {
            if (tiles_.solid(level_.at(x, y - 1)))
                addQuad(dark, {fx + 1, 0, fz}, {fx, 0, fz}, {fx, H, fz}, {fx + 1, H, fz}, 0, 0, -1, 1, H);
            if (tiles_.solid(level_.at(x, y + 1)))
                addQuad(dark, {fx, 0, fz + 1}, {fx + 1, 0, fz + 1}, {fx + 1, H, fz + 1}, {fx, H, fz + 1}, 0, 0, 1, 1, H);
        }
    }

    // Is the face of the wall at (wallX, wallY) towards the neighbour (nx, ny) visible? Drawn
    // wherever the neighbour is not a wall (open floor or furniture). A face into a door cell
    // whose passage runs towards this wall is skipped: that is the darkness you see through an
    // open door.
    bool wallFaceOpen(int wallX, int wallY, int nx, int ny) const {
        const char neighbour = level_.at(nx, ny);
        if (tiles_.solid(neighbour)) return false;
        if (tiles_.isOpenDoor(neighbour)) {
            const bool passageAlongX = doorPassageAlongX(level_, nx, ny);
            if (passageAlongX && ny == wallY) return false;   // this wall sits at the end of the passage
            if (!passageAlongX && nx == wallX) return false;
        }
        return true;
    }

    void addWallCell(int x, int y, char glyph) {
        const float fx = static_cast<float>(x), fz = static_cast<float>(y);
        // Pictures are one object, not a repeating material: stretch them once over the wall
        // instead of tiling them kWallHeight times.
        const float vMax = tiles_[glyph].wallIsObject ? 1.0f : H;
        Batch& b = batchFor(geometry_.world, tiles_.wallTexture(glyph), false, false);
        if (wallFaceOpen(x, y, x + 1, y))
            addQuad(b, {fx + 1, 0, fz}, {fx + 1, 0, fz + 1}, {fx + 1, H, fz + 1}, {fx + 1, H, fz}, -1, 0, 0, 1, vMax);
        if (wallFaceOpen(x, y, x - 1, y))
            addQuad(b, {fx, 0, fz + 1}, {fx, 0, fz}, {fx, H, fz}, {fx, H, fz + 1}, 1, 0, 0, 1, vMax);
        if (wallFaceOpen(x, y, x, y + 1))
            addQuad(b, {fx + 1, 0, fz + 1}, {fx, 0, fz + 1}, {fx, H, fz + 1}, {fx + 1, H, fz + 1}, 0, 0, -1, 1, vMax);
        if (wallFaceOpen(x, y, x, y - 1))
            addQuad(b, {fx, 0, fz}, {fx + 1, 0, fz}, {fx + 1, H, fz}, {fx, H, fz}, 0, 0, 1, 1, vMax);
    }

    void addFloorCell(int x, int y, char glyph) {
        const float fx = static_cast<float>(x), fz = static_cast<float>(y);
        addFloorAndCeiling(fx, fz, tiles_.floorTexture(glyph));
        if (!outdoor_ && isDoorwayCell(level_, x, y)) addRoomEntranceFrame(x, y);

        const TileInfo& tile = tiles_[glyph];
        if (tile.sprite.empty()) return;
        addCross(batchFor(geometry_.props, tile.sprite, false, true), fx + 0.5f, fz + 0.5f, tile.spriteSize,
                 tile.spriteSize, tile.spriteLift);
    }

    // Room entrances get a wooden door frame (on the room side of a two-cell opening).
    void addRoomEntranceFrame(int x, int y) {
        const bool alongZ = tiles_.solid(level_.at(x - 1, y)) && tiles_.solid(level_.at(x + 1, y));
        const bool prevIsDoorway = alongZ ? isDoorwayCell(level_, x, y - 1) : isDoorwayCell(level_, x - 1, y);
        // A passage that leads up to a real door already has that door's frame.
        const bool nextToDoor = alongZ ? (tiles_.isDoor(level_.at(x, y - 1)) || tiles_.isDoor(level_.at(x, y + 1)))
                                       : (tiles_.isDoor(level_.at(x - 1, y)) || tiles_.isDoor(level_.at(x + 1, y)));
        if (!prevIsDoorway && !nextToDoor)
            addDoorFrame(geometry_.world, static_cast<float>(x), static_cast<float>(y), !alongZ, kRoomFrameInset, H);
    }

    // Furniture and props: models from assets/models, placed on their cells (props may sit
    // off centre and above the floor).
    void addFurniture(const ModelLibrary& models) {
        for (const auto& placement : level_.furniture()) {
            const Model* model = models.find(placement.model);
            if (!model) continue;  // unknown model names are skipped rather than crashing the game
            appendModel(geometry_.world, *model, placement.x + 0.5 + placement.offX,
                        placement.y + 0.5 + placement.offZ, placement.yawDeg, placement.lift);
        }
    }

    const Level& level_;
    const TileSet& tiles_;
    bool outdoor_;
    SceneGeometry geometry_;
};

}  // namespace

SceneGeometry buildSceneGeometry(const Level& level, const ModelLibrary& models, bool outdoor) {
    return SceneBuilder(level, outdoor).build(models);
}

std::vector<Batch> buildDoorPanel() {
    std::vector<Batch> out;
    Batch& b = batchFor(out, "door", false, false);
    const float w = kDoorPanelWidth, h = kDoorPanelHeight;
    const float t = 0.025f;  // half the panel's thickness
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
