// Baked lighting by light probes: a grid of colours over the floor plan,
// computed once on the CPU from static lamps (with the walls between them and
// each probe taken into account), then sampled by the renderer as one more
// light term. Each probe is an "ambient cube": a colour for each of the six
// axis directions, so a surface facing a lamp is lit more than one turned away
// (dynamic props and a model with its back to the light too). A level with
// many lamps bakes the ones that never move and sends the renderer only the
// dynamic ones, lifting the 32-lamp limit for the static part and its cost.
// Baked light casts no shadows and does not flicker. Headless; the renderer
// uploads the grid as a texture array (`SceneRenderer::UploadProbes`).
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
  // The height the probes stand at, for the direction to each lamp.
  double height = 1.2;
  // Light every probe gets from every direction whatever the lamps do (a
  // baked ambient).
  std::array<float, 3> ambient{0, 0, 0};
};

// The six faces of a probe's ambient cube, in this order: +x (east), -x, +y
// (up), -y, +z (south), -z.
inline constexpr int kProbeFaces = 6;

// Six colours per probe over the floor plan: probe (i, j) sits at x = (i + 0.5)
// * spacing, z = (j + 0.5) * spacing from the map's corner.
struct ProbeGrid {
  double spacing = 0.5;
  int width = 0, depth = 0;
  // width * depth * kProbeFaces * 3 floats: probe by probe, face by face.
  std::vector<float> rgb;

  [[nodiscard]] bool Empty() const { return width <= 0 || depth <= 0; }
  // One face of one probe (clamped to the grid).
  [[nodiscard]] std::array<float, 3> Face(int i, int j, int face) const;
  // The mean of a probe's faces: its light without a direction.
  [[nodiscard]] std::array<float, 3> At(int i, int j) const;
  // Bilinear between the four probes around (x, z), clamped at the edges: the
  // mean of the faces, or `SampleFace` for one face.
  [[nodiscard]] std::array<float, 3> Sample(double x, double z) const;
  [[nodiscard]] std::array<float, 3> SampleFace(double x, double z,
                                                int face) const;
  // The light on a surface with unit normal (nx, ny, nz): the faces weighted
  // by the squares of the normal's components (the renderer's formula).
  [[nodiscard]] std::array<float, 3> SampleNormal(double x, double z, double nx,
                                                  double ny, double nz) const;

  bool operator==(const ProbeGrid&) const = default;
};

// Bakes the grid for `level`. A lamp lights a probe when nothing blocks the
// line between them; each face gets the lamp's colour times the renderer's own
// falloff, `(1 - distance / radius)^2`, and `0.35 + 0.65 * |component of the
// direction to the lamp along the face's axis|` (the renderer's angle term, on
// the axis). Probes inside walls take the average of their lit neighbours, so
// surfaces beside a wall do not darken when sampled.
ProbeGrid BakeProbes(const Level& level, const std::vector<ProbeLamp>& lamps,
                     const ProbeOptions& options = {});

// A text file: `probes6 <spacing> <width> <depth>` then one line per row of
// probes, each probe six `r g b` triples. `ParseProbes` also reads the old
// one-colour form (`probes`, one triple a probe, used for every face) and
// returns false (leaving `out`) on a bad file.
std::string SerializeProbes(const ProbeGrid& grid);
bool ParseProbes(const std::string& text, ProbeGrid* out, std::string* error);
}

#endif
