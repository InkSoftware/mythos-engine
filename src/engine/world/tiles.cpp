#include "engine/world/tiles.h"

#include "engine/core/json.h"

namespace mythos {

const TileSetPtr& defaultTileSet() {
    static const TileSetPtr tiles = std::make_shared<const TileSet>();
    return tiles;
}

bool parseTileSet(const json::Value& root, TileSet& out, std::string* error) {
    auto fail = [&](const std::string& what) {
        if (error) *error = what;
        return false;
    };
    if (!root.isObject()) return fail("the tile legend must be an object");
    out = TileSet{};
    out['#'].solid = false;  // a loaded legend states every glyph itself, '#' included
    for (const auto& [key, value] : root.members()) {
        if (!key.empty() && key[0] == '_') continue;
        if (key == "defaultWall" || key == "defaultFloor") {
            if (!value.isString()) return fail(key + " must be a string");
            (key == "defaultWall" ? out.defaultWall : out.defaultFloor) = value.asString();
        } else if (key != "tiles") {
            return fail("unknown key \"" + key + "\"");
        }
    }
    const json::Value* tiles = root.find("tiles");
    if (!tiles || !tiles->isObject()) return fail("missing \"tiles\" object");
    for (const auto& [glyph, def] : tiles->members()) {
        if (glyph.size() != 1) return fail("tile key \"" + glyph + "\" must be a single character");
        if (!def.isObject()) return fail("tile '" + glyph + "' must be an object");
        TileInfo& t = out[glyph[0]];
        for (const auto& [field, v] : def.members()) {
            const std::string where = "tile '" + glyph + "': " + field;
            if (!field.empty() && field[0] == '_') continue;
            if (field == "solid" || field == "wallIsObject" || field == "empty") {
                if (!v.isBool()) return fail(where + " must be true or false");
                (field == "solid" ? t.solid : field == "empty" ? t.empty : t.wallIsObject) = v.asBool();
            } else if (field == "wall" || field == "floor" || field == "sprite") {
                if (!v.isString()) return fail(where + " must be a texture name");
                (field == "wall" ? t.wall : field == "floor" ? t.floor : t.sprite) = v.asString();
            } else if (field == "spriteSize" || field == "spriteLift") {
                if (!v.isNumber()) return fail(where + " must be a number");
                (field == "spriteSize" ? t.spriteSize : t.spriteLift) = static_cast<float>(v.asNumber());
            } else if (field == "door") {
                if (!v.isString() || (v.asString() != "closed" && v.asString() != "open"))
                    return fail(where + " must be \"closed\" or \"open\"");
                t.door = v.asString() == "closed" ? DoorKind::Closed : DoorKind::Open;
            } else {
                return fail("tile '" + glyph + "': unknown field \"" + field + "\"");
            }
        }
    }
    return true;
}

}  // namespace mythos
