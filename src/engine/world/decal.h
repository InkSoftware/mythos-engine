#ifndef MYTHOS_ENGINE_WORLD_DECAL_H_
#define MYTHOS_ENGINE_WORLD_DECAL_H_

#include <string>

namespace mythos {
enum class DecalFace {
  kNorth, kSouth, kWest, kEast, kFloor, kCeiling,
  kThinNorth, kThinSouth, kThinWest, kThinEast
};

struct Decal {
  int x = 0, y = 0;
  DecalFace face = DecalFace::kNorth;
  double offset_u = 0, offset_v = 0;
  double width = 1, height = 1, rotation = 0;
  std::string texture;

  bool operator==(const Decal&) const = default;
};

// Lines use: x y face offset_u offset_v width height rotation texture.
// Face names are lowercase; thin-wall faces use the thin_ prefix. Texture is
// a single nonempty token.
bool ValidDecal(const Decal& decal);
bool ParseDecalLine(const std::string& line, Decal* out);
std::string SerializeDecalLine(const Decal& decal);
}

#endif