#include "engine/world/tiles.h"

#include "engine/core/json.h"

namespace mythos {
const TileSetPtr& DefaultTileSet() {
  static const TileSetPtr kTiles = std::make_shared<const TileSet>();
  return kTiles;
}

bool ParseTileSet(const json::Value& root, TileSet& out,
                  std::string* const error) {
  const auto fail = [&](const std::string& what) {
    if (error) *error = what;
    return false;
  };
  if (!root.is_object()) return fail("the tile legend must be an object");
  // Start from a blank set so the legend alone decides what is solid.
  out = TileSet{};
  out['#'].solid = false;
  for (const auto& [key, value] : root.items()) {
    if (!key.empty() && key[0] == '_') continue;
    if (key == "defaultWall" || key == "defaultFloor") {
      if (!value.is_string()) return fail(key + " must be a string");
      (key == "defaultWall" ? out.default_wall : out.default_floor) =
          value.get<std::string>();
    } else if (key != "tiles") {
      return fail("unknown key \"" + key + "\"");
    }
  }
  const json::Value* tiles = json::Find(root, "tiles");
  if (!tiles || !tiles->is_object()) return fail("missing \"tiles\" object");
  for (const auto& [glyph, def] : tiles->items()) {
    if (glyph.size() != 1) {
      return fail("tile key \"" + glyph + "\" must be a single character");
    }
    if (!def.is_object()) return fail("tile '" + glyph + "' must be an object");
    TileInfo& tile = out[glyph[0]];
    for (const auto& [field, value] : def.items()) {
      std::string where = "tile '";
      where += glyph;
      where += "': ";
      where += field;
      if (!field.empty() && field[0] == '_') continue;
      if (field == "solid" || field == "wallIsObject" || field == "empty" ||
          field == "thin" || field == "window" || field == "openCeiling" ||
          field == "openFloor") {
        if (!value.is_boolean()) return fail(where + " must be true or false");
        (field == "solid"         ? tile.solid
         : field == "empty"       ? tile.empty
         : field == "thin"        ? tile.thin
         : field == "window"      ? tile.window
         : field == "openCeiling" ? tile.open_ceiling
         : field == "openFloor"   ? tile.open_floor
                                  : tile.wall_is_object) = value.get<bool>();
      } else if (field == "wall" || field == "floor" || field == "sprite") {
        if (!value.is_string()) return fail(where + " must be a texture name");
        (field == "wall"    ? tile.wall
         : field == "floor" ? tile.floor
                            : tile.sprite) = value.get<std::string>();
      } else if (field == "spriteSize" || field == "spriteLift") {
        if (!value.is_number()) return fail(where + " must be a number");
        (field == "spriteSize" ? tile.sprite_size : tile.sprite_lift) =
            static_cast<float>(value.get<double>());
      } else if (field == "door") {
        if (!value.is_string() || (value.get<std::string>() != "closed" &&
                                   value.get<std::string>() != "open")) {
          return fail(where + R"( must be "closed" or "open")");
        }
        tile.door = value.get<std::string>() == "closed" ? DoorKind::kClosed
                                                         : DoorKind::kOpen;
      } else {
        std::string msg = "tile '";
        msg += glyph;
        msg += "': unknown field \"";
        msg += field;
        msg += '"';
        return fail(msg);
      }
    }
  }
  // A window is always a thin wall.
  for (auto& tile : out.tiles()) {
    if (tile.window) tile.thin = true;
  }
  return true;
}
}
