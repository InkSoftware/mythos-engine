#include "engine/gfx/pbr.h"

#include "engine/gfx/normalmap.h"
#include "engine/gfx/textures.h"

namespace mythos {

namespace {
bool EndsWith(const std::string& text, const std::string& suffix) {
  return text.size() > suffix.size() && text.ends_with(suffix);
}
bool SameSize(const Surface& a, const Surface& b) {
  return a.w == b.w && a.h == b.h;
}
}  // namespace

bool IsPbrCompanion(const std::string& name) {
  return EndsWith(name, kNormalSuffix) || EndsWith(name, kRoughSuffix) ||
         EndsWith(name, kMetalSuffix) || EndsWith(name, kAoSuffix);
}

Surface MakeSurfaceMap(const Surface& albedo, const Surface* normal,
                       const Surface* rough, float strength) {
  if (albedo.Empty()) return {};
  const bool have_normal = normal && !normal->Empty();
  const bool have_rough = rough && !rough->Empty();
  const auto sized = [&](const Surface& map) {
    return SameSize(map, albedo) ? map : Resample(map, albedo.w, albedo.h);
  };
  Surface out = have_normal ? sized(*normal) : MakeNormalMap(albedo, strength);
  const Surface roughness = have_rough ? sized(*rough) : Surface();
  for (int y = 0; y < albedo.h; ++y) {
    for (int x = 0; x < albedo.w; ++x) {
      const uint32_t p = out.At(x, y);
      const int gloss =
          have_rough ? 255 - static_cast<int>((roughness.At(x, y) >> 8) & 0xFF)
                     : 0;  // green channel
      out.At(x, y) = Rgba(static_cast<int>((p >> 16) & 0xFF),
                          static_cast<int>((p >> 8) & 0xFF),
                          static_cast<int>(p & 0xFF), gloss);
    }
  }
  return out;
}

Surface MakeMaterialMap(const Surface& albedo, const Surface* metal,
                        const Surface* ao) {
  const bool have_metal = metal && !metal->Empty(),
             have_ao = ao && !ao->Empty();
  if (albedo.Empty() || (!have_metal && !have_ao)) return {};
  const auto sized = [&](const Surface& map) {
    return SameSize(map, albedo) ? map : Resample(map, albedo.w, albedo.h);
  };
  const Surface metal_map = have_metal ? sized(*metal) : Surface(),
                ao_map = have_ao ? sized(*ao) : Surface();
  Surface out(albedo.w, albedo.h);
  for (int y = 0; y < albedo.h; ++y) {
    for (int x = 0; x < albedo.w; ++x) {
      out.At(x, y) = Rgba(
          have_metal ? static_cast<int>((metal_map.At(x, y) >> 8) & 0xFF) : 0,
          have_ao ? static_cast<int>((ao_map.At(x, y) >> 8) & 0xFF) : 255, 255,
          255);
    }
  }
  return out;
}

}  // namespace mythos
