#include "engine/gfx/pbr.h"

#include "engine/gfx/normalmap.h"
#include "engine/gfx/textures.h"

namespace mythos {

namespace {
bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() > suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}
bool sameSize(const Surface& a, const Surface& b) { return a.w == b.w && a.h == b.h; }
}  // namespace

bool isPbrCompanion(const std::string& name) {
    return endsWith(name, kNormalSuffix) || endsWith(name, kRoughSuffix) || endsWith(name, kMetalSuffix) ||
           endsWith(name, kAoSuffix);
}

Surface makeSurfaceMap(const Surface& albedo, const Surface* normal, const Surface* rough, float strength) {
    if (albedo.empty()) return Surface();
    const bool haveNormal = normal && !normal->empty();
    const bool haveRough = rough && !rough->empty();
    const auto sized = [&](const Surface& map) { return sameSize(map, albedo) ? map : resample(map, albedo.w, albedo.h); };
    Surface out = haveNormal ? sized(*normal) : makeNormalMap(albedo, strength);
    const Surface roughness = haveRough ? sized(*rough) : Surface();
    for (int y = 0; y < albedo.h; ++y)
        for (int x = 0; x < albedo.w; ++x) {
            const uint32_t p = out.at(x, y);
            const int gloss = haveRough ? 255 - static_cast<int>((roughness.at(x, y) >> 8) & 0xFF) : 0;  // green channel
            out.at(x, y) = rgba(static_cast<int>((p >> 16) & 0xFF), static_cast<int>((p >> 8) & 0xFF),
                                static_cast<int>(p & 0xFF), gloss);
        }
    return out;
}

Surface makeMaterialMap(const Surface& albedo, const Surface* metal, const Surface* ao) {
    const bool haveMetal = metal && !metal->empty(), haveAo = ao && !ao->empty();
    if (albedo.empty() || (!haveMetal && !haveAo)) return Surface();
    const auto sized = [&](const Surface& map) { return sameSize(map, albedo) ? map : resample(map, albedo.w, albedo.h); };
    const Surface metalMap = haveMetal ? sized(*metal) : Surface(), aoMap = haveAo ? sized(*ao) : Surface();
    Surface out(albedo.w, albedo.h);
    for (int y = 0; y < albedo.h; ++y)
        for (int x = 0; x < albedo.w; ++x)
            out.at(x, y) = rgba(haveMetal ? static_cast<int>((metalMap.at(x, y) >> 8) & 0xFF) : 0,
                                haveAo ? static_cast<int>((aoMap.at(x, y) >> 8) & 0xFF) : 255, 255, 255);
    return out;
}

}  // namespace mythos
