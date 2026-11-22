#ifndef MYTHOS_ENGINE_UI_DEBUG_OVERLAY_H_
#define MYTHOS_ENGINE_UI_DEBUG_OVERLAY_H_

#include "engine/core/cvar.h"
#include "engine/gfx/render_stats.h"
#include "engine/ui/debug_console.h"
#include "engine/ui/ui.h"

namespace mythos {
// Which debug panels are on. `fly` is for the game: it swaps the player for a
// `FreeCamera` while it is set.
struct DebugOverlayOptions {
  bool stats = false;
  bool graph = false;
  bool fly = false;
};

// Registers `debug.stats`, `debug.graph` and `debug.fly` for `options`, which
// must outlive `reg`.
void RegisterDebugCvars(CvarRegistry& reg, DebugOverlayOptions& options);

// Colours and layout of the debug overlay, in overlay pixels. `text` needs a
// font; with none, nothing is drawn.
struct DebugOverlayTheme {
  Style text;
  uint32_t panel = 0xB4000000, border = 0xFF5A5448;
  uint32_t good = 0xFF7FD36B, warn = 0xFFE8C45A, bad = 0xFFE0624A;
  uint32_t input_color = 0xFFFFFFFF, echo_color = 0xFFB4B0A0;
  float margin = 4.0f;
  float graph_h = 32.0f;
  // How much of the overlay's height the console takes.
  float console_height = 0.45f;
};

// Draws what is on: the frame stats top left (fps, frame time, draw calls,
// triangles, shadows, lights, particles), the frame-time graph below them, and
// the console (when `console` is visible) across the top. `time_sec` blinks
// the caret.
void DrawDebugOverlay(UiCanvas& c, const DebugOverlayOptions& options,
                      const DebugOverlayTheme& theme,
                      const FrameHistory& frames, const RenderStats& stats,
                      const DebugConsole* console, double time_sec);
}  // namespace mythos

#endif
