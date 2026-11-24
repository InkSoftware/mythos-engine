#include "engine/gfx/probes.h"

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <utility>

namespace mythos {
namespace {
// The average of the renderer's `0.35 + 0.65 * |n . l|` over directions.
constexpr double kAverageAngle = 0.7;

// The probe's place in a row-major array of `width` columns.
size_t Cell(const int width, const int i, const int j) {
  return static_cast<size_t>(j) * static_cast<size_t>(width) +
         static_cast<size_t>(i);
}

size_t Index(const ProbeGrid& g, const int i, const int j) {
  return (static_cast<size_t>(j) * static_cast<size_t>(g.width) +
          static_cast<size_t>(i)) *
         3;
}
}

std::array<float, 3> ProbeGrid::At(const int i, const int j) const {
  if (Empty()) return {0, 0, 0};
  const int x = std::clamp(i, 0, width - 1), z = std::clamp(j, 0, depth - 1);
  const size_t k = Index(*this, x, z);
  return {rgb[k], rgb[k + 1], rgb[k + 2]};
}

std::array<float, 3> ProbeGrid::Sample(const double x, const double z) const {
  if (Empty()) return {0, 0, 0};
  // Probe (i, j) is at (i + 0.5) * spacing: shift so the integer part is the
  // probe on the low side.
  const double fx = x / spacing - 0.5, fz = z / spacing - 0.5;
  const double ix = std::floor(fx), iz = std::floor(fz);
  const auto tx = static_cast<float>(fx - ix), tz = static_cast<float>(fz - iz);
  const int i = static_cast<int>(ix), j = static_cast<int>(iz);
  const auto a = At(i, j), b = At(i + 1, j), c = At(i, j + 1),
             d = At(i + 1, j + 1);
  std::array<float, 3> out{};
  for (size_t k = 0; k < 3; ++k) {
    const float top = a[k] + (b[k] - a[k]) * tx;
    const float bottom = c[k] + (d[k] - c[k]) * tx;
    out[k] = top + (bottom - top) * tz;
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
  grid.rgb.assign(
      static_cast<size_t>(grid.width) * static_cast<size_t>(grid.depth) * 3,
      0.0f);
  std::vector<bool> open(Cell(grid.width, 0, grid.depth), false);

  for (int j = 0; j < grid.depth; ++j) {
    for (int i = 0; i < grid.width; ++i) {
      const double px = (i + 0.5) * grid.spacing, pz = (j + 0.5) * grid.spacing;
      const bool free = !level.PointBlocked(px, pz);
      open[Cell(grid.width, i, j)] = free;
      if (!free) continue;
      std::array<double, 3> sum = {options.ambient[0], options.ambient[1],
                                   options.ambient[2]};
      for (const ProbeLamp& lamp : lamps) {
        const double d = std::hypot(lamp.x - px, lamp.z - pz);
        if (d >= lamp.radius) continue;
        // The renderer's falloff is in 3D; the probe stands at the lamp's
        // height for the line over the floor plan, so use the plan distance.
        if (!level.LineOfSight(lamp.x, lamp.z, px, pz)) continue;
        double att = 1.0 - d / lamp.radius;
        att *= att * kAverageAngle;
        sum[0] += lamp.r * att;
        sum[1] += lamp.g * att;
        sum[2] += lamp.b * att;
      }
      const size_t k = Index(grid, i, j);
      for (size_t c = 0; c < 3; ++c)
        grid.rgb[k + c] = static_cast<float>(sum[c]);
    }
  }

  // Probes in walls take the average of lit neighbours (two passes spread it
  // across a thick wall), so sampling near a wall does not blend in black.
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<bool> filled = open;
    for (int j = 0; j < grid.depth; ++j) {
      for (int i = 0; i < grid.width; ++i) {
        if (open[Cell(grid.width, i, j)]) continue;
        std::array<float, 3> sum{};
        int n = 0;
        for (int dj = -1; dj <= 1; ++dj) {
          for (int di = -1; di <= 1; ++di) {
            const int x = i + di, z = j + dj;
            if (x < 0 || z < 0 || x >= grid.width || z >= grid.depth) continue;
            if (!open[Cell(grid.width, x, z)]) continue;
            const auto v = grid.At(x, z);
            for (size_t c = 0; c < 3; ++c) sum[c] += v[c];
            ++n;
          }
        }
        if (n == 0) continue;
        const size_t k = Index(grid, i, j);
        for (size_t c = 0; c < 3; ++c) {
          grid.rgb[k + c] = sum[c] / static_cast<float>(n);
        }
        filled[Cell(grid.width, i, j)] = true;
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
  out << "probes " << grid.spacing << " " << grid.width << " " << grid.depth
      << "\n";
  for (int j = 0; j < grid.depth; ++j) {
    for (int i = 0; i < grid.width; ++i) {
      const auto v = grid.At(i, j);
      out << (i > 0 ? " " : "") << v[0] << " " << v[1] << " " << v[2];
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
  if (!(in >> word >> g.spacing >> g.width >> g.depth) || word != "probes" ||
      g.spacing <= 0 || g.width < 1 || g.depth < 1 || g.width > 4096 ||
      g.depth > 4096) {
    return fail("bad header");
  }
  const size_t count =
      static_cast<size_t>(g.width) * static_cast<size_t>(g.depth) * 3;
  g.rgb.resize(count);
  for (size_t k = 0; k < count; ++k) {
    if (!(in >> g.rgb[k]) || !std::isfinite(g.rgb[k])) {
      return fail("too few or bad values");
    }
  }
  *out = std::move(g);
  return true;
}
}
