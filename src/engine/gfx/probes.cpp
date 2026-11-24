#include "engine/gfx/probes.h"

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <utility>

namespace mythos {
namespace {
// The probe's place in a row-major array of `width` columns.
size_t Cell(const int width, const int i, const int j) {
  return static_cast<size_t>(j) * static_cast<size_t>(width) +
         static_cast<size_t>(i);
}

size_t Index(const ProbeGrid& g, const int i, const int j, const int face) {
  return (Cell(g.width, i, j) * kProbeFaces + static_cast<size_t>(face)) * 3;
}

// A bilinear blend of four probes' values for one colour channel.
float Bilinear(const float a, const float b, const float c, const float d,
               const float tx, const float tz) {
  const float top = a + (b - a) * tx;
  const float bottom = c + (d - c) * tx;
  return top + (bottom - top) * tz;
}
}

std::array<float, 3> ProbeGrid::Face(const int i, const int j,
                                     const int face) const {
  if (Empty()) return {0, 0, 0};
  const int x = std::clamp(i, 0, width - 1), z = std::clamp(j, 0, depth - 1);
  const size_t k = Index(*this, x, z, std::clamp(face, 0, kProbeFaces - 1));
  return {rgb[k], rgb[k + 1], rgb[k + 2]};
}

std::array<float, 3> ProbeGrid::At(const int i, const int j) const {
  std::array<float, 3> sum{};
  for (int f = 0; f < kProbeFaces; ++f) {
    const auto v = Face(i, j, f);
    for (size_t c = 0; c < 3; ++c) sum[c] += v[c];
  }
  for (float& c : sum) c /= static_cast<float>(kProbeFaces);
  return sum;
}

std::array<float, 3> ProbeGrid::SampleFace(const double x, const double z,
                                           const int face) const {
  if (Empty()) return {0, 0, 0};
  // Probe (i, j) is at (i + 0.5) * spacing: shift so the integer part is the
  // probe on the low side.
  const double fx = x / spacing - 0.5, fz = z / spacing - 0.5;
  const double ix = std::floor(fx), iz = std::floor(fz);
  const auto tx = static_cast<float>(fx - ix), tz = static_cast<float>(fz - iz);
  const int i = static_cast<int>(ix), j = static_cast<int>(iz);
  const auto a = Face(i, j, face), b = Face(i + 1, j, face),
             c = Face(i, j + 1, face), d = Face(i + 1, j + 1, face);
  std::array<float, 3> out{};
  for (size_t k = 0; k < 3; ++k)
    out[k] = Bilinear(a[k], b[k], c[k], d[k], tx, tz);
  return out;
}

std::array<float, 3> ProbeGrid::Sample(const double x, const double z) const {
  std::array<float, 3> sum{};
  for (int f = 0; f < kProbeFaces; ++f) {
    const auto v = SampleFace(x, z, f);
    for (size_t c = 0; c < 3; ++c) sum[c] += v[c];
  }
  for (float& c : sum) c /= static_cast<float>(kProbeFaces);
  return sum;
}

std::array<float, 3> ProbeGrid::SampleNormal(const double x, const double z,
                                             const double nx, const double ny,
                                             const double nz) const {
  const double w[3] = {nx * nx, ny * ny, nz * nz};
  const int faces[3] = {nx < 0 ? 1 : 0, ny < 0 ? 3 : 2, nz < 0 ? 5 : 4};
  std::array<float, 3> out{};
  for (int a = 0; a < 3; ++a) {
    const auto v = SampleFace(x, z, faces[a]);
    for (size_t c = 0; c < 3; ++c) out[c] += static_cast<float>(w[a]) * v[c];
  }
  return out;
}

ProbeGrid BakeProbes(const Level& level, const std::vector<ProbeLamp>& lamps,
                     const ProbeOptions& options) {
  ProbeGrid grid;
  grid.spacing = std::max(0.1, options.spacing);
  if (level.Width() <= 0 || level.Height() <= 0) return grid;
  grid.width = static_cast<int>(std::ceil(level.Width() / grid.spacing));
  grid.depth = static_cast<int>(std::ceil(level.Height() / grid.spacing));
  grid.rgb.assign(Cell(grid.width, 0, grid.depth) * kProbeFaces * 3, 0.0f);
  std::vector<bool> open(Cell(grid.width, 0, grid.depth), false);

  for (int j = 0; j < grid.depth; ++j) {
    for (int i = 0; i < grid.width; ++i) {
      const double px = (i + 0.5) * grid.spacing, pz = (j + 0.5) * grid.spacing;
      const bool free = !level.PointBlocked(px, pz);
      open[Cell(grid.width, i, j)] = free;
      if (!free) continue;
      // Every face starts with the baked ambient.
      std::array<std::array<double, 3>, kProbeFaces> faces;
      for (auto& f : faces) {
        f = {options.ambient[0], options.ambient[1], options.ambient[2]};
      }
      for (const ProbeLamp& lamp : lamps) {
        // A line over the floor plan decides whether it is seen; the falloff
        // is by the distance in 3D, as the renderer does.
        const double dx = lamp.x - px, dy = lamp.y - options.height,
                     dz = lamp.z - pz;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist >= lamp.radius || dist < 1e-6) continue;
        if (!level.LineOfSight(lamp.x, lamp.z, px, pz)) continue;
        double att = 1.0 - dist / lamp.radius;
        att *= att;
        // The direction to the lamp along each axis: +x -x +y -y +z -z.
        const double along[kProbeFaces] = {dx / dist,  -dx / dist, dy / dist,
                                           -dy / dist, dz / dist,  -dz / dist};
        for (int f = 0; f < kProbeFaces; ++f) {
          // A surface looking along +x is lit from the lamp by 0.35 + 0.65 *
          // |l.x| in the renderer (the angle term uses the absolute value).
          const double angle = 0.35 + 0.65 * std::fabs(along[f]);
          faces[static_cast<size_t>(f)][0] += lamp.r * att * angle;
          faces[static_cast<size_t>(f)][1] += lamp.g * att * angle;
          faces[static_cast<size_t>(f)][2] += lamp.b * att * angle;
        }
      }
      for (int f = 0; f < kProbeFaces; ++f) {
        const size_t k = Index(grid, i, j, f);
        for (size_t c = 0; c < 3; ++c) {
          grid.rgb[k + c] =
              static_cast<float>(faces[static_cast<size_t>(f)][c]);
        }
      }
    }
  }

