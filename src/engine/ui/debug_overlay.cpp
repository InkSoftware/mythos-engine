#include "engine/ui/debug_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace mythos {
namespace {
// Frame budgets, in ms, for 60 fps and 30 fps.
constexpr double kGoodMs = 17.0, kBadMs = 34.0;
constexpr int kGraphSamples = 120;

std::string Fixed(const double v, const int digits) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.*f", digits, v);
  return buf;
}

uint32_t FrameColor(const DebugOverlayTheme& t, const double ms) {
  return ms <= kGoodMs ? t.good : (ms <= kBadMs ? t.warn : t.bad);
}

float DrawStats(UiCanvas& c, const DebugOverlayTheme& t,
                const FrameHistory& frames, const RenderStats& r, float y) {
  const Style& s = t.text;
  const double avg = frames.AverageMs();
  const std::vector<std::string> lines = {
      Fixed(frames.Fps(), 0) + " fps  " + Fixed(avg, 1) + " ms  (worst " +
          Fixed(frames.MaxMs(), 1) + ")",
      "draws " + std::to_string(r.draw_calls) + "  tris " +
          std::to_string(r.triangles),
      "shadows " + std::to_string(r.shadow_casters) + " lamps, " +
          std::to_string(r.shadow_faces) + " faces, " +
          std::to_string(r.shadow_draw_calls) + " draws",
      "lights " + std::to_string(r.lights) + "  particles " +
          std::to_string(r.particles) + (r.mirror ? "  mirror" : "")};
  float w = 0;
  for (const std::string& l : lines) w = std::max(w, UiCanvas::Width(s, l));
  const float h = s.LineHeight() * static_cast<float>(lines.size());
  DrawBox(c.fb, static_cast<int>(t.margin), static_cast<int>(y),
          static_cast<int>(t.margin + w + 6), static_cast<int>(y + h + 4),
          t.panel, t.border);
  float ty = y + 2;
  for (size_t i = 0; i < lines.size(); ++i) {
    c.Text(i == 0 ? s.Recolored(FrameColor(t, avg)) : s, t.margin + 3, ty,
           lines[i]);
    ty += s.LineHeight();
  }
  return y + h + 4;
}

float DrawGraph(UiCanvas& c, const DebugOverlayTheme& t,
                const FrameHistory& frames, const float y) {
  const std::vector<double> ms = frames.Milliseconds();
  const int w = kGraphSamples;
  const auto x0 = static_cast<int>(t.margin);
  const int y0 = static_cast<int>(y), y1 = y0 + static_cast<int>(t.graph_h);
  DrawBox(c.fb, x0, y0, x0 + w + 2, y1, t.panel, t.border);
  constexpr double kTop = 50.0;  // the graph's full height, in ms
  const auto height_of = [&](const double v) {
    return static_cast<int>(std::clamp(v / kTop, 0.0, 1.0) *
                            (t.graph_h - 2.0f));
  };
  const size_t first = ms.size() > static_cast<size_t>(w)
                           ? ms.size() - static_cast<size_t>(w)
                           : 0;
  for (size_t i = first; i < ms.size(); ++i) {
    const int x = x0 + 1 + static_cast<int>(i - first);
    const int h = height_of(ms[i]);
    FillRect(c.fb, x, y1 - 1 - h, x + 1, y1 - 1, FrameColor(t, ms[i]));
  }
  // The 60 fps line.
  const int guide = y1 - 1 - height_of(kGoodMs);
  FillRect(c.fb, x0 + 1, guide, x0 + w + 1, guide + 1,
           WithAlpha(t.border, 0.9));
  return y + t.graph_h;
}

void DrawConsole(UiCanvas& c, const DebugOverlayTheme& t,
                 const DebugConsole& console, const double time_sec) {
  const Style& s = t.text;
  const int w = c.fb.w;
  const int h =
      std::max(static_cast<int>(s.LineHeight() * 3),
               static_cast<int>(static_cast<float>(c.fb.h) * t.console_height));
  DrawBox(c.fb, 0, 0, w, h, t.panel, t.border);
  const float lh = s.LineHeight();
  const float input_y = static_cast<float>(h) - lh - 3;
  FillRect(c.fb, 0, static_cast<int>(input_y) - 1, w,
           static_cast<int>(input_y) - 1 + 1, t.border);
  const bool caret = std::fmod(time_sec, 1.0) < 0.5;
  const std::string line = "> " + console.Input() + (caret ? "_" : "");
  c.Text(s.Recolored(t.input_color), t.margin, input_y, line);
  // The printed lines, newest at the bottom, scrolled back by ScrollBack().
  const auto& lines = console.Lines();
  const int rows = std::max(0, static_cast<int>((input_y - 4) / lh));
  int newest = static_cast<int>(lines.size()) - 1 - console.ScrollBack();
  float y = input_y - lh - 1;
  for (int row = 0; row < rows && newest >= 0; ++row, --newest) {
    const std::string& text = lines[static_cast<size_t>(newest)];
    const bool echo = text.starts_with("> ");
    c.Text(s.Recolored(echo ? t.echo_color : s.color), t.margin, y, text);
    y -= lh;
  }
}
}  // namespace

void RegisterDebugCvars(CvarRegistry& reg, DebugOverlayOptions& options) {
  reg.AddBool("debug.stats", &options.stats, "show fps and render counts");
  reg.AddBool("debug.graph", &options.graph, "show the frame-time graph");
  reg.AddBool("debug.fly", &options.fly, "fly the free camera");
}

void DrawDebugOverlay(UiCanvas& c, const DebugOverlayOptions& options,
                      const DebugOverlayTheme& theme,
                      const FrameHistory& frames, const RenderStats& stats,
                      const DebugConsole* console, const double time_sec) {
  if (!theme.text.font || c.fb.Empty()) return;
  const bool show_console = console && console->Visible();
  // The stats sit below the console when it is open.
  float y = theme.margin;
  if (show_console) {
    DrawConsole(c, theme, *console, time_sec);
    y += static_cast<float>(c.fb.h) * theme.console_height + theme.margin;
  }
  if (options.stats) y = DrawStats(c, theme, frames, stats, y) + theme.margin;
  if (options.graph) DrawGraph(c, theme, frames, y);
}
}  // namespace mythos
