#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "engine/core/fileio.h"
#include "engine/core/json.h"
#include "engine/gfx/probes.h"
#include "engine/world/level.h"

// Bakes light probes (gfx/probes.h) for a level from a list of static lamps
// and writes them as a .probes text file a game loads with ParseProbes and
// uploads with SceneRenderer::UploadProbes.
//
//   mythos_bake_probes <level.txt> <lamps.txt> <out.probes>
//       [--tiles tiles.json] [--spacing metres] [--ambient r g b]
//
// lamps.txt has one lamp per line: `x y z r g b radius` (world units, linear
// colour); blank lines and lines starting with ';' are ignored. --tiles is the
// level's tile legend (without it only '#' is a wall). Returns 2 on bad usage
// and 1 on any other failure.
namespace {
bool ReadLamps(const std::string& text, std::vector<mythos::ProbeLamp>& lamps,
               std::string& error) {
  std::istringstream in(text);
  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    ++number;
    const size_t start = line.find_first_not_of(" \t\r");
    if (start == std::string::npos || line[start] == ';') continue;
    std::istringstream ls(line);
    mythos::ProbeLamp lamp;
    if (!(ls >> lamp.x >> lamp.y >> lamp.z >> lamp.r >> lamp.g >> lamp.b >>
          lamp.radius) ||
        lamp.radius <= 0) {
      error = "lamps line " + std::to_string(number) + ": " + line;
      return false;
    }
    lamps.push_back(lamp);
  }
  return true;
}
}

int main(const int argc, char** const argv) {
  const auto usage = [] {
    std::fprintf(stderr,
                 "usage: mythos_bake_probes <level.txt> <lamps.txt> "
                 "<out.probes> [--tiles tiles.json] [--spacing metres] "
                 "[--ambient r g b]\n");
    return 2;
  };
  if (argc < 4) return usage();
  const std::string level_path = argv[1], lamps_path = argv[2],
                    out_path = argv[3];
  std::string tiles_path;
  mythos::ProbeOptions options;
  std::vector<double> heights;  // empty: one grid at options.height
  for (int i = 4; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--tiles" && i + 1 < argc) {
      tiles_path = argv[++i];
    } else if (arg == "--spacing" && i + 1 < argc) {
      options.spacing = std::strtod(argv[++i], nullptr);
    } else if (arg == "--heights" && i + 1 < argc) {
      // Comma separated metres: a grid of probes at each (at most 4).
      std::string list = argv[++i];
      for (size_t at = 0; at < list.size();) {
        const size_t comma = list.find(',', at);
        heights.push_back(std::strtod(
            list.substr(at, comma == std::string::npos ? comma : comma - at)
                .c_str(),
            nullptr));
        at = comma == std::string::npos ? list.size() : comma + 1;
      }
    } else if (arg == "--ambient" && i + 3 < argc) {
      for (float& c : options.ambient) {
        c = static_cast<float>(std::strtod(argv[++i], nullptr));
      }
    } else {
      return usage();
    }
  }
  if (options.spacing <= 0) return usage();

  mythos::Level level;
  std::string error;
  if (!level.LoadFile(level_path, &error)) {
    std::fprintf(stderr, "mythos_bake_probes: %s\n", error.c_str());
    return 1;
  }
  if (!tiles_path.empty()) {
    mythos::json::Value legend;
    auto tiles = std::make_shared<mythos::TileSet>();
    if (!mythos::json::ParseFile(tiles_path, legend, &error) ||
        !mythos::ParseTileSet(legend, *tiles, &error)) {
      std::fprintf(stderr, "mythos_bake_probes: %s\n", error.c_str());
      return 1;
    }
    level.SetTiles(tiles);
  }
  std::vector<mythos::ProbeLamp> lamps;
  const std::string lamp_text = mythos::ReadTextFile(lamps_path);
  if (lamp_text.empty()) {
    std::fprintf(stderr, "mythos_bake_probes: cannot read %s\n",
                 lamps_path.c_str());
    return 1;
  }
  if (!ReadLamps(lamp_text, lamps, error)) {
    std::fprintf(stderr, "mythos_bake_probes: %s\n", error.c_str());
    return 1;
  }
  const mythos::ProbeStack stack =
      heights.empty() ? mythos::ProbeStack{}
                      : mythos::BakeProbeStack(level, lamps, heights, options);
  const mythos::ProbeGrid grid = heights.empty()
                                     ? mythos::BakeProbes(level, lamps, options)
                                     : stack.grids[0];
  if (grid.Empty() ||
      !mythos::WriteFileAtomic(
          out_path, heights.empty() ? mythos::SerializeProbes(grid)
                                    : mythos::SerializeProbeStack(stack))) {
    std::fprintf(stderr, "mythos_bake_probes: cannot write %s\n",
                 out_path.c_str());
    return 1;
  }
  std::printf("baked %zu lamp(s) into %d x %d probes at %zu height(s): %s\n",
              lamps.size(), grid.width, grid.depth,
              heights.empty() ? size_t{1} : stack.grids.size(),
              out_path.c_str());
  return 0;
}