  // Probes in walls take the average of lit neighbours (two passes spread it
  // across a thick wall), so sampling near a wall does not blend in black.
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<bool> filled = open;
    for (int j = 0; j < grid.depth; ++j) {
      for (int i = 0; i < grid.width; ++i) {
        if (open[Cell(grid.width, i, j)]) continue;
        for (int f = 0; f < kProbeFaces; ++f) {
          std::array<float, 3> sum{};
          int n = 0;
          for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
              const int x = i + di, z = j + dj;
              if (x < 0 || z < 0 || x >= grid.width || z >= grid.depth)
                continue;
              if (!open[Cell(grid.width, x, z)]) continue;
              const auto v = grid.Face(x, z, f);
              for (size_t c = 0; c < 3; ++c) sum[c] += v[c];
              ++n;
            }
          }
          if (n == 0) continue;
          const size_t k = Index(grid, i, j, f);
          for (size_t c = 0; c < 3; ++c) {
            grid.rgb[k + c] = sum[c] / static_cast<float>(n);
          }
          filled[Cell(grid.width, i, j)] = true;
        }
      }
    }
    open = std::move(filled);
  }
  return grid;
}

std::string SerializeProbes(const ProbeGrid& grid) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out.precision(6);
  out << "probes6 " << grid.spacing << " " << grid.width << " " << grid.depth
      << "\n";
  for (int j = 0; j < grid.depth; ++j) {
    for (int i = 0; i < grid.width; ++i) {
      for (int f = 0; f < kProbeFaces; ++f) {
        const auto v = grid.Face(i, j, f);
        out << (i > 0 || f > 0 ? " " : "") << v[0] << " " << v[1] << " "
            << v[2];
      }
    }
    out << "\n";
  }
  return out.str();
}

bool ParseProbes(const std::string& text, ProbeGrid* const out,
                 std::string* const error) {
  const auto fail = [&](const std::string& why) {
    if (error != nullptr) *error = why;
    return false;
  };
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  std::string word;
  ProbeGrid g;
  if (!(in >> word >> g.spacing >> g.width >> g.depth) ||
      (word != "probes" && word != "probes6") || g.spacing <= 0 ||
      g.width < 1 || g.depth < 1 || g.width > 4096 || g.depth > 4096) {
    return fail("bad header");
  }
  const bool old = word == "probes";
  const size_t cells = Cell(g.width, 0, g.depth);
  g.rgb.resize(cells * kProbeFaces * 3);
  for (size_t p = 0; p < cells; ++p) {
    // The old form has one colour a probe, used for all six faces.
    for (int f = 0; f < (old ? 1 : kProbeFaces); ++f) {
      float v[3];
      if (!(in >> v[0] >> v[1] >> v[2]) || !std::isfinite(v[0]) ||
          !std::isfinite(v[1]) || !std::isfinite(v[2])) {
        return fail("too few or bad values");
      }
      for (int copy = 0; copy < (old ? kProbeFaces : 1); ++copy) {
        const size_t k =
            (p * kProbeFaces + static_cast<size_t>(old ? copy : f)) * 3;
        for (size_t c = 0; c < 3; ++c) g.rgb[k + c] = v[c];
      }
    }
  }
  *out = std::move(g);
  return true;
}
}
