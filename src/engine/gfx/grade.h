#ifndef MYTHOS_ENGINE_GFX_GRADE_H_
#define MYTHOS_ENGINE_GFX_GRADE_H_

#include <array>

#include "engine/gfx/surface.h"

// Tone mapping and colour grading: the maths behind `CameraState::grade`, kept
// here (no GL) so it is tested; the shaders repeat it. The default grade is
// exactly the engine's original look (ACES, no change after it).
namespace mythos {
// The curve that brings the lit, linear picture into 0..1.
enum class Tonemap : int {
  kAces = 0,    // Narkowicz's ACES fit: the engine's look
  kFilmic = 1,  // Hable's filmic curve: a softer toe and shoulder
  kNone = 2,    // a plain clamp
};

inline constexpr int kTonemapCount = 3;

struct ColorGrade {
  Tonemap tonemap = Tonemap::kAces;
  // Linear light multiplier before the curve (1 = unchanged).
  float exposure = 1;
  // After the curve, on the display colour: contrast around mid grey, colour
  // saturation (0 = grey), and a multiplier per channel.
  float contrast = 1;
  float saturation = 1;
  float tint[3] = {1, 1, 1};
  // 0 none, 1 the engine's vignette, above 1 darker corners.
  float vignette = 1;
  // How much of the colour lookup table (`SceneRenderer::SetColorLut`) is
  // mixed in, 0..1; ignored while no table is set.
  float lut_amount = 1;

  // True when contrast, saturation and tint leave the colour alone (the
  // composite pass then skips the grade).
  [[nodiscard]] bool PostIsIdentity() const;
};

// Returns `g` with every value forced into a sane range (NaN becomes the
// default), so a bad game value cannot blank the picture.
ColorGrade Sanitize(const ColorGrade& g);

// One channel through the curve, then to display (gamma 2.2) space.
float TonemapChannel(Tonemap t, float linear);

// The world shader's colour for a lit pixel, without the vignette or the grain:
// exposure, the curve and gamma.
std::array<float, 3> ToDisplay(const ColorGrade& g, const float linear[3]);

// What the composite pass does to a display colour: contrast, saturation, tint.
std::array<float, 3> GradeDisplay(const ColorGrade& g, const float display[3]);

// A colour lookup table is a strip of `size` slices, each size x size, laid
// out side by side: the picture is size * size wide and size high; pixel
// (b * size + r, g) holds the output for the input (r, g, b) / (size - 1).
// Row 0 is g = 0. Sizes 2..64 are valid.
inline constexpr int kLutMinSize = 2;
inline constexpr int kLutMaxSize = 64;

// The table that changes nothing.
Surface IdentityLutStrip(int size);

// The size of a valid strip, or 0 when `strip` is not one (wrong shape or a
// size out of range).
int LutStripSize(const Surface& strip);

// Looks `rgb` (0..1) up in a strip with trilinear filtering, the way the
// shader does. Returns the input for an invalid strip.
std::array<float, 3> ApplyLut(const Surface& strip, const float rgb[3]);
}  // namespace mythos

#endif
