// Baked lighting by light probes: a grid of colours over the floor plan,
// computed once on the CPU from static lamps (with the walls between them and
// each probe taken into account), then sampled by the renderer as one more
// light term. A level with many lamps bakes the ones that never move and
// sends the renderer only the dynamic ones, lifting the 32-lamp limit for the
// static part and its cost. Baked light casts no shadows and does not flicker.
// Headless; the renderer uploads the grid as a texture
// (`SceneRenderer::UploadProbes`).
#ifndef MYTHOS_ENGINE_GFX_PROBES_H_
#define MYTHOS_ENGINE_GFX_PROBES_H_

#include <array>
#include <string>
#include <vector>

#include "engine/world/level.h"

namespace mythos {
// A static lamp to bake: world position, linear colour and the radius at which
// its light fades out (as `PointLight`, so the same numbers can be used).
struct ProbeLamp {
  double x = 0, y = 0, z = 0;
  double r = 1, g = 1, b = 1;
  double radius = 4;
};

struct ProbeOptions {
  // Metres between probes.
  double spacing = 0.5;
  // Light every probe gets whatever the lamps do (a baked ambient).
  std::array<float, 3> ambient{0, 0, 0};
};

// RGB per probe over the floor plan: probe (i, j) sits at x = (i + 0.5) *
// spacing, z = (j + 0.5) * spacing from the map's corner.
struct ProbeGrid {
  double spacing = 0.5;
  int width = 0, depth = 0;
  std::vector<float> rgb;  // width * depth * 3

  [[nodiscard]] bool Empty() const { return width <= 0 || depth <= 0; }
  [[nodiscard]] std::array<float, 3> At(int i, int j) const;
  // Bilinear between the four probes around (x, z), clamped at the edges.
  [[nodiscard]] std::array<float, 3> Sample(double x, double z) const;

  bool operator==(const ProbeGrid&) const = default;
};

// Bakes the grid for `level`. A lamp lights a probe when nothing blocks the
// line between them; the light is the lamp's colour times the renderer's own
// falloff, `(1 - distance / radius)^2`, and 0.7 for the average angle of a
// surface. Probes inside walls take the average of their lit neighbours, so
// surfaces beside a wall do not darken when sampled.
ProbeGrid BakeProbes(const Level& level, const std::vector<ProbeLamp>& lamps,
                     const ProbeOptions& options = {});

// A text file: `probes <spacing> <width> <depth>` then one row of
// `r g b` triples per line. Parse returns false (leaving `out`) on a bad file.
std::string SerializeProbes(const ProbeGrid& grid);
bool ParseProbes(const std::string& text, ProbeGrid* out, std::string* error);
}

#endif
