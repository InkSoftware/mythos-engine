

// Unit tests for mythos_engine: a single home-made harness, with no framework.
// A test is `TEST(name) { ... }`; it registers itself at static-init time and
// main() runs them all, or only those whose name contains argv[1].
// CHECK(cond) records a failure and carries on; CHECK_EQ(a, b) compares two
// integral values and prints both on a mismatch.  The binary links only
// mythos_engine, so nothing here can touch SDL or OpenGL.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/audio_math.h"
#include "engine/audio/music.h"
#include "engine/audio/radio_fx.h"
#include "engine/audio/reverb.h"
#include "engine/audio/sound_bank.h"
#include "engine/audio/spatial.h"
#include "engine/core/cvar.h"
#include "engine/core/file_watch.h"
#include "engine/core/fileio.h"
#include "engine/core/json.h"
#include "engine/core/json_reader.h"
#include "engine/core/mat4.h"
#include "engine/core/noise.h"
#include "engine/core/tween.h"
#include "engine/core/vfs.h"
#include "engine/data/achievements.h"
#include "engine/data/credits.h"
#include "engine/data/events.h"
#include "engine/data/i18n.h"
#include "engine/data/save.h"
#include "engine/data/translation.h"
#include "engine/editor/adapter.h"
#include "engine/editor/history.h"
#include "engine/editor/level_doc.h"
#include "engine/editor/prefab.h"
#include "engine/editor/project.h"
#include "engine/gfx/anim_state.h"
#include "engine/gfx/flicker.h"
#include "engine/gfx/font.h"
#include "engine/gfx/gltf.h"
#include "engine/gfx/grade.h"
#include "engine/gfx/lod.h"
#include "engine/gfx/materials.h"
#include "engine/gfx/mesh.h"
#include "engine/gfx/model.h"
#include "engine/gfx/normalmap.h"
#include "engine/gfx/particles.h"
#include "engine/gfx/pbr.h"
#include "engine/gfx/probes.h"
#include "engine/gfx/render_stats.h"
#include "engine/gfx/skeleton.h"
#include "engine/gfx/sky.h"
#include "engine/gfx/sun_shadow.h"
#include "engine/gfx/surface.h"
#include "engine/gfx/text.h"
#include "engine/gfx/textures.h"
#include "engine/ui/controls_ui.h"
#include "engine/ui/debug_console.h"
#include "engine/ui/debug_overlay.h"
#include "engine/ui/input_map.h"
#include "engine/ui/menu.h"
#include "engine/ui/paging.h"
#include "engine/ui/settings.h"
#include "engine/ui/settings_cvars.h"
#include "engine/ui/ui.h"
#include "engine/world/ai.h"
#include "engine/world/body.h"
#include "engine/world/camera_fx.h"
#include "engine/world/cutscene.h"
#include "engine/world/event_graph.h"
#include "engine/world/free_cam.h"
#include "engine/world/gamepad.h"
#include "engine/world/interact.h"
#include "engine/world/level.h"
#include "engine/world/mirror.h"
#include "engine/world/movement.h"
#include "engine/world/nav.h"
#include "engine/world/player.h"
#include "engine/world/room_doors.h"
#include "engine/world/save_state.h"
#include "engine/world/sound_path.h"
#include "engine/world/tiles.h"
#include "engine/world/trigger.h"

namespace mythos {

namespace {

// The registry of tests and the running totals; main() reports them.
struct TestCase {
  const char* name;
  std::function<void()> fn;
};

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> tests;
  return tests;
}

int g_failures = 0;
int g_checks = 0;

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    Registry().push_back({.name = name, .fn = std::move(fn)});
  }
};

#define TEST(name)                                \
  static void name();                             \
  static Registrar name##_registrar(#name, name); \
  static void name()

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      ++g_failures;                                                     \
      std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                   \
  } while (0)

#define CHECK_EQ(a, b)                                                     \
  do {                                                                     \
    ++g_checks;                                                            \
    const auto va_ = (a);                                                  \
    const auto vb_ = (b);                                                  \
    if (!(va_ == vb_)) {                                                   \
      ++g_failures;                                                        \
      std::printf("    FAILED %s:%d: %s == %s (got %lld, want %lld)\n",    \
                  __FILE__, __LINE__, #a, #b, static_cast<long long>(va_), \
                  static_cast<long long>(vb_));                            \
    }                                                                      \
  } while (0)

}

namespace {
// Counts code points (not bytes) of a UTF-8 string.
size_t Utf8Length(const std::string& s) {
  size_t n = 0;
  for (const unsigned char ch : s) n += (ch & 0xC0) != 0x80;
  return n;
}
}

// Text paging: whole words per page, explicit breaks, and UTF-8 never cut
// in the middle of a code point.
TEST(GamepadSticksHaveADeadZoneAndAQuadraticLook) {
  CHECK_EQ(StickDeadzone(0.0), 0.0);
  CHECK_EQ(StickDeadzone(0.15), 0.0);  // drift inside the zone is ignored
  CHECK_EQ(StickDeadzone(-0.2), 0.0);
  CHECK(std::fabs(StickDeadzone(1.0) - 1.0) < 1e-9);  // the rim still reaches 1
  CHECK(std::fabs(StickDeadzone(-1.0) + 1.0) < 1e-9);
  CHECK(StickDeadzone(0.6) > 0.0 && StickDeadzone(0.6) < 0.6);
  CHECK_EQ(StickAxisToUnit(0), 0.0);
  CHECK_EQ(StickAxisToUnit(32767), 1.0);
  CHECK_EQ(StickAxisToUnit(-32768), -1.0);
  CHECK_EQ(StickLookPixels(1.0, 0.5, 800.0), 400.0);
  CHECK_EQ(StickLookPixels(-1.0, 0.5, 800.0), -400.0);
  CHECK(StickLookPixels(0.5, 1.0, 800.0) <
        0.5 * 800.0);  // a gentle push is slow
}

TEST(PaginateTextEdgeCases) {
  CHECK_EQ(PaginateText("", size_t{10}).size(), 1u);
  CHECK(PaginateText("", size_t{10})[0].empty());
  CHECK_EQ(PaginateText("short", size_t{10}).size(), 1u);
  CHECK_EQ(PaginateText("0123456789", size_t{10}).size(), 1u);
  CHECK_EQ(PaginateText("0123456789 a", size_t{10}).size(), 2u);
  const std::string para =
      "One two three. Four five six seven. Eight nine ten eleven twelve.";
  const auto pages = PaginateText(para, size_t{20});
  CHECK(pages.size() >= 3u);
  std::string joined;
  for (const auto& p : pages) {
    CHECK(!p.empty() && p.size() <= 20u);
    CHECK(p.front() != ' ' && p.back() != ' ');
    joined += (joined.empty() ? "" : " ") + p;
  }
  CHECK(joined == para);
  CHECK(pages[0] == "One two three.");
  const auto lines = PaginateText("aaaa\nbbbb\ncccc", size_t{9});
  CHECK_EQ(lines.size(), 2u);
  CHECK(lines[0] == "aaaa\nbbbb" && lines[1] == "cccc");
  const auto forced = PaginateText("one\ftwo[page]three", size_t{100});
  CHECK_EQ(forced.size(), 3u);
  CHECK(forced[1] == "two" && forced[2] == "three");
  const std::string greek = "αβγδεζηθικλμνξοπρστυφχψω";
  const auto cut = PaginateText(greek, size_t{5});
  std::string back;
  for (const auto& p : cut) {
    CHECK(p.size() % 2 == 0);
    CHECK(!p.empty());
    back += p;
  }
  CHECK(back == greek);
  const auto gsent =
      PaginateText("Καλημέρα κόσμε. Τι κάνεις σήμερα;", size_t{16});
  for (const auto& p : gsent) {
    CHECK(Utf8Length(p) <= 16u);
    CHECK(p.find('\xEF') == std::string::npos);
  }
}

// Entry paging by height, and page indices clamped to the valid range.
TEST(PagedEntriesAndClampedPages) {
  const auto starts = PaginateEntries({50, 50, 50, 50}, 110, 3);
  CHECK_EQ(starts.size(), 2u);
  CHECK(starts[0] == 0 && starts[1] == 2);
  CHECK_EQ(PaginateEntries({}, 100, 3).size(), 1u);
  CHECK_EQ(PaginateEntries({500}, 100, 3).size(), 1u);
  CHECK_EQ(PaginateEntries({500, 10}, 100, 3).size(), 2u);
  CHECK_EQ(ClampPage(5, 3), 2);
  CHECK_EQ(ClampPage(-1, 3), 0);
  CHECK_EQ(ClampPage(2, 0), 0);
}

// A word longer than the width stays whole; empty text still yields a line.
TEST(WrapWordsKeepsWordsWholeAndAlwaysGivesALine) {
  CHECK_EQ(WrapWords("", 10).size(), 1u);
  const auto lines = WrapWords("alpha beta gamma delta", 11);
  CHECK_EQ(lines.size(), 2u);
  CHECK(lines[0] == "alpha beta" && lines[1] == "gamma delta");
  CHECK_EQ(WrapWords("extraordinarily", 5).size(), 1u);
  const auto greek = WrapWords("αβγδ εζηθ ικλμ", 9);
  CHECK_EQ(greek.size(), 2u);
}

// The overlay grows to the window's aspect ratio but only at an integer
// scale of the base size, and never below the base size.
TEST(OverlayTakesTheWindowShapeAtAWholeScale) {
  int w = 0, h = 0;
  OverlaySize(960, 600, 320, 200, w, h);
  CHECK(w == 320 && h == 200);
  OverlaySize(1920, 1080, 320, 200, w, h);
  CHECK(w == 384 && h == 216);
  OverlaySize(1280, 720, 320, 200, w, h);
  CHECK(w == 426 && h == 240);
  OverlaySize(800, 1000, 320, 200, w, h);
  CHECK(w == 400 && h == 500);
  OverlaySize(100, 100, 320, 200, w, h);
  CHECK(w == 320 && h == 200);
}

// Quads are split into two triangles; a `glow_<texture>` material is
// emissive and names the texture after the prefix.
TEST(ObjParserReadsQuadsAndGlowMaterials) {
  const std::string obj =
      "o test\n"
      "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
      "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
      "vn 0 0 1\n"
      "usemtl glow_computer\n"
      "f 1/1/1 2/2/1 3/3/1 4/4/1\n";
  std::vector<Batch> out;
  std::string err;
  CHECK(ParseObj(obj, out, &err));
  CHECK_EQ(out.size(), 1u);
  if (out.size() == 1) {
    CHECK(out[0].emissive);
    CHECK(out[0].texture == "computer");
    CHECK_EQ(out[0].verts.size(), 6u);
  }
}

// A `blend<percent>_<texture>` material is drawn blended: the texture after the
// underscore at that opacity; an ordinary material stays solid.
TEST(ObjParserReadsBlendMaterialsAsOpacity) {
  const std::string obj =
      "v 0 0 0\nv 1 0 0\nv 1 1 0\n"
      "usemtl blend35_dust\nf 1 2 3\n"
      "usemtl wood\nf 1 2 3\n"
      "usemtl blend100_glass\nf 1 2 3\n"
      "usemtl blendy\nf 1 2 3\n";
  std::vector<Batch> out;
  std::string err;
  CHECK(ParseObj(obj, out, &err));
  CHECK_EQ(out.size(), 4u);
  if (out.size() == 4) {
    CHECK(out[0].texture == "dust");
    CHECK(std::fabs(out[0].opacity - 0.35f) < 1e-6f);
    CHECK(out[1].texture == "wood");
    CHECK_EQ(out[1].opacity, 1.0f);
    CHECK(out[2].texture == "glass");
    CHECK_EQ(out[2].opacity, 1.0f);     // 100 percent is solid
    CHECK(out[3].texture == "blendy");  // not a blend material
    CHECK_EQ(out[3].opacity, 1.0f);
  }
}

// A `cutout_<texture>` material discards the transparent texels: leaves and
// fences.
TEST(ObjParserReadsCutoutMaterialsAsAlphaTest) {
  const std::string obj =
      "v 0 0 0\nv 1 0 0\nv 1 1 0\n"
      "usemtl cutout_leaf\nf 1 2 3\n"
      "usemtl wood\nf 1 2 3\n";
  std::vector<Batch> out;
  std::string err;
  CHECK(ParseObj(obj, out, &err));
  CHECK_EQ(out.size(), 2u);
  if (out.size() == 2) {
    CHECK(out[0].texture == "leaf");
    CHECK(out[0].alpha_test);
    CHECK(out[1].texture == "wood");
    CHECK(!out[1].alpha_test);
  }
}

TEST(ObjParserRejectsBadFaces) {
  std::vector<Batch> out;
  std::string err;
  CHECK(!ParseObj("v 0 0 0\nf 1 2 3\n", out, &err));
  CHECK(!err.empty());
}

// A malformed [furniture] line fails the whole parse with a message.
TEST(ZoneParserRejectsBadFurniture) {
  Level l;
  std::string err;
  CHECK(!l.ParseText("[map]\n...\n...\n[furniture]\n1 1 bed x\n", &err));
  CHECK(!err.empty());
}

// A [furniture] line may carry the offsets and lift decor has: a piece pushed
// against the wall behind it. It stays solid in its cell, and the editor's
// document round-trips it (offsets are written only when there are any).
TEST(FurnitureCanBePushedAgainstAWallWithOffsets) {
  Level l;
  std::string err;
  CHECK(l.ParseText(
      "[map]\n....\n....\n[furniture]\n1 0 sink 270 0.3 -0.1 0\n2 1 bed 0\n",
      &err));
  const Placement* sink = l.FurnitureAt(1, 0);
  CHECK(sink != nullptr && sink->solid);
  CHECK(sink && std::fabs(sink->off_x - 0.3) < 1e-9 &&
        std::fabs(sink->off_z + 0.1) < 1e-9);
  CHECK(l.FurnitureAt(2, 1) && l.FurnitureAt(2, 1)->off_x == 0.0);
  editor::LevelDoc doc;
  CHECK(
      editor::ParseLevelDoc("[map]\n..\n..\n[furniture]\n0 0 sink 270 0.3 -0.1 "
                            "0\n1 1 bed 0\n[decor]\n",
                            &doc, &err));
  CHECK_EQ(doc.furniture.size(), 2u);
  CHECK(std::fabs(doc.furniture[0].off_x - 0.3) < 1e-9);
  const std::string out = editor::SerializeLevelDoc(doc);
  CHECK(out.find("0 0 sink 270 0.3 -0.1 0\n") != std::string::npos);
  CHECK(out.find("1 1 bed 0\n") != std::string::npos);  // none: not written
}

// [decor] lines are non-solid placements: they never block a cell, are not
// found by FurnitureAt, and carry an optional offset and lift.
TEST(ADecorSectionPlacesNonSolidPieces) {
  Level l;
  std::string err;
  CHECK(
      l.ParseText("[map]\n...\n...\n[furniture]\n0 0 bed 90\n[decor]\n1 1 vase "
                  "180 0.25 -0.1 0.78\n2 1 duck 0\n",
                  &err));
  CHECK_EQ(l.Furniture().size(), 3u);
  CHECK(!l.Passable(0, 0));
  CHECK(l.Passable(1, 1) && l.Passable(2, 1));
  CHECK(l.FurnitureAt(1, 1) == nullptr);
  const Placement& vase = l.Furniture()[1];
  CHECK(!vase.solid && vase.model == "vase");
  CHECK(vase.off_x > 0.249 && vase.off_x < 0.251 && vase.off_z < -0.099 &&
        vase.off_z > -0.101);
  CHECK(vase.lift > 0.779 && vase.lift < 0.781);
  CHECK(l.Furniture()[2].lift == 0.0 && l.Furniture()[2].off_x == 0.0);
  CHECK(!l.ParseText("[map]\n...\n[decor]\n5 5 vase 0\n", &err));
  CHECK(!l.ParseText("[map]\n...\n[decor]\n1 0 vase\n", &err));
}

namespace {
// Euclidean distance between two points.
double Dist3(const double a[3], const double b[3]) {
  return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) +
                   (a[1] - b[1]) * (a[1] - b[1]) +
                   (a[2] - b[2]) * (a[2] - b[2]));
}
}

// The arm solver (two-bone IK) keeps both bone lengths fixed and reaches
// any target within reach.
TEST(AnArmKeepsItsBoneLengthsAndReachesWhatIsInReach) {
  const BodyParams bt;
  const double shoulder[3] = {0, 1.4, 0}, bend[3] = {0, -1, 0.3};
  for (const double target : {0.15, 0.3, 0.5, 0.6, 0.9}) {
    const double t[3] = {target * 0.6, 1.2, target * 0.8};
    const ArmIk ik = SolveArm(shoulder, t, bt.upper_arm, bt.fore_arm, bend);
    CHECK(std::abs(Dist3(shoulder, ik.elbow) - bt.upper_arm) < 1e-6);
    CHECK(std::abs(Dist3(ik.elbow, ik.hand) - bt.fore_arm) < 1e-6);
    const double want = Dist3(shoulder, t);
    if (want < bt.upper_arm + bt.fore_arm - 0.01) {
      CHECK(Dist3(ik.hand, t) < 1e-6);
    } else {
      CHECK(Dist3(shoulder, ik.hand) <= bt.upper_arm + bt.fore_arm);
    }
    CHECK(ik.elbow[1] <= shoulder[1] + 1e-9);
  }
}

// The chest yaw trails the view yaw, so the head can turn a little first.
TEST(TheChestFollowsTheViewButTheHeadMayTurnFirst) {
  const BodyParams bt;
  CHECK(std::abs(TurnBody(0.0, bt.turn_limit * 0.5, 0.0, false, bt)) < 1e-9);
  const double dragged = TurnBody(0.0, bt.turn_limit + 0.5, 0.0, false, bt);
  CHECK(std::abs(dragged - 0.5) < 1e-9);
  double yaw = 0.0;
  for (int i = 0; i < 300; ++i) {
    yaw = TurnBody(yaw, 0.6, 0.02, true, bt);
  }
  CHECK(std::abs(yaw - 0.6) < 0.01);
  CHECK(std::abs(TurnBody(3.1, -3.1, 0.0, false, bt) - 3.1) < 1e-9 ||
        std::abs(TurnBody(3.1, -3.1, 0.0, false, bt)) > 3.0);
}

TEST(TheBodyStandsBehindTheEyeAndTheHandHoldsTheLightWhereTheViewShowsIt) {
  const BodyParams bt;
  BodyInput in;
  in.eye_x = 5;
  in.eye_z = 7;
  in.eye_height = 1.55;
  in.yaw = 0.7;
  in.body_yaw = 0.7;
  in.raise = 1.0;
  const BodyPose p = PoseBody(in, bt, HeldParams{});
  CHECK(std::abs(p.torso.m[13]) < 1e-6);
  const double dx = p.torso.m[12] - in.eye_x, dz = p.torso.m[14] - in.eye_z;
  CHECK(std::abs(std::hypot(dx, dz) - bt.eye_ahead) < 1e-6);
  CHECK(dx * std::cos(in.yaw) + dz * std::sin(in.yaw) < 0);
  const double hx = p.hand_x - in.eye_x, hz = p.hand_z - in.eye_z;
  CHECK(p.hand_y < in.eye_height);
  CHECK(hx * std::cos(in.yaw) + hz * std::sin(in.yaw) > 0.1);
  CHECK(std::abs(p.held.m[12] - p.hand_x) < 1e-5 &&
        std::abs(p.held.m[14] - p.hand_z) < 1e-5);
  const double wrist_x = p.fore_arm_r.m[12] + p.fore_arm_r.m[8] * bt.fore_arm,
               wrist_z = p.fore_arm_r.m[14] + p.fore_arm_r.m[10] * bt.fore_arm;
  CHECK(std::hypot(wrist_x - p.hand_x, wrist_z - p.hand_z) < 0.05);
  for (const PartMatrix* m :
       {&p.torso, &p.head, &p.leg_l, &p.leg_r, &p.upper_arm_l, &p.fore_arm_l,
        &p.upper_arm_r, &p.fore_arm_r, &p.held}) {
    for (int c = 0; c < 3; ++c) {
      const size_t o = static_cast<size_t>(c) * 4;
      const double len =
          std::sqrt(static_cast<double>(m->m[o]) * m->m[o] +
                    static_cast<double>(m->m[o + 1]) * m->m[o + 1] +
                    static_cast<double>(m->m[o + 2]) * m->m[o + 2]);
      CHECK(std::abs(len - 1.0) < 1e-4);
    }
  }
}

// Body parameters load from JSON and invalid values are rejected.
TEST(BodyParamsLoadAndRejectNonsense) {
  json::Value v;
  std::string err;
  CHECK(json::Parse(
      R"({"body": {"neckHeight": 1.5, "eyeAhead": 0.1, "hipHeight": 0.88, "hipHalfSpacing": 0.09, "shoulderHeight": 1.42, "shoulderHalfSpacing": 0.22, "upperArm": 0.3, "foreArm": 0.335, "legSwing": 0.55, "turnLimit": 0.9, "raiseSeconds": 0.25}})",
      v, &err));
  json::Reader r(v, "t");
  BodyParams p;
  CHECK(LoadBodyParams(r, "body", p));
  CHECK(p.fore_arm > 0.33 && p.fore_arm < 0.34);
  json::Reader r2(v, "t");
  HeldParams h;
  CHECK(!LoadHeldParams(r2, "held", h));
}

// The hand lags behind a turn, settles back, and sways while walking.
TEST(TheHandLagsATurnSettlesAndSwaysAtAWalk) {
  const HandSwayParams p;
  HandSway sway;
  HandPose still;
  for (int i = 0; i < 120; ++i) {
    still = sway.Update(1.0 / 60, i / 60.0, 5, 5, 0.3, 0.0, p);
  }
  CHECK(still.walk < 0.01);
  CHECK(std::abs(still.yaw) < 0.05);
  const HandPose turned = sway.Update(1.0 / 60, 2.0, 5, 5, 0.6, 0.0, p);
  CHECK(turned.yaw < -0.05);
  HandSway walker;
  HandPose w;
  for (int i = 0; i < 180; ++i) {
    w = walker.Update(1.0 / 60, i / 60.0, 5 + i * 0.05, 5, 0, 0, p);
  }
  CHECK(w.walk > 0.9);
  CHECK(w.stride > 1.0);
  HandSway jumped;
  jumped.Update(1.0 / 60, 0, 0, 0, 0, 0, p);
  const HandPose j = jumped.Update(1.0, 1.0, 50, 50, 2.0, 0, p);
  CHECK(std::abs(j.yaw) < 0.2 && j.walk < 0.01);
}

TEST(TheChestStartsFacingTheViewAndTheArmRisesAndFalls) {
  const BodyParams bt;
  BodyMotion m;
  m.Update(0.016, 1.2, false, false, bt);
  CHECK(std::abs(m.Yaw() - 1.2) < 1e-9);
  for (int i = 0; i < 60; ++i) m.Update(0.016, 1.2, false, true, bt);
  CHECK(m.Raise() > 0.95);
  for (int i = 0; i < 60; ++i) m.Update(0.016, 1.2, false, false, bt);
  CHECK(m.Raise() < 0.05);
}

// A mirror prop is placed only when the eye is near enough and in sight
// of the glass, and only if the prop has a model.
TEST(AMirrorShowsOnlyInRangeAndInSight) {
  MirrorParams m;
  m.range = 5;
  Prop mirror;
  mirror.model = "mirror";
  mirror.at = {.x = 2.5, .y = 1.0};
  mirror.yaw_deg = 0;
  mirror.lift = 1.0;
  Level level;
  std::string err;
  CHECK(level.Parse({"#####", "#...#", "#...#", "#...#", "#####"}, &err));
  const MirrorPlacement near = PlaceMirror(m, mirror, 2.5, 3.0, level);
  CHECK(near.on);
  CHECK(std::abs(near.nz - 1.0) < 1e-9 && std::abs(near.nx) < 1e-9);
  CHECK(std::abs(near.y - (1.0 + m.glass_height)) < 1e-9);
  CHECK(!PlaceMirror(m, mirror, 2.5, 30.0, level).on);
  mirror.model.clear();
  CHECK(!PlaceMirror(m, mirror, 2.5, 3.0, level).on);
}

// The 5x7 bitmap font and the SDF text layout.
TEST(FontMeasuresText) {
  CHECK_EQ(TextWidth("AB", 1), 11);
  CHECK_EQ(TextWidth("AB", 2), 22);
  CHECK_EQ(TextWidth("", 1), 0);
}

// Reference wrapper for the SDF font test: breaks greedily, remeasuring the
// whole candidate line each time.
static std::vector<std::string> WrapByRemeasuring(const SdfFont& font,
                                                  const std::string& text,
                                                  float size, float max_width) {
  std::vector<std::string> lines;
  std::string line;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find(' ', start);
    const std::string word = text.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (!word.empty()) {
      std::string candidate = word;
      if (!line.empty()) {
        candidate = line;
        candidate += ' ';
        candidate += word;
      }
      if (!line.empty() && font.Width(candidate, size) > max_width) {
        lines.push_back(line);
        line = word;
      } else {
        line = candidate;
      }
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

TEST(SdfFontWrapMatchesRemeasuringEveryCandidate) {
  SdfFont font;
  std::string err;
  CHECK(font.LoadFile(
      std::string(MYTHOS_TEST_ASSET_DIR) + "/fonts/Alegreya.ttf", &err));
  if (!font.Loaded()) return;
  // One long line is split over two literals on purpose.
  const std::string texts[] = {
      // NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
      "The quick brown fox jumps over the lazy dog, and then AVATAR To Wa yo "
      "walks home",
      "  leading and   repeated   spaces  ",
      "supercalifragilisticexpialidocious is longer than any narrow line",
      "Η γρήγορη καφέ αλεπού πηδά πάνω από το τεμπέλικο σκυλί",
      "",
  };
  for (const std::string& text : texts) {
    for (const float size : {14.0f, 22.0f, 40.0f}) {
      for (const float max_width : {60.0f, 140.0f, 333.0f, 900.0f}) {
        CHECK(font.Wrap(text, size, max_width) ==
              WrapByRemeasuring(font, text, size, max_width));
      }
    }
  }
  CHECK_EQ(font.Kerning('A', 'V'), font.Kerning('A', 'V'));
  CHECK(font.GlyphFor('A').advance > 0.0f);
  CHECK(font.GlyphFor(0x3B1).advance > 0.0f);
}

TEST(FontDrawsPixelsAndLowercaseMatchesUppercase) {
  Surface a(40, 10, 0), b(40, 10, 0);
  DrawText(a, 0, 0, "Hi", 0xFFFFFFFF);
  DrawText(b, 0, 0, "HI", 0xFFFFFFFF);
  int lit = 0;
  for (const uint32_t p : a.px) lit += p != 0;
  CHECK(lit > 0);
  CHECK(a.px == b.px);
}

// JSON parsing and the tile legend loaded from it.
TEST(EngineJsonParsesValuesAndReportsErrors) {
  json::Value v;
  std::string err;

  const std::string text =
      R"({"a": [1, -2.5e1, true, null], "s": "\u0393\u03b5 \"q\" \ud83d\ude00", "o": {}})";
  CHECK(json::Parse(text, v, &err));
  CHECK(v.is_object());
  const json::Value* a = json::Find(v, "a");
  CHECK(a && a->is_array() && a->size() == 4);
  if (a && a->size() == 4) {
    CHECK((*a)[1].get<double>() == -25.0);
    CHECK((*a)[2].get<bool>());
    CHECK((*a)[3].is_null());
  }
  CHECK(json::Find(v, "s") &&
        json::Find(v, "s")->get<std::string>() == "Γε \"q\" \xF0\x9F\x98\x80");
  CHECK(v.size() == 3 && std::prev(v.end()).key() == "o");
  CHECK(!json::Parse("{\"a\": 1,\n \"a\": 2}", v, &err));
  CHECK(err.find("line 2") == 0);
  CHECK(!json::Parse("[1, 2", v, &err));
  CHECK(!json::Parse("{'a': 1}", v, &err));
  CHECK(!json::Parse("01", v, &err));
  CHECK(!json::Parse("\"\\ud800\"", v, &err));
}

TEST(EngineTileLegendLoadsFromJson) {
  json::Value v;
  const std::string legend =
      R"({"defaultWall": "stone", "tiles": {"#": {"solid": true}, "D": {"door": "open"},
                           "*": {"sprite": "star", "spriteSize": 0.5}}})";
  CHECK(json::Parse(legend, v));
  TileSet t;
  std::string err;
  CHECK(ParseTileSet(v, t, &err));
  CHECK(t.Solid('#') && t.IsOpenDoor('D') && !t.Solid('D'));
  CHECK(t.SpriteTexture('*') == "star" && t['*'].sprite_size == 0.5f);
  CHECK(t.WallTexture('#') == "stone");
  const std::string typo = R"({"tiles": {"#": {"solidd": true}}})";
  CHECK(json::Parse(typo, v));
  CHECK(!ParseTileSet(v, t, &err));
  CHECK(err.find("solidd") != std::string::npos);
}

TEST(EngineDefaultLegendKnowsOnlyTheHashWall) {
  Level l;
  CHECK(l.Parse({"#####", "#WLI#", "#####"}));
  CHECK(!l.Passable(0, 0));
  CHECK(l.Passable(1, 1));
  CHECK(l.Passable(2, 1));
  const auto tiles = std::make_shared<TileSet>();
  (*tiles)['W'].solid = true;
  l.SetTiles(tiles);
  CHECK(!l.Passable(1, 1));
  CHECK(l.Passable(2, 1));
  const Level copy = l;
  CHECK(!copy.Passable(1, 1));
}

namespace {
// A legend with a thin wall `W`, a window `w` and a thin closed door `N`.
std::shared_ptr<TileSet> ThinLegend() {
  auto tiles = std::make_shared<TileSet>();
  (*tiles)['W'].solid = true;
  (*tiles)['W'].thin = true;
  (*tiles)['w'].solid = true;
  (*tiles)['w'].window = true;
  (*tiles)['N'].solid = true;
  (*tiles)['N'].thin = true;
  (*tiles)['N'].door = DoorKind::kClosed;
  return tiles;
}
}

// Thin walls and windows: the slab (0.2 m at the middle of the cell) blocks
// movement and sight, and the mesher builds geometry for it.
TEST(EngineThinWallsBlockOnlyTheirSlab) {
  Level l;
  CHECK(l.Parse({"#######", "#..W..#", "#..W..#", "#..W..#", "#######"}));
  l.SetTiles(ThinLegend());
  CHECK(CanOccupy(l, 3.2, 2.5, 0.15));
  CHECK(!CanOccupy(l, 3.5, 2.5, 0.15));
  CHECK(!CanOccupy(l, 3.62, 2.5, 0.15));
  CHECK(CanOccupy(l, 2.5, 2.5, 0.15));
  Vec2 p{.x = 2.5, .y = 2.5};
  for (int i = 0; i < 20; ++i) MoveWithCollision(l, p, 0.1, 0.0, 0.15);
  CHECK(p.x > 3.2 && p.x < 3.3);
  CHECK(l.Tiles().Thin('W') && !l.Tiles().Thin('#'));
}

TEST(EngineThinWallsJoinUpAndBlockSight) {
  Level l;
  CHECK(l.Parse({"#####", "#WWW#", "#...#", "#####"}));
  l.SetTiles(ThinLegend());
  CHECK_EQ(l.ThinArms(2, 1), 1u + 4u + 8u);
  CHECK_EQ(l.ThinRects(2, 1).size(), 4u);
  CHECK(l.ThinRects(2, 2).empty());
  CHECK(l.LineOfSight(1.5, 2.5, 3.5, 2.5));
  CHECK(!l.LineOfSight(2.5, 2.5, 2.5, 0.5));
  CHECK(l.LineOfSight(2.5, 2.5, 2.5, 1.7));
}

TEST(EngineNavigationShortestPathsAndReachability) {
  Level level;
  CHECK(level.Parse({"#######", "#.....#", "#.###.#", "#.....#", "#######"}));
  const std::vector<NavCell> expected{{1, 1}, {2, 1}, {3, 1}, {4, 1},
                                      {5, 1}, {5, 2}, {5, 3}};
  CHECK(FindPath(level, {1, 1}, {5, 3}) == expected);
  CHECK(FindPath(level, {1, 1}, {5, 3}) == expected);
  CHECK_EQ(ReachableCells(level, {1, 1}).size(), size_t{12});
  CHECK_EQ(FindPath(level, {1, 1}, {1, 1}).size(), size_t{1});
  CHECK(FindPath(level, {-1, 1}, {1, 1}).empty());
  CHECK(FindPath(level, {1, 1}, {3, 2}).empty());
  CHECK(ReachableCells(level, {3, 2}).empty());
  level.Set(5, 2, '#');
  level.Set(1, 2, '#');
  CHECK(FindPath(level, {1, 1}, {5, 3}).empty());
  CHECK_EQ(ReachableCells(level, {1, 1}).size(), size_t{5});
}

TEST(EngineNavigationDoorsAndBodyClearance) {
  Level level;
  CHECK(level.Parse({"#####", "#.N.#", "#####"}));
  auto tiles = std::make_shared<TileSet>();
  (*tiles)['N'].solid = true;
  (*tiles)['N'].door = DoorKind::kClosed;
  (*tiles)['n'].door = DoorKind::kOpen;
  level.SetTiles(tiles);
  CHECK(FindPath(level, {1, 1}, {3, 1}).empty());
  level.Set(2, 1, 'n');
  CHECK_EQ(FindPath(level, {1, 1}, {3, 1}).size(), size_t{3});
  NavOptions options;
  options.avoid_doors = true;
  CHECK(FindPath(level, {1, 1}, {3, 1}, options).empty());
  options.avoid_doors = false;
  options.check_occupancy = true;
  options.radius = 0.2;
  CHECK_EQ(FindPath(level, {1, 1}, {3, 1}, options).size(), size_t{3});
  options.radius = 0.5;
  CHECK(FindPath(level, {1, 1}, {3, 1}, options).empty());
  options.radius = std::numeric_limits<double>::quiet_NaN();
  CHECK(ReachableCells(level, {1, 1}, options).empty());

  CHECK(level.Parse({".......", ".......", "..W....", ".......", "......."}));
  level.SetTiles(ThinLegend());
  options.radius = 0.3;
  CHECK_EQ(FindPath(level, {1, 3}, {3, 3}, options).size(), size_t{3});
  options.radius = 0.91;
  CHECK(FindPath(level, {1, 3}, {3, 3}, options).empty());
  options.check_occupancy = false;
  CHECK_EQ(FindPath(level, {1, 3}, {3, 3}, options).size(), size_t{3});
}

TEST(EngineLegendReadsThinAndWindow) {
  json::Value v;
  CHECK(json::Parse(R"({"tiles": {"W": {"solid": true, "thin": true},
                        "w": {"solid": true, "window": true}}})",
                    v));
  TileSet t;
  std::string err;
  CHECK(ParseTileSet(v, t, &err));
  CHECK(t.Thin('W') && t['w'].window && t.Thin('w'));
  CHECK(!t.Thin('#'));
}

TEST(EngineDecalsPersistAndEvictOldest) {
  editor::LevelDoc doc;
  doc.rows = {"...", "..."};
  for (const auto face :
       {DecalFace::kNorth, DecalFace::kSouth, DecalFace::kWest,
        DecalFace::kEast, DecalFace::kFloor, DecalFace::kCeiling,
        DecalFace::kThinNorth, DecalFace::kThinSouth, DecalFace::kThinWest,
        DecalFace::kThinEast}) {
    Decal d;
    d.x = 1;
    d.face = face;
    d.offset_u = 0.123456789012345;
    d.width = 0.000001;
    d.rotation = 37.25;
    d.texture = "stain";
    doc.decals.push_back(d);
  }
  const std::string text = editor::SerializeLevelDoc(doc);
  editor::LevelDoc loaded;
  std::string error;
  CHECK(editor::ParseLevelDoc(text, &loaded, &error));
  CHECK(loaded.decals == doc.decals);
  Level level;
  CHECK(level.ParseText(text, &error));
  CHECK(level.Decals() == doc.decals);
  loaded.Resize(1, 1, '.');
  CHECK(loaded.decals.empty());
  Decal d = doc.decals.front();
  const auto revision = level.Revision();
  d.width = std::numeric_limits<double>::infinity();
  CHECK(!level.AddDecal(d));
  CHECK(level.Revision() == revision);
  CHECK(
      !level.ParseText("[map]\n.\n[decals]\n0 0 up 0 0 1 1 0 stain\n", &error));
  CHECK(level.Parse({"."}));
  d = doc.decals.front();
  d.x = 0;
  for (size_t i = 0; i < Level::kMaxDecals; ++i) {
    d.rotation = static_cast<double>(i);
    CHECK(level.AddDecal(d));
  }
  CHECK(level.AddDecal(level.Decals().front()));
  CHECK_EQ(level.Decals().size(), Level::kMaxDecals);
  CHECK(level.Decals().front().rotation == 1);
  CHECK(level.Decals().back().rotation == 0);
}

TEST(EngineDecalsBuildOnEachCellFaceAndStack) {
  Level level;
  CHECK(level.Parse({"###", "#.#", "###"}));
  for (const auto face :
       {DecalFace::kNorth, DecalFace::kSouth, DecalFace::kWest,
        DecalFace::kEast, DecalFace::kFloor, DecalFace::kCeiling}) {
    Decal d;
    d.x = 1;
    d.y = 1;
    d.face = face;
    d.texture = "stain";
    CHECK(level.AddDecal(d));
  }
  ModelLibrary models;
  SceneGeometry geometry = BuildSceneGeometry(level, models, false);
  CHECK_EQ(geometry.decals.size(), size_t{6});
  const float normals[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0},
                               {1, 0, 0},  {0, 1, 0}, {0, -1, 0}};
  for (size_t i = 0; i < geometry.decals.size(); ++i) {
    CHECK_EQ(geometry.decals[i].verts.size(), size_t{6});
    for (const auto& v : geometry.decals[i].verts) {
      CHECK(v.nx == normals[i][0] && v.ny == normals[i][1] &&
            v.nz == normals[i][2]);
    }
  }
  SceneGeometry upper = BuildSceneGeometry(level, models, false, kFloorPitch);
  CHECK(std::abs(upper.decals[0].verts[0].py - geometry.decals[0].verts[0].py -
                 kFloorPitch) < 1e-4f);
  AppendSceneGeometry(geometry, std::move(upper));
  CHECK_EQ(geometry.decals.size(), size_t{12});
}

TEST(EngineTriggersPersistAndValidate) {
  editor::LevelDoc doc;
  doc.rows = {"...", "..."};
  Trigger box;
  box.id = "box";
  box.event = TriggerEvent::kExit;
  box.once = true;
  box.min = {-0.125, 0.25, 1};
  box.max = {2.5, 3, 4};
  Trigger cell;
  cell.id = "cell";
  cell.shape = TriggerShape::kCell;
  cell.event = TriggerEvent::kUse;
  cell.cell_x = 2;
  cell.cell_z = 1;
  cell.floor = -1;
  doc.triggers = {box, cell};
  const std::string text = editor::SerializeLevelDoc(doc);
  CHECK(!text.empty());
  editor::LevelDoc loaded;
  std::string error;
  CHECK(editor::ParseLevelDoc(text, &loaded, &error));
  CHECK_EQ(loaded.triggers.size(), size_t{2});
  CHECK(editor::SerializeLevelDoc(loaded) == text);
  Level level;
  CHECK(level.ParseText(text, &error));
  CHECK_EQ(level.Triggers().size(), size_t{2});
  CHECK(level.Triggers()[0].min.x == box.min.x);
  CHECK(level.Triggers()[0].once);
  CHECK(level.Triggers()[1].floor == -1);
  CHECK(!level.AddTrigger(box));
  CHECK(level.RemoveTrigger("box"));
  CHECK(!level.RemoveTrigger("box"));
  loaded.Resize(2, 2, '.');
  CHECK_EQ(loaded.triggers.size(), size_t{1});
  CHECK(loaded.triggers[0].id == "box");
  CHECK(
      !level.ParseText("[map]\n.\n[triggers]\na cell enter 0 1 0 0\n", &error));
  CHECK(
      !level.ParseText("[map]\n.\n[triggers]\na cell enter 0 0 0 0\n"
                       "a cell exit 0 0 0 0\n",
                       &error));
  CHECK(!level.ParseText("[map]\n.\n[triggers]\na box enter 1 1 0 0 0 1 1\n",
                         &error));
  CHECK(level.ParseText("[map]\n.\n", &error));
  CHECK(level.Triggers().empty());
}

TEST(EngineEventRulesPersistAndValidate) {
  editor::LevelDoc doc;
  doc.rows = {".."};
  EventRule trigger{
      "open door",
      {EventKind::kTrigger, "door", "enter", false},
      {{"has key", true}, {"locked", false}},
      {{"message", "A \"quoted\" message\nnext line"}, {"open", ""}}};
  EventRule flag{
      "alarm", {EventKind::kFlag, "alarm", "", true}, {}, {{"sound", "bell"}}};
  EventRule timer{"timer", {EventKind::kTimer, "clock", "", false}, {}, {}};
  doc.rules = {trigger, flag, timer};
  const std::string text = editor::SerializeLevelDoc(doc);
  CHECK(!text.empty());
  editor::LevelDoc loaded;
  std::string error;
  CHECK(editor::ParseLevelDoc(text, &loaded, &error));
  CHECK(loaded.rules == doc.rules);
  CHECK(editor::SerializeLevelDoc(loaded) == text);
  Level level;
  CHECK(level.ParseText(text, &error));
  CHECK(level.Rules() == doc.rules);
  EventRuntime runtime;
  for (const auto& rule : level.Rules()) CHECK(runtime.Add(rule));
  std::vector<std::string> arguments;
  CHECK(runtime.Dispatch(
      trigger.event, {{"has key", true}},
      [&](const EventAction& a) { arguments.push_back(a.argument); }));
  CHECK(arguments ==
        std::vector<std::string>({trigger.actions[0].argument, ""}));
  const std::string line = SerializeEventRuleLine(trigger);
  CHECK(!line.empty());
  EventRule parsed = flag;
  CHECK(!ParseEventRuleLine("not a rule", &parsed));
  CHECK(parsed == flag);
  CHECK(!level.ParseText("[map]\n.\n[rules]\n" + line + "\n" + line + "\n",
                         &error));
  CHECK(!editor::ParseLevelDoc(
      "[map]\n.\n[rules]\n" + line + "\n" + line + "\n", &loaded, &error));
  doc.rules.push_back(trigger);
  CHECK(editor::SerializeLevelDoc(doc).empty());
  doc.rules.pop_back();
  doc.rules[0].actions[0].type.clear();
  CHECK(editor::SerializeLevelDoc(doc).empty());
  CHECK(level.ParseText("[map]\n.\n", &error));
  CHECK(level.Rules().empty());
}

TEST(EngineEventGraphIntegratesLevelTriggersAndRules) {
  editor::LevelDoc doc;
  doc.rows = {"."};
  for (const auto& [id, event] :
       std::vector<std::pair<std::string, TriggerEvent>>{
           {"a", TriggerEvent::kEnter},
           {"b", TriggerEvent::kStay},
           {"c", TriggerEvent::kUse},
           {"d", TriggerEvent::kExit}}) {
    Trigger trigger;
    trigger.id = id;
    trigger.shape = TriggerShape::kCell;
    trigger.event = event;
    trigger.once = id == "a";
    doc.triggers.push_back(trigger);
  }
  for (const auto& [id, detail] :
       std::vector<std::pair<std::string, std::string>>{
           {"a", "enter"}, {"b", "stay"}, {"c", "use"}, {"d", "exit"}})
    doc.rules.push_back({id,
                         {EventKind::kTrigger, id, detail, false},
                         {{"enabled", true}},
                         {{id, detail}}});
  doc.rules.push_back(
      {"timer", {EventKind::kTimer, "clock", "", false}, {}, {{"tick", ""}}});
  Level level;
  CHECK(level.ParseText(editor::SerializeLevelDoc(doc)));
  EventGraphRuntime graph;
  CHECK(graph.Load(level));
  EventGraphRuntime::Flags flags{{"enabled", true}};
  std::vector<std::string> actions;
  auto record = [&](const EventAction& action) {
    actions.push_back(action.type);
  };
  CHECK(!graph.Sample({0.5, 0, 0.5}, 0, true, flags, {}));
  CHECK(!graph.Sample({std::numeric_limits<double>::quiet_NaN(), 0, 0}, 0,
                      false, flags, record));
  CHECK(graph.Sample({0.5, 0, 0.5}, 0, true, flags, [&](const EventAction& a) {
    actions.push_back(a.type);
    flags["enabled"] = false;
    CHECK(!graph.Reset());
    CHECK(!graph.Sample({2, 0, 0}, 0, false, flags, record));
  }));
  CHECK(actions == std::vector<std::string>({"a", "c"}));
  flags["enabled"] = true;
  actions.clear();
  CHECK(graph.Sample({0.5, 0, 0.5}, 0, true, flags, record));
  CHECK(actions == std::vector<std::string>({"b", "c"}));
  actions.clear();
  CHECK(graph.Sample({0.5, 0, 0.5}, 1, false, flags, record));
  CHECK(actions == std::vector<std::string>({"d"}));
  actions.clear();
  CHECK(graph.Sample({0.5, 0, 0.5}, 0, false, flags, record));
  CHECK(actions.empty());
  CHECK(graph.Reset());
  CHECK(graph.Sample({0.5, 0, 0.5}, 0, false, flags, record));
  CHECK(actions == std::vector<std::string>({"a"}));
  actions.clear();
  CHECK(graph.Dispatch({EventKind::kTimer, "clock", "", false}, flags, record));
  CHECK(actions == std::vector<std::string>({"tick"}));
  CHECK(!graph.Dispatch({EventKind::kTrigger, "a", "enter", false}, flags,
                        record));
  CHECK(graph.Clear());
  actions.clear();
  CHECK(graph.Sample({0.5, 0, 0.5}, 0, true, flags, record));
  CHECK(actions.empty());
}

TEST(EngineEventRulesDispatchAndValidate) {
  EventRuntime runtime;
  EventRule rule;
  rule.id = "z";
  rule.event = {EventKind::kTrigger, "door", "enter", false};
  rule.conditions = {{"key", true}, {"locked", false}};
  rule.actions = {{"sound", "open"}, {"game", "door"}};
  CHECK(runtime.Add(rule));
  CHECK(!runtime.Add(rule));
  rule.id = "a";
  rule.conditions.clear();
  rule.actions = {{"first", ""}};
  CHECK(runtime.Add(rule));
  EventRule invalid = rule;
  invalid.id = "bad";
  invalid.actions = {{"", ""}};
  CHECK(!runtime.Add(invalid));
  invalid.actions.clear();
  invalid.conditions = {{"", true}};
  CHECK(!runtime.Add(invalid));
  invalid.conditions.clear();
  invalid.event.kind = static_cast<EventKind>(99);
  CHECK(!runtime.Add(invalid));

  std::vector<std::string> actions;
  auto record = [&](const EventAction& action) {
    actions.push_back(action.type);
  };
  Event event{EventKind::kTrigger, "door", "enter", false};
  CHECK(runtime.Dispatch(event, {{"key", true}}, record));
  CHECK(actions == std::vector<std::string>({"first", "sound", "game"}));
  actions.clear();
  CHECK(runtime.Dispatch(event, {{"key", true}, {"locked", true}}, record));
  CHECK(actions == std::vector<std::string>({"first"}));
  actions.clear();
  event.detail = "exit";
  CHECK(runtime.Dispatch(event, {}, record));
  CHECK(actions.empty());
  event.detail.clear();
  CHECK(!runtime.Dispatch(event, {}, record));
  CHECK(!runtime.Dispatch(rule.event, {}, {}));

  EventRule flag{
      "flag", {EventKind::kFlag, "alarm", "", true}, {}, {{"alarm", ""}}};
  EventRule timer{
      "timer", {EventKind::kTimer, "clock", "", false}, {}, {{"tick", ""}}};
  CHECK(runtime.Add(flag));
  CHECK(runtime.Add(timer));
  CHECK(runtime.Dispatch({EventKind::kFlag, "alarm", "", false}, {}, record));
  CHECK(actions.empty());
  CHECK(runtime.Dispatch(flag.event, {}, record));
  CHECK(runtime.Dispatch(timer.event, {}, record));
  CHECK(actions == std::vector<std::string>({"alarm", "tick"}));

  actions.clear();
  CHECK(
      runtime.Dispatch(rule.event, {{"key", true}}, [&](const EventAction& a) {
        actions.push_back(a.type);
        runtime.Clear();
      }));
  CHECK(actions == std::vector<std::string>({"first", "sound", "game"}));
  CHECK(!runtime.Remove("a"));
}

TEST(EngineTriggerTransitionsAndValidation) {
  TriggerRuntime runtime;
  Trigger t;
  t.id = "enter";
  t.min = {0, 0, 0};
  t.max = {1, 2, 1};
  CHECK(runtime.Add(t));
  CHECK(!runtime.Add(t));
  t.id = "exit";
  t.event = TriggerEvent::kExit;
  CHECK(runtime.Add(t));
  t.id = "stay";
  t.event = TriggerEvent::kStay;
  t.once = true;
  CHECK(runtime.Add(t));
  t.id = "use";
  t.event = TriggerEvent::kUse;
  t.once = false;
  CHECK(runtime.Add(t));
  t.id = "cell";
  t.shape = TriggerShape::kCell;
  t.event = TriggerEvent::kEnter;
  t.cell_x = 1;
  t.floor = 2;
  CHECK(runtime.Add(t));
  t.id = "invalid";
  t.shape = TriggerShape::kBox;
  t.min.x = std::numeric_limits<double>::infinity();
  CHECK(!runtime.Add(t));

  std::vector<TriggerHit> hits;
  CHECK(runtime.Update({0.5, 1, 0.5}, 0, true, hits));
  CHECK_EQ(hits.size(), size_t{2});
  CHECK(hits[0].id == "enter" && hits[1].id == "use");
  CHECK(runtime.Update({0.5, 1, 0.5}, 0, false, hits));
  CHECK_EQ(hits.size(), size_t{1});
  CHECK(hits[0].id == "stay");
  CHECK(runtime.Update({0.5, 1, 0.5}, 0, false, hits));
  CHECK(hits.empty());
  CHECK(runtime.Update({1.5, 1, 0.5}, 2, false, hits));
  CHECK_EQ(hits.size(), size_t{2});
  CHECK(hits[0].id == "cell" && hits[1].id == "exit");
  CHECK(!runtime.Update({std::numeric_limits<double>::infinity(), 0, 0}, 0,
                        false, hits));
  CHECK_EQ(hits.size(), size_t{2});
  CHECK(runtime.Update({1, 1, 0.5}, 2, false, hits));
  CHECK_EQ(hits.size(), size_t{1});
  CHECK(hits[0].id == "enter");
  runtime.Reset();
  CHECK(runtime.Update({0.5, 1, 0.5}, 0, false, hits));
  CHECK(runtime.Update({0.5, 1, 0.5}, 0, false, hits));
  CHECK_EQ(hits.size(), size_t{1});
  CHECK(hits[0].id == "stay");
  CHECK(runtime.Remove("stay"));
  CHECK(!runtime.Remove("stay"));
}

TEST(EngineModelDecalTransformAndValidation) {
  Placement p;
  p.x = 2;
  p.y = 3;
  p.off_x = 0.25;
  p.off_z = -0.25;
  p.lift = 0.5;
  p.yaw_deg = 90;
  ModelDecal d;
  d.center = {0, 1, 1};
  d.normal = {0, 0, 1};
  d.tangent = {1, 0, 0};
  d.width = 2;
  d.height = 1;
  d.texture = "mark";
  const auto batch = BuildModelDecal(p, d);
  CHECK(batch.has_value());
  if (batch) {
    CHECK(batch->texture == "mark");
    CHECK_EQ(batch->verts.size(), size_t{6});
    for (const auto& v : batch->verts) {
      CHECK(std::abs(v.px - 3.75f) < 1e-4f);
      CHECK(v.py >= 0.9999f && v.py <= 2.0001f);
      CHECK(v.pz >= 2.2499f && v.pz <= 4.2501f);
      CHECK(std::abs(v.nx - 1) < 1e-4f && std::abs(v.nz) < 1e-4f);
      CHECK(v.u >= 0 && v.u <= 1 && v.v >= 0 && v.v <= 1);
    }
  }
  d.rotation_deg = 90;
  const auto rotated = BuildModelDecal(p, d);
  CHECK(rotated.has_value());
  if (rotated) {
    for (const auto& v : rotated->verts) {
      CHECK(v.py >= 0.4999f && v.py <= 2.5001f);
      CHECK(v.pz >= 2.7499f && v.pz <= 3.7501f);
    }
  }
  d.tangent = d.normal;
  CHECK(!BuildModelDecal(p, d));
  d.tangent = {1, 0, 0};
  d.width = 0;
  CHECK(!BuildModelDecal(p, d));
  d.width = 1;
  d.center[0] = std::numeric_limits<float>::infinity();
  CHECK(!BuildModelDecal(p, d));
}

TEST(EngineCellDecalsClipToFaceBounds) {
  Level level;
  CHECK(level.Parse({"###", "#.#", "###"}));
  for (const auto face :
       {DecalFace::kNorth, DecalFace::kSouth, DecalFace::kWest,
        DecalFace::kEast, DecalFace::kFloor, DecalFace::kCeiling}) {
    Decal d;
    d.x = d.y = 1;
    d.face = face;
    d.width = 3;
    d.height = 4;
    d.rotation = 23;
    d.texture = "stain";
    CHECK(level.AddDecal(d));
  }
  Decal outside;
  outside.x = outside.y = 1;
  outside.face = DecalFace::kFloor;
  outside.offset_u = 10;
  outside.texture = "outside";
  CHECK(level.AddDecal(outside));
  ModelLibrary models;
  const auto geometry = BuildSceneGeometry(level, models, false);
  CHECK_EQ(geometry.decals.size(), size_t{6});
  for (const auto& batch : geometry.decals) {
    CHECK(!batch.verts.empty());
    for (const auto& v : batch.verts) {
      CHECK(v.px >= 0.9999f && v.px <= 2.0001f);
      CHECK(v.pz >= 0.9999f && v.pz <= 2.0001f);
      CHECK(v.py >= -0.0001f && v.py <= kWallHeight + 0.0001f);
      CHECK(v.u >= 0 && v.u <= 1 && v.v >= 0 && v.v <= 1);
    }
  }
}

TEST(EngineHorizontalDecalsRequireSurfaces) {
  Level level;
  CHECK(level.Parse({".efcs#"}));
  auto tiles = std::make_shared<TileSet>();
  (*tiles)['e'].empty = true;
  (*tiles)['f'].open_floor = true;
  (*tiles)['c'].open_ceiling = true;
  (*tiles)['s'].open_floor = true;
  (*tiles)['s'].open_ceiling = true;
  level.SetTiles(tiles);
  for (int x = 0; x < 6; ++x) {
    for (const auto face : {DecalFace::kFloor, DecalFace::kCeiling}) {
      Decal d;
      d.x = x;
      d.face = face;
      d.texture = std::to_string(x) + (face == DecalFace::kFloor ? "f" : "c");
      CHECK(level.AddDecal(d));
    }
  }
  ModelLibrary models;
  const auto indoor = BuildSceneGeometry(level, models, false);
  CHECK_EQ(indoor.decals.size(), size_t{4});
  CHECK(indoor.decals[0].texture == "0f");
  CHECK(indoor.decals[1].texture == "0c");
  CHECK(indoor.decals[2].texture == "2c");
  CHECK(indoor.decals[3].texture == "3f");
  const auto outdoor = BuildSceneGeometry(level, models, true);
  CHECK_EQ(outdoor.decals.size(), size_t{2});
  CHECK(outdoor.decals[0].texture == "0f");
  CHECK(outdoor.decals[1].texture == "3f");
}

TEST(EngineWallDecalsRequireThickWallFaces) {
  Level level;
  CHECK(level.Parse({".....", ".###.", ".#...", ".#W..", "....."}));
  level.SetTiles(ThinLegend());
  for (const auto face : {DecalFace::kNorth, DecalFace::kSouth,
                          DecalFace::kWest, DecalFace::kEast}) {
    Decal d;
    d.x = 2;
    d.y = 2;
    d.face = face;
    d.texture = std::to_string(static_cast<int>(face));
    CHECK(level.AddDecal(d));
  }
  ModelLibrary models;
  const auto geometry = BuildSceneGeometry(level, models, false);
  CHECK_EQ(geometry.decals.size(), size_t{2});
  CHECK(geometry.decals[0].texture ==
        std::to_string(static_cast<int>(DecalFace::kNorth)));
  CHECK(geometry.decals[1].texture ==
        std::to_string(static_cast<int>(DecalFace::kWest)));
}

TEST(EngineThinCellHorizontalDecalsClipToExistingQuarters) {
  Level level;
  CHECK(level.Parse({"###", "#W.", "###"}));
  level.SetTiles(ThinLegend());
  for (const auto face : {DecalFace::kFloor, DecalFace::kCeiling}) {
    Decal d;
    d.x = d.y = 1;
    d.face = face;
    d.width = d.height = 3;
    d.rotation = 25;
    d.texture = "decal";
    CHECK(level.AddDecal(d));
  }
  ModelLibrary models;
  const auto geometry = BuildSceneGeometry(level, models, false);
  CHECK_EQ(geometry.decals.size(), size_t{2});
  for (const auto& batch : geometry.decals) {
    CHECK(!batch.verts.empty());
    for (const auto& v : batch.verts) {
      CHECK(v.px >= 1.4999f && v.px <= 2.0001f);
      CHECK(v.pz >= 0.9999f && v.pz <= 2.0001f);
      CHECK(v.u >= 0 && v.u <= 1 && v.v >= 0 && v.v <= 1);
    }
  }
}

TEST(EngineThinWallsAndWindowsBuildGeometry) {
  Level l;
  CHECK(l.Parse({"#######", "#.....#", "#WWwWW#", "#.....#", "#######"}));
  l.SetTiles(ThinLegend());
  ModelLibrary models;
  const SceneGeometry g = BuildSceneGeometry(l, models, false);
  bool glass = false;
  for (const Batch& b : g.world)
    glass = glass || (b.texture == "glass" && !b.verts.empty());
  CHECK(glass);

  bool slab_face = false;
  for (const Batch& b : g.world)
    for (const auto& v : b.verts)
      if (std::abs(v.pz - 2.4f) < 1e-4f || std::abs(v.pz - 2.6f) < 1e-4f)
        slab_face = true;
  CHECK(slab_face);
}

TEST(EngineThinWallDecalsClipToVisibleFaces) {
  Level level;
  CHECK(level.Parse({".....", ".WWW.", "..w..", "....."}));
  level.SetTiles(ThinLegend());
  for (const auto face : {DecalFace::kThinNorth, DecalFace::kThinSouth}) {
    Decal d;
    d.x = 2;
    d.y = 1;
    d.face = face;
    d.width = 3;
    d.height = 5;
    d.texture = "stain";
    CHECK(level.AddDecal(d));
  }
  Decal unsupported;
  unsupported.x = 0;
  unsupported.y = 0;
  unsupported.face = DecalFace::kThinNorth;
  unsupported.texture = "empty";
  CHECK(level.AddDecal(unsupported));
  ModelLibrary models;
  const auto geometry = BuildSceneGeometry(level, models, false);
  bool north = false, south = false;
  for (const auto& batch : geometry.decals) {
    CHECK(batch.texture == "stain");
    for (const auto& v : batch.verts) {
      CHECK(v.px >= 1.9999f && v.px <= 3.0001f);
      CHECK(v.py >= -0.0001f && v.py <= kWallHeight + 0.0001f);
      CHECK(v.u >= -0.0001f && v.u <= 1.0001f);
      CHECK(v.v >= -0.0001f && v.v <= 1.0001f);
      if (v.nz < -0.5f) {
        north = true;
        CHECK(std::abs(v.pz - 1.4f) < 1e-4f);
      } else if (v.nz > 0.5f) {
        south = true;
        CHECK(std::abs(v.pz - 1.6f) < 1e-4f);
      }
    }
  }
  CHECK(north);
  CHECK(south);
}

TEST(EngineWindowGlassDecalsClipAndPersist) {
  for (const bool along_x : {true, false}) {
    Level level;
    CHECK(level.Parse(along_x
                          ? std::vector<std::string>{".....", ".WwW.", "....."}
                          : std::vector<std::string>{".W.", ".w.", ".W."}));
    level.SetTiles(ThinLegend());
    for (const auto face : {DecalFace::kGlassNorth, DecalFace::kGlassSouth,
                            DecalFace::kGlassWest, DecalFace::kGlassEast}) {
      Decal d;
      d.x = along_x ? 2 : 1;
      d.y = 1;
      d.face = face;
      d.width = 3;
      d.height = 4;
      d.rotation = 20;
      d.texture = "glass_mark";
      Decal parsed;
      CHECK(ParseDecalLine(SerializeDecalLine(d), &parsed));
      CHECK(parsed == d);
      CHECK(level.AddDecal(d));
    }
    Decal missing;
    missing.face = DecalFace::kGlassNorth;
    missing.texture = "missing";
    CHECK(level.AddDecal(missing));
    ModelLibrary models;
    const auto geometry = BuildSceneGeometry(level, models, false);
    CHECK_EQ(geometry.decals.size(), size_t{2});
    for (const auto& batch : geometry.decals) {
      CHECK(batch.texture == "glass_mark");
      CHECK(!batch.verts.empty());
      for (const auto& v : batch.verts) {
        const float along = along_x ? v.px - 2 : v.pz - 1;
        CHECK(along >= 0.0999f && along <= 0.9001f);
        CHECK(v.py >= 0.8999f && v.py <= 2.0501f);
        CHECK(std::abs((along_x ? v.pz : v.px) - 1.5f) < 1e-4f);
        CHECK(v.u >= -0.0001f && v.u <= 1.0001f);
        CHECK(v.v >= -0.0001f && v.v <= 1.0001f);
      }
    }
  }
}

TEST(EngineWindowWallDecalsClipAroundOpening) {
  for (const bool along_x : {true, false}) {
    Level level;
    CHECK(level.Parse(along_x
                          ? std::vector<std::string>{".....", ".WwW.", "....."}
                          : std::vector<std::string>{".W.", ".w.", ".W."}));
    level.SetTiles(ThinLegend());
    for (const auto face : along_x
                               ? std::vector<DecalFace>{DecalFace::kThinNorth,
                                                        DecalFace::kThinSouth}
                               : std::vector<DecalFace>{DecalFace::kThinWest,
                                                        DecalFace::kThinEast}) {
      Decal d;
      d.x = along_x ? 2 : 1;
      d.y = 1;
      d.face = face;
      d.width = 3;
      d.height = 4;
      d.texture = "frame";
      CHECK(level.AddDecal(d));
      d.width = d.height = 0.1;
      d.offset_u = 0.25;
      d.offset_v = 0.12;
      d.texture = "opening";
      CHECK(level.AddDecal(d));
    }
    ModelLibrary models;
    const auto geometry = BuildSceneGeometry(level, models, false);
    CHECK_EQ(geometry.decals.size(), size_t{2});
    for (const auto& batch : geometry.decals) {
      CHECK(batch.texture == "frame");
      CHECK(!batch.verts.empty());
      for (size_t i = 0; i + 2 < batch.verts.size(); i += 3) {
        const auto& a = batch.verts[i];
        const auto& b = batch.verts[i + 1];
        const auto& c = batch.verts[i + 2];
        const float u = along_x ? (a.px + b.px + c.px) / 3 - 2
                                : (a.pz + b.pz + c.pz) / 3 - 1;
        const float y = (a.py + b.py + c.py) / 3;
        CHECK(y <= 0.9401f || y >= 2.0499f || u <= 0.1001f || u >= 0.8999f ||
              (u >= 0.4799f && u <= 0.5201f) || (y >= 1.4549f && y <= 1.4951f));
        for (const auto* v : {&a, &b, &c}) {
          CHECK(v->u >= -0.0001f && v->u <= 1.0001f);
          CHECK(v->v >= -0.0001f && v->v <= 1.0001f);
        }
      }
    }
  }
}

// Floors stacked in one scene and stairwell tiles (openCeiling /
// openFloor).
TEST(EngineStairwellTilesOpenTheCeilingAndTheFloor) {
  json::Value v;
  CHECK(json::Parse(R"({"tiles": {"U": {"openCeiling": true},
                        "v": {"openFloor": true}}})",
                    v));
  TileSet t;
  std::string err;
  CHECK(ParseTileSet(v, t, &err));
  CHECK(t['U'].open_ceiling && !t['U'].open_floor);
  CHECK(t['v'].open_floor && !t['v'].open_ceiling);
  Level l;
  CHECK(l.Parse({"#####", "#.U.#", "#####"}));
  l.SetTiles(std::make_shared<const TileSet>(t));
  ModelLibrary models;
  const auto count_quads_at = [&](const SceneGeometry& g, float y, float x) {
    int n = 0;
    for (const Batch& b : g.world)
      for (size_t i = 0; i + 2 < b.verts.size(); i += 3)
        if (std::abs(b.verts[i].py - y) < 1e-4f &&
            std::abs(b.verts[i + 1].py - y) < 1e-4f &&
            std::abs(b.verts[i + 2].py - y) < 1e-4f &&
            b.verts[i].px >= x - 1e-4f && b.verts[i].px <= x + 1.0001f &&
            b.verts[i + 1].px >= x - 1e-4f &&
            b.verts[i + 1].px <= x + 1.0001f &&
            b.verts[i + 2].px >= x - 1e-4f &&
            b.verts[i + 2].px <= x + 1.0001f &&
            std::abs(b.verts[i].ny) > 0.5f && b.verts[i].pz > 0.99f &&
            b.verts[i].pz < 2.01f && b.verts[i + 1].pz > 0.99f &&
            b.verts[i + 1].pz < 2.01f && b.verts[i + 2].pz > 0.99f &&
            b.verts[i + 2].pz < 2.01f)
          ++n;
    return n;
  };
  const SceneGeometry g = BuildSceneGeometry(l, models, false);
  CHECK(count_quads_at(g, kWallHeight, 1.0f) > 0);
  CHECK_EQ(count_quads_at(g, kWallHeight, 2.0f), 0);
  CHECK(count_quads_at(g, 0.0f, 2.0f) > 0);
}

TEST(EngineBuildsAFloorHigherUp) {
  Level l;
  CHECK(l.Parse({"###", "#.#", "###"}));
  ModelLibrary models;
  const SceneGeometry low = BuildSceneGeometry(l, models, false);
  SceneGeometry up = BuildSceneGeometry(l, models, false, kFloorPitch);
  CHECK_EQ(low.world.size(), up.world.size());
  float lo = 1e9f;
  for (const Batch& b : up.world)
    for (const auto& v : b.verts) lo = std::min(lo, v.py);
  CHECK(std::abs(lo - kFloorPitch) < 1e-4f);
  static_assert(kFloorPitch > kWallHeight);
  SceneGeometry both = BuildSceneGeometry(l, models, false);
  const size_t n = both.world.size();
  AppendSceneGeometry(both, std::move(up));
  CHECK_EQ(both.world.size(), 2 * n);
}

// Core engine rules: collision, ray tests, stepping, noise, file I/O,
// image loading.
TEST(EngineCollisionSlidesAlongWalls) {
  Level l;
  CHECK(l.Parse({"#####", "#...#", "#...#", "#####"}));
  Vec2 p{.x = 1.5, .y = 1.5};
  MoveWithCollision(l, p, -1.0, 0.5, 0.22);
  CHECK(p.x == 1.5);
  CHECK(std::fabs(p.y - 2.0) < 1e-9);
}

TEST(EngineRayHitsSphere) {
  const Vec3 eye{.x = 0, .y = 0, .z = 0}, ahead{.x = 1, .y = 0, .z = 0};
  CHECK(RayHitsSphere(eye, ahead, {3, 0, 0}, 0.5, 10) == 3.0);
  CHECK(RayHitsSphere(eye, ahead, {3, 0.4, 0}, 0.5, 10) > 0);
  CHECK(RayHitsSphere(eye, ahead, {3, 0.6, 0}, 0.5, 10) < 0);
  CHECK(RayHitsSphere(eye, ahead, {-3, 0, 0}, 0.5, 10) < 0);
  CHECK(RayHitsSphere(eye, ahead, {30, 0, 0}, 0.5, 10) < 0);
}

TEST(EngineWalkStep) {
  const Vec2 fwd = WalkStep(0, 1, 0, 2.0);
  CHECK(std::abs(fwd.x - 2.0) < 1e-9 && std::abs(fwd.y) < 1e-9);
  const Vec2 right = WalkStep(0, 0, 1, 1.0);
  CHECK(std::abs(right.x) < 1e-9 && std::abs(right.y - 1.0) < 1e-9);
  const Vec2 diag = WalkStep(0, 1, 1, 1.0);
  CHECK(std::hypot(diag.x, diag.y) < 1.001);
}

TEST(EngineNoiseIsDeterministic) {
  CHECK(Hnoise(3, 4, 7) == Hnoise(3, 4, 7));
  CHECK(Hnoise(3, 4, 7) >= 0.0 && Hnoise(3, 4, 7) < 1.0);
  CHECK(Grain(1, 2, 3, 10) >= -10 && Grain(1, 2, 3, 10) <= 10);
}

TEST(EngineFileIoRoundTrip) {
  const std::string path =
      (std::filesystem::temp_directory_path() / "mythos_fileio_test.txt")
          .string();
  CHECK(WriteFileAtomic(path, "hello"));
  CHECK(ReadTextFile(path) == std::string("hello"));
  CHECK(WriteFileAtomic(path, "", true));
  CHECK(ReadTextFile(path) == std::string());
}

TEST(EngineLoadImageDecodesBmpAndOverridesTextures) {
  const unsigned char bmp[] = {
      'B', 'M', 62, 0, 0, 0, 0, 0,  0, 0, 54, 0, 0, 0, 40,  0,   0, 0, 2, 0, 0,
      0,   1,   0,  0, 0, 1, 0, 24, 0, 0, 0,  0, 0, 8, 0,   0,   0, 0, 0, 0, 0,
      0,   0,   0,  0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0, 255, 255, 0, 0, 0, 0};
  const auto dir = std::filesystem::temp_directory_path() / "mythos_image_test";
  std::filesystem::create_directories(dir);
  {
    std::ofstream f(dir / "brick.bmp", std::ios::binary);
    f.write(reinterpret_cast<const char*>(bmp), sizeof bmp);
  }
  Surface s;
  CHECK(LoadImage((dir / "brick.bmp").string(), s));
  CHECK(s.w == 2 && s.h == 1);
  CHECK_EQ(s.At(0, 0), Rgba(255, 0, 0));
  CHECK_EQ(s.At(1, 0), Rgba(0, 0, 255));
  CHECK(!LoadImage((dir / "none.png").string(), s));
  TextureLibrary lib;
  lib.Add("brick", Surface(4, 4, 0xFF00FF00));
  CHECK_EQ(lib.LoadOverrides(dir.string()), 1);
  CHECK(lib.Get("brick").w == 2);
  std::filesystem::remove_all(dir);
}

TEST(EngineJsonReaderReportsFirstError) {
  json::Value root;
  CHECK(json::Parse("{\"a\": {\"n\": 3, \"s\": \"x\", \"c\": [1, 2]}}", root));
  json::Reader r(root, "t.json");
  CHECK_EQ(r.Integer("a.n"), 3);
  CHECK(r.String("a.s") == std::string("x"));
  int x = 0, y = 0;
  r.Cell("a.c", x, y);
  CHECK(x == 1 && y == 2 && r.Ok());
  r.Number("a.missing");
  r.Number("a.also");
  CHECK(!r.Ok());
  CHECK(r.Error().find("a.missing") != std::string::npos);
}

TEST(EngineUiAlphaAndHit) {
  CHECK_EQ(WithAlpha(0xFF123456, 0.5), 0x7F123456u);
  Surface s(4, 4);
  FillRect(s, -2, -2, 2, 2, 0xFFFFFFFF);
  CHECK_EQ(s.At(1, 1), 0xFFFFFFFFu);
  CHECK_EQ(s.At(2, 2), 0u);
}

// Player, walker, interaction verbs and their effects on the progress.
TEST(RuntimeWalkerStepsOncePerStride) {
  Level l;
  l.Parse({"#####", "#...#", "#####"});
  Player p;
  p.pos = {.x = 1.5, .y = 1.5};
  p.angle = 0;
  PlayerParams params;
  params.walk_speed = 1.0;
  params.run_speed = 2.0;
  params.stride_length = 0.5;
  MoveInput in;
  in.forward = true;
  PlayerWalker w;
  int steps = 0;
  for (int i = 0; i < 6; ++i) {
    if (w.Update(l, p, 0.1, in, params)) ++steps;
  }
  CHECK_EQ(steps, 1);
  CHECK(p.pos.x > 2.0 && p.pos.x < 2.2);
  const MoveInput idle;
  CHECK(!w.Update(l, p, 0.1, idle, params));
}

TEST(RuntimeWalkerSpeedFollowsTheStickAndKeysOverrideIt) {
  Level l;
  l.Parse({"#########", "#.......#", "#########"});
  PlayerParams params;
  params.walk_speed = 1.0;
  params.run_speed = 2.0;
  params.stride_length = 10.0;
  const auto walked = [&](double forward, double side, bool key) {
    Player p;
    p.pos = {.x = 1.5, .y = 1.5};
    p.angle = 0;
    MoveInput in;
    in.analog_forward = forward;
    in.analog_side = side;
    in.forward = key;
    PlayerWalker w;
    w.Update(l, p, 0.5, in, params);
    return p.pos.x - 1.5;
  };
  CHECK(std::fabs(walked(1.0, 0.0, false) - 0.5) < 1e-9);   // full push
  CHECK(std::fabs(walked(0.5, 0.0, false) - 0.25) < 1e-9);  // half push
  CHECK(std::fabs(walked(3.0, 0.0, false) - 0.5) < 1e-9);   // never faster
  CHECK_EQ(walked(0.0, 0.0, false), 0.0);                   // no push, no move
  CHECK(std::fabs(walked(0.2, 0.0, true) - 0.5) < 1e-9);    // a key wins
}

TEST(RuntimeLookClampsPitch) {
  Player p;
  PlayerParams params;
  params.mouse_sensitivity = 0.01;
  params.max_pitch = 1.0;
  MoveInput in;
  in.mouse_dy = -1000;
  ApplyLook(p, in, params);
  CHECK_EQ(p.pitch, 1.0);
}

TEST(RuntimeFindAimedPicksNearestVisibleEnabled) {
  Level l;
  l.Parse({"#######", "#.....#", "#######"});
  Player p;
  p.pos = {.x = 1.5, .y = 1.5};
  p.angle = 0;
  const std::vector<AimCandidate> c = {{.at = {.x = 4.5, .y = 1.5},
                                        .height = 1.0,
                                        .radius = 0.3,
                                        .enabled = true},
                                       {.at = {.x = 3.0, .y = 1.5},
                                        .height = 1.0,
                                        .radius = 0.3,
                                        .enabled = true},
                                       {.at = {.x = 2.5, .y = 1.5},
                                        .height = 1.0,
                                        .radius = 0.3,
                                        .enabled = false}};
  CHECK_EQ(FindAimed(l, p, 1.0, 5.0, c), 1);
  CHECK_EQ(FindAimed(l, p, 1.0, 1.0, c), -1);
  p.angle = kPi;
  CHECK_EQ(FindAimed(l, p, 1.0, 5.0, c), -1);
}

TEST(RuntimeVerbs) {
  Verb v = Verb::kTake;
  CHECK(ParseVerb("read", v) && v == Verb::kRead);
  CHECK(ParseVerb("listen", v) && v == Verb::kListen);
  CHECK(!ParseVerb("lick", v) && v == Verb::kListen);
  Prop p;
  p.id = "thing";
  Interactor it;
  p.verb = Verb::kRead;
  CHECK(it.Use(p, false, "", 0.1, false, false, 1.0) == Use::kNone);
  CHECK(it.Use(p, false, "", 0.1, true, true, 1.0) == Use::kRead);
  p.verb = Verb::kInspect;
  CHECK(it.Use(p, false, "", 0.1, true, true, 1.0) == Use::kInspect);
  CHECK(it.Use(p, true, "thing", 0.1, true, true, 1.0) == Use::kPutBack);
  CHECK(it.Use(p, true, "thing", 0.1, false, false, 1.0) == Use::kNone);
}

TEST(RuntimeTakeNeedsAFreeHand) {
  Prop p;
  p.id = "key";
  p.verb = Verb::kTake;
  Interactor it;
  CHECK(it.Use(p, false, "", 0.1, true, true, 1.0) == Use::kTake);
  CHECK(it.Use(p, false, "lamp", 0.1, true, true, 1.0) == Use::kHandsFull);
  CHECK(it.Use(p, false, "", 0.1, false, false, 1.0) == Use::kNone);
}

TEST(RuntimeListenHoldsThenWaitsForRelease) {
  Prop p;
  p.id = "radio";
  p.verb = Verb::kListen;
  Interactor it;
  CHECK(it.Use(p, false, "", 0.4, false, false, 1.0) == Use::kNone);
  CHECK(it.Use(p, false, "", 0.4, false, true, 1.0) == Use::kListening);
  CHECK(it.ListenTime() > 0.39);
  CHECK(it.Use(p, false, "", 0.4, false, true, 1.0) == Use::kListening);
  CHECK(it.Use(p, false, "", 0.4, false, true, 1.0) == Use::kHeard);
  CHECK(it.Use(p, false, "", 0.4, false, true, 1.0) == Use::kNone);
  it.Release(false);
  CHECK(it.Use(p, false, "", 0.4, false, true, 1.0) == Use::kListening);
  it.Idle();
  CHECK_EQ(it.ListenTime(), 0.0);
}

TEST(RuntimeEffectsWriteTheProgress) {
  Progress pr;
  Effects e;
  e.notebook = "notebook.x";
  e.clue = "clue.a";
  e.flags["seen"] = 1;
  CHECK(ApplyEffects(pr, e));
  CHECK(!ApplyEffects(pr, e));
  CHECK_EQ(pr.notebook.size(), 1u);
  CHECK_EQ(pr.clues.size(), 1u);
  CHECK(pr.Flag("seen") && !pr.Flag("nope"));
}

TEST(RuntimePropOverrideAndPlacement) {
  Prop p;
  p.id = "mug";
  p.at = {.x = 4.75, .y = 2.25};
  p.lift = 0.8;
  PropOverride o;
  o.model = "mug2";
  o.yaw_deg = 90;
  o.ApplyTo(p);
  CHECK(p.model == "mug2");
  CHECK_EQ(p.yaw_deg, 90.0);
  CHECK_EQ(p.lift, 0.8);
  const Placement pl = PlacementOf(p);
  CHECK(pl.x == 4 && pl.y == 2 && !pl.solid && pl.id == "mug");
  CHECK(pl.off_x > 0.24 && pl.off_x < 0.26 && pl.off_z < -0.24 &&
        pl.off_z > -0.26);
}

// Menus and the settings page: keys, clicks, persistence and new rows.
TEST(RuntimeMenuStepWrapsSkipsAndActivates) {
  int index = 0;
  UiInput in;
  const auto usable = [](int i) { return i != 1; };
  in.down_pressed = true;
  CHECK_EQ(MenuStep(index, 4, in, false, usable), -1);
  CHECK_EQ(index, 2);
  index = 3;
  CHECK_EQ(MenuStep(index, 4, in, false, usable), -1);
  CHECK_EQ(index, 0);
  in = UiInput{};
  in.up_pressed = true;
  MenuStep(index, 4, in, false, usable);
  CHECK_EQ(index, 3);
  in = UiInput{};
  CHECK_EQ(MenuStep(index, 4, in, true, usable), 3);
  in.hover_item = 1;
  MenuStep(index, 4, in, false, usable);
  CHECK_EQ(index, 3);
  in.hover_item = 2;
  MenuStep(index, 4, in, false, usable);
  CHECK_EQ(index, 2);
  in = UiInput{};
  in.click_item = 0;
  CHECK_EQ(MenuStep(index, 4, in, false, usable), 0);
  in.click_item = 1;
  CHECK_EQ(MenuStep(index, 4, in, false, usable), -1);
}

TEST(RuntimeSettingsPageKeysClicksAndBack) {
  GameSettings s;
  SettingsPage page = SettingsPage::kMain;
  int index = kSetGraphics;
  UiInput in;
  in.confirm_pressed = true;
  CHECK(!UpdateSettingsPage(s, page, index, in, false, 2));
  CHECK(page == SettingsPage::kGraphics);
  CHECK_EQ(index, kSetResolution);
  in = UiInput{};
  in.right_pressed = true;
  for (int i = 0; i < 6; ++i) UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.resolution, 2);
  in = UiInput{};
  in.click_item = kSetResolution;
  in.click_step = 1;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.resolution, 0);
  in = UiInput{};
  in.click_item = kSetMaster;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(page == SettingsPage::kGraphics && index == kSetResolution);
  index = kSetFullscreen;
  in = UiInput{};
  in.confirm_pressed = true;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(s.fullscreen);
  in = UiInput{};
  in.pause_pressed = true;
  CHECK(!UpdateSettingsPage(s, page, index, in, false, 2));
  CHECK(page == SettingsPage::kMain && index == kSetGraphics);

  in = UiInput{};
  in.click_item = kSetAudio;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(page == SettingsPage::kAudio);
  in = UiInput{};
  in.click_item = kSetMaster;
  in.click_value = 4;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.master_volume, 4);
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.master_volume, 3);
  in = UiInput{};
  in.click_item = kSetBack;
  CHECK(!UpdateSettingsPage(s, page, index, in, false, 2));
  CHECK(page == SettingsPage::kMain && index == kSetAudio);
  index = kSetLanguage;
  in = UiInput{};
  in.confirm_pressed = true;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(s.language == Lang::kGreek);
  index = kSetBack;
  CHECK(UpdateSettingsPage(s, page, index, in, false, 2));
  in = UiInput{};
  in.pause_pressed = true;
  CHECK(UpdateSettingsPage(s, page, index, in, false, 2));
  SetLanguage(Lang::kEnglish);
}

TEST(RuntimeSettingsRoundTripThroughKeyValues) {
  GameSettings a;
  a.resolution = ResolutionIndex(1920, 1080);
  a.render_scale = 5;
  a.film_grain = false;
  a.anti_alias = 4;
  a.shadows = 5;
  a.materials = 1;
  a.motion_blur = 4;
  a.camera_effect = true;
  a.visual_fx = false;
  a.vsync = false;
  a.fps_cap = 4;
  a.fullscreen = true;
  a.master_volume = 3;
  a.language = Lang::kGreek;
  GameSettings b;
  ApplyKeyValues(b, ToKeyValues(a));
  CHECK_EQ(b.resolution, a.resolution);
  CHECK_EQ(b.render_scale, 5);
  CHECK(!b.film_grain && b.fullscreen);
  CHECK_EQ(b.anti_alias, 4);
  CHECK_EQ(b.shadows, 5);
  CHECK_EQ(b.materials, 1);
  CHECK_EQ(GameSettings{}.materials, 2);
  CHECK(b.motion_blur == 4 && b.camera_effect && !b.visual_fx && !b.vsync);
  {
    GameSettings old;
    ApplyKeyValues(old, {{"cameraEffect", "0"}});
    CHECK(!old.visual_fx && !old.camera_effect);
    GameSettings old_on;
    ApplyKeyValues(old_on, {{"cameraEffect", "1"}});
    CHECK(old_on.visual_fx && !old_on.camera_effect);
  }
  CHECK_EQ(b.fps_cap, 4);
  CHECK_EQ(b.master_volume, 3);
  CHECK(b.language == Lang::kGreek);
  GameSettings c;
  ApplyKeyValues(c, {{"masterVolume", "99"},
                     {"renderScale", "-4"},
                     {"nonsense", "1"},
                     {"resolution", "bad"}});
  CHECK_EQ(c.master_volume, 10);
  CHECK_EQ(c.render_scale, 0);
  CHECK_EQ(c.resolution, 0);
  GameSettings d;
  ApplyKeyValues(d, {{"antiAliasing", "99"},
                     {"fpsCap", "77"},
                     {"shadowMaps", "9"},
                     {"motionBlurScale", "33"},
                     {"materials", "7"}});
  CHECK_EQ(d.anti_alias, kAntiAliasCount - 1);
  CHECK_EQ(d.fps_cap, 0);
  CHECK_EQ(d.shadows, kShadowLevelCount - 1);
  CHECK_EQ(d.materials, kMaterialLevelCount - 1);
  CHECK_EQ(d.motion_blur, 0);
}

TEST(RuntimeSettingsPageNewRows) {
  GameSettings s;
  SettingsPage page = SettingsPage::kGraphics;
  int index = kSetVsync;
  UiInput in;
  in.confirm_pressed = true;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(!s.vsync);
  index = kSetFpsCap;
  in = UiInput{};
  in.right_pressed = true;
  for (int i = 0; i < 20; ++i) UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.fps_cap, kFpsCapCount - 1);
  index = kSetShadows;
  in = UiInput{};
  in.left_pressed = true;
  for (int i = 0; i < 9; ++i) UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.shadows, 0);
  index = kSetMaterials;
  in = UiInput{};
  in.left_pressed = true;
  for (int i = 0; i < 5; ++i) UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.materials, 0);
  in.left_pressed = false;
  in.right_pressed = true;
  for (int i = 0; i < 5; ++i) UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.materials, kMaterialLevelCount - 1);
  index = kSetMotionBlur;
  in = UiInput{};
  in.right_pressed = true;
  for (int i = 0; i < 3; ++i) UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.motion_blur, 3);
  index = kSetRenderScale;
  for (int i = 0; i < 20; ++i) UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.render_scale, kRenderScaleCount - 1);
  index = kSetAntiAlias;
  for (int i = 0; i < 9; ++i) UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.anti_alias, kAntiAliasCount - 1);
  index = kSetCameraEffect;
  in = UiInput{};
  in.confirm_pressed = true;
  CHECK(!s.camera_effect);
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(s.camera_effect && s.visual_fx);
  index = kSetVisualFx;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(!s.visual_fx && s.camera_effect);
  {
    const auto& rows = SettingsPageItems(SettingsPage::kGraphics);
    const auto vfx = std::ranges::find(rows, kSetVisualFx);
    CHECK(vfx != rows.end() && *(vfx + 1) == kSetCameraEffect);
  }
  for (const SettingsPage p :
       {SettingsPage::kMain, SettingsPage::kGraphics, SettingsPage::kAudio}) {
    CHECK_EQ(SettingsPageItems(p).back(), kSetBack);
  }
  page = SettingsPage::kAudio;
  index = kSetMaster;
  in = UiInput{};
  in.up_pressed = true;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(index, kSetBack);
}

// The built-in English and Greek strings are the fallback; a game's own
// translation takes precedence.
TEST(RuntimeEngineStringsCoverBothLanguagesAndYieldToTheGame) {
  const TranslationTable& en = EngineStrings(Lang::kEnglish);
  const TranslationTable& el = EngineStrings(Lang::kGreek);
  CHECK_EQ(en.size(), el.size());
  for (const auto& [key, text] : en) CHECK(el.count(key) == 1);
  SetLanguage(Lang::kEnglish);
  SetTranslations(Lang::kEnglish, nullptr);
  CHECK(Tr("settings.vsync") == "VSYNC");
  CHECK(Tr("common.on") == "ON");
  const TranslationTable game = {{"settings.vsync", "SYNC"}};
  SetTranslations(Lang::kEnglish, &game);
  CHECK(Tr("settings.vsync") == "SYNC");
  CHECK(Tr("settings.title") == "SETTINGS");
  SetTranslations(Lang::kEnglish, nullptr);
}

namespace {
// Parses text that the test expects to be valid JSON.
json::Value ParseJson(const std::string& text) {
  json::Value v;
  std::string err;
  CHECK(json::Parse(text, v, &err));
  return v;
}
}

// Achievements, player params, sound bank, translations and credits load
// from their documented JSON formats.
TEST(RuntimeDataLoadersReadTheDocumentedFormats) {
  std::string err;
  {
    const json::Value v = ParseJson(
        R"({"achievements":{"_c":"x","a":{"event":"loops","count":3}}})");
    std::vector<AchievementDef> defs;
    CHECK(LoadAchievementDefs(v, "f", defs, &err));
    CHECK_EQ(defs.size(), 1u);
    CHECK(defs[0].id == "a" && defs[0].count == 3 &&
          defs[0].name == "achievements.a.name");
    const json::Value bad =
        ParseJson(R"({"achievements":{"a":{"event":"loops","count":0}}})");
    std::vector<AchievementDef> none;
    CHECK(!LoadAchievementDefs(bad, "f", none, &err));
  }
  {
    const json::Value v = ParseJson(
        R"({"player":{"radius":0.2,"walkSpeed":2,"runSpeed":3,"turnSpeed":2.5,
            "strideLength":0.8,"mouseSensitivity":0.002,"maxPitch":1.1}})");
    json::Reader r(v, "f");
    PlayerParams p;
    CHECK(LoadPlayerParams(r, "player", p));
    CHECK(p.radius == 0.2 && p.run_speed == 3 && p.max_pitch == 1.1);
    const json::Value missing = ParseJson(R"({"player":{"radius":0.2}})");
    json::Reader r2(missing, "f");
    PlayerParams q;
    CHECK(!LoadPlayerParams(r2, "player", q));
    CHECK(r2.Error().find("player.walkSpeed") != std::string::npos);
  }
  {
    const json::Value v = ParseJson(
        R"({"events":{"Step":{"clips":["a","b"],"gain":0.5,"jitter":true}},
            "ambience":{"house":{"clip":"h","gain":0.3}},
            "voice":{"dir":"voices","gain":1,"duck":0.4,"radioFalloff":0.1}})");
    SoundBank bank;
    CHECK(LoadSoundBank(v, "f", bank, {"Step"}, {"house"}, &err));
    CHECK(bank.events.at("Step").jitter &&
          bank.events.at("Step").clips.size() == 2);
    CHECK_EQ(bank.AllClips().size(), 3u);
    SoundBank other;
    CHECK(!LoadSoundBank(v, "f", other, {"Missing"}, {"house"}, &err));
    CHECK(!LoadSoundBank(v, "f", other, {"Step"}, {"car"}, &err));
  }
  {
    const json::Value v = ParseJson(
        R"({"language":"en","name":"ENGLISH","strings":{"hud":{"a":"A"},"b":"B"}})");
    Translation t;
    CHECK(LoadTranslation(v, "f", t, &err));
    CHECK(t.strings.at("hud.a") == "A" && t.strings.at("b") == "B");
  }
  {
    const json::Value v = ParseJson(R"({"sections":[{"title":"t","entries":[
            {"name":"n","by":"b","license":"CC0","what":""}]}]})");
    std::vector<CreditSection> secs;
    CHECK(LoadCredits(v, "f", secs, &err));
    CHECK(secs.size() == 1 && secs[0].entries[0].license == "CC0");
  }
}

namespace {

// Multiplies the column-major matrix m by the point (x, y, z, 1).
void Transform(const float* m, float x, float y, float z, float out[4]) {
  for (int r = 0; r < 4; ++r) {
    out[r] = m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r];
  }
}
}

// Matrices, frustum and cube-face views.
TEST(RuntimeMat4ViewProjectionAndFrustum) {
  const float eye[3] = {2, 1.5f, 3}, center[3] = {2, 1.5f, -2},
              up[3] = {0, 1, 0};
  float view[16], proj[16], vp[16], out[4];
  Mat4LookAt(eye, center, up, view);
  Transform(view, 2, 1.5f, -2, out);
  CHECK(std::abs(out[0]) < 1e-5f && std::abs(out[1]) < 1e-5f &&
        std::abs(out[2] + 5.0f) < 1e-5f);
  Mat4Perspective(1.2f, 1.6f, 0.05f, 40.0f, proj);
  Mat4Multiply(proj, view, vp);
  Transform(vp, 2, 1.5f, -2, out);
  CHECK(std::abs(out[0] / out[3]) < 1e-5f && std::abs(out[1] / out[3]) < 1e-5f);
  CHECK(out[2] / out[3] > -1.0f && out[2] / out[3] < 1.0f);
  Frustum planes;
  FrustumPlanes(vp, planes);
  const float in_min[3] = {1.9f, 1.4f, -2.1f}, in_max[3] = {2.1f, 1.6f, -1.9f};
  CHECK(BoxInFrustum(planes, in_min, in_max));
  const float behind_min[3] = {1.9f, 1.4f, 5.0f},
              behind_max[3] = {2.1f, 1.6f, 6.0f};
  CHECK(!BoxInFrustum(planes, behind_min, behind_max));
  CHECK(!BoxInFrustum(planes, in_min, in_max, -100.0f));
  const float far_min[3] = {1.9f, 1.4f, -90.0f},
              far_max[3] = {2.1f, 1.6f, -80.0f};
  CHECK(!BoxInFrustum(planes, far_min, far_max));
}

TEST(RuntimeCubeFacesLookDownTheirAxis) {
  const float eye[3] = {4, 1.2f, -3};
  const float dirs[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                            {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  for (int f = 0; f < 6; ++f) {
    float view[16], out[4];
    CubeFaceView(f, eye, view);
    Transform(view, eye[0] + 3 * dirs[f][0], eye[1] + 3 * dirs[f][1],
              eye[2] + 3 * dirs[f][2], out);
    CHECK(std::abs(out[0]) < 1e-5f && std::abs(out[1]) < 1e-5f &&
          std::abs(out[2] + 3.0f) < 1e-5f);
  }

  float view[16], out[4];
  CubeFaceView(0, eye, view);
  Transform(view, eye[0] + 1, eye[1], eye[2] - 1, out);
  CHECK(out[0] > 0.5f);
  CubeFaceView(4, eye, view);
  Transform(view, eye[0] + 1, eye[1], eye[2] + 1, out);
  CHECK(out[0] > 0.5f);
}

// Audio arithmetic, cue selection and light flicker.
TEST(RuntimePositionalGainPansFadesAndHushes) {
  const Vec2 me{.x = 5, .y = 5};
  const StereoGain ahead = PositionalGain(me, 0.0, {.x = 8, .y = 5}, 0);
  CHECK(std::abs(ahead.left - ahead.right) < 1e-4f);
  const StereoGain on_right = PositionalGain(me, 0.0, {.x = 5, .y = 8}, 0);
  const StereoGain on_left = PositionalGain(me, 0.0, {.x = 5, .y = 2}, 0);
  CHECK(on_right.right > on_right.left);
  CHECK(on_left.left > on_left.right);
  CHECK(on_right.left > 0 && on_left.right > 0);
  const StereoGain near = PositionalGain(me, 0.0, {.x = 6, .y = 5}, 0);
  const StereoGain far = PositionalGain(me, 0.0, {.x = 15, .y = 5}, 0);
  CHECK(near.left > far.left);
  const StereoGain hushed = PositionalGain(me, 0.0, {.x = 6, .y = 5}, 2.0);
  CHECK(hushed.left == 0.0f && hushed.right == 0.0f);
  CHECK(PositionalGain(me, 0.0, {8, 5}, 2.0).left > 0.0f);
  CHECK(PositionalGain(me, 0.0, {6, 5}, 0, true).left == 0.0f);
  const StereoGain carries =
      PositionalGain(me, 0.0, {.x = 15, .y = 5}, 0, false, 1.0, 0.1);
  CHECK(carries.left > far.left * 5.0f);
  CHECK(PositionalGain(me, 0.0, {6, 5}, 0, false, 1.0, 0.1).left >
        carries.left);
  CHECK(PositionalGain(me, 0.0, {15, 5}, 0).left ==
        PositionalGain(me, 0.0, {15, 5}, 0, false, 0.75, 0.5).left);
}

TEST(RuntimeCuePickKeepsTheDrawOrder) {
  SoundCue one;
  one.clips = {"a"};
  one.pitch = 1.5f;
  SoundCue many;
  many.clips = {"a", "b", "c"};
  many.jitter = true;
  Rng r1(7), r2(7);
  const CuePick p1 = PickCue(one, r1);
  CHECK_EQ(p1.clip, 0u);
  CHECK_EQ(p1.pitch, 1.5f);
  const float jitter_draw = r2.Next();
  CHECK_EQ(r1.Next(), r2.Next());
  (void)jitter_draw;
  Rng r3(7);
  for (int i = 0; i < 200; ++i) {
    const CuePick p = PickCue(many, r3);
    CHECK(p.clip < 3);
    CHECK(p.pitch >= 0.92f && p.pitch <= 1.08f);
  }
}

TEST(RuntimeFlickerMatchesItsFormulaAndStaysInRange) {
  CHECK_EQ(LampGain(12.3, 0, 0.0), 1.0);
  int dark = 0;
  for (int slot = 0; slot < 2000; ++slot) {
    const double t = slot / 16.0 + 0.01;
    const uint32_t h =
        MixSlot(static_cast<uint32_t>(t * 16.0) * 2654435761u + 2u * 97u);
    const bool drop = (h & 0xFFFF) / 65535.0 < 0.8 * 0.5;
    const double g = LampGain(t, 2, 0.8);
    CHECK(g > 0.0 && g < 1.01);
    if (drop) {
      CHECK(g < 0.14);
      ++dark;
    }
  }
  CHECK(dark > 500 && dark < 1100);
  int lit = 0;
  for (int i = 0; i < 2000; ++i) lit += TorchLit(i / 14.0 + 0.01, 0.5) ? 1 : 0;
  CHECK(lit > 1300 && lit < 1700);
  CHECK(TorchLit(3.0, 0.0));
}

namespace {
// A legend with walls and room doors (`N` closed, `n` open).
TileSetPtr DoorTiles() {
  json::Value root;
  std::string err;
  const std::string legend =
      R"({"defaultWall": "brick", "defaultFloor": "wood", "tiles": {
        "#": {"solid": true}, "N": {"solid": true, "door": "closed"}, "n": {"door": "open"}}})";
  CHECK(json::Parse(legend, root, &err));
  auto set = std::make_shared<TileSet>();
  CHECK(ParseTileSet(root, *set, &err));
  return set;
}

// Two rooms, each 3 cells wide, joined by one door in the wall at x = 4.
Level DoorLevel() {
  Level l;
  l.SetTiles(DoorTiles());
  l.Parse({"#########", "#...#...#", "#...N...#", "#...#...#", "#########"});
  return l;
}
}

// Room doors swing into the smaller region and never shut on the player.
TEST(RuntimeRoomDoorSwingsIntoTheSmallerSpaceAndCloses) {
  Level l = DoorLevel();
  RoomDoors doors;
  doors.Scan(l);
  std::vector<DoorState> st;
  doors.AppendStates(st);
  CHECK_EQ(st.size(), 1u);
  CHECK(st[0].passage_along_x);
  CHECK_EQ(doors.Use(4, 2), 1);
  CHECK_EQ(doors.Use(9, 9), 0);
  const Vec2 far{.x = 1.5, .y = 1.5};
  doors.Update(l, 1.0, far, 0.25, 0.5, 0.5);
  CHECK_EQ(l.At(4, 2), 'n');
  CHECK_EQ(doors.Use(4, 2), -1);
  CHECK_EQ(doors.Update(l, 1.0, far, 0.25, 0.5, 0.5), 1);
  CHECK_EQ(l.At(4, 2), 'N');
}

TEST(RuntimeRoomDoorNeverShutsOnThePlayer) {
  Level l = DoorLevel();
  RoomDoors doors;
  doors.Scan(l);
  doors.Use(4, 2);
  const Vec2 inside{.x = 4.5, .y = 2.5};
  doors.Update(l, 1.0, inside, 0.25, 0.5, 0.5);
  doors.Use(4, 2);
  for (int i = 0; i < 20; ++i) doors.Update(l, 0.1, inside, 0.25, 0.5, 0.5);
  CHECK_EQ(l.At(4, 2), 'n');
  const Vec2 out{.x = 1.5, .y = 2.5};
  for (int i = 0; i < 20; ++i) doors.Update(l, 0.1, out, 0.25, 0.5, 0.5);
  CHECK_EQ(l.At(4, 2), 'N');
}

// Normal, roughness, metal and occlusion maps (PBR companions).
TEST(NormalMapIsFlatForAFlatTextureAndTiltsWithASlope) {
  const Surface flat(8, 8, Rgba(120, 120, 120));
  const Surface nf = MakeNormalMap(flat, 3.0f);
  CHECK_EQ(nf.w, 8);
  for (const uint32_t p : nf.px) CHECK_EQ(p, Rgba(128, 128, 255));

  Surface ramp(8, 8);
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) ramp.At(x, y) = Rgba(x * 30, x * 30, x * 30);
  }
  Surface nr = MakeNormalMap(ramp, 3.0f);
  const uint32_t p = nr.At(4, 4);
  CHECK(((p >> 16) & 0xFF) < 120);
  CHECK(std::abs(static_cast<int>((p >> 8) & 0xFF) - 128) <= 1);
  CHECK((p & 0xFF) > 128);
  CHECK_EQ(MakeNormalMap(ramp, 0.0f).At(4, 4), Rgba(128, 128, 255));

  Surface down(8, 8);
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) down.At(x, y) = Rgba(y * 30, y * 30, y * 30);
  }
  CHECK(((MakeNormalMap(down, 3.0f).At(4, 4) >> 8) & 0xFF) > 136);
  CHECK(MakeNormalMap(Surface(), 1.0f).Empty());
}

TEST(SurfaceMapUsesAuthoredNormalAndRoughnessOrFallsBack) {
  const Surface albedo(4, 4, Rgba(100, 100, 100));
  Surface plain = MakeSurfaceMap(albedo, nullptr, nullptr, 3.0f);
  CHECK_EQ(plain.At(1, 1), Rgba(128, 128, 255, 0));
  const Surface nrm(4, 4, Rgba(200, 50, 220));
  const Surface rough(4, 4, Rgba(64, 64, 64));
  Surface both = MakeSurfaceMap(albedo, &nrm, &rough, 3.0f);
  CHECK_EQ(both.At(2, 3), Rgba(200, 50, 220, 191));
  CHECK_EQ(MakeSurfaceMap(albedo, nullptr, &rough, 3.0f).At(0, 0),
           Rgba(128, 128, 255, 191));
  const Surface small(2, 2, Rgba(0, 0, 0));
  Surface sized = MakeSurfaceMap(albedo, nullptr, &small, 3.0f);
  CHECK_EQ(sized.w, 4);
  CHECK_EQ(sized.At(3, 3), Rgba(128, 128, 255, 255));
  CHECK(MakeSurfaceMap(Surface(), &nrm, &rough, 1.0f).Empty());
  CHECK(IsPbrCompanion("m_sofa_0_nrm"));
  CHECK(IsPbrCompanion("m_sofa_0_rough"));
  CHECK(!IsPbrCompanion("m_sofa_0"));
  CHECK(!IsPbrCompanion("_nrm"));
  CHECK(IsPbrCompanion("m_sofa_0_metal"));
  CHECK(IsPbrCompanion("m_sofa_0_ao"));
}

TEST(MaterialMapPacksMetalAndOcclusionWithOpenDefaults) {
  const Surface albedo(4, 4, Rgba(100, 100, 100));
  CHECK(MakeMaterialMap(albedo, nullptr, nullptr).Empty());
  CHECK(MakeMaterialMap(Surface(), &albedo, &albedo).Empty());
  const Surface metal(4, 4, Rgba(220, 220, 220));
  const Surface ao(4, 4, Rgba(90, 90, 90));
  CHECK_EQ(MakeMaterialMap(albedo, &metal, &ao).At(1, 2), Rgba(220, 90, 255));
  CHECK_EQ(MakeMaterialMap(albedo, &metal, nullptr).At(0, 0),
           Rgba(220, 255, 255));
  CHECK_EQ(MakeMaterialMap(albedo, nullptr, &ao).At(3, 3), Rgba(0, 90, 255));
  const Surface small(2, 2, Rgba(255, 255, 255));
  Surface sized = MakeMaterialMap(albedo, &small, nullptr);
  CHECK_EQ(sized.w, 4);
  CHECK_EQ(sized.At(2, 2), Rgba(255, 255, 255));
}

TEST(EngineCommonMaterialsStandAlone) {
  TextureLibrary lib;
  AddCommonMaterials(lib);
  for (const char* n : {"brick", "wood", "tile", "carpet", "stone", "grass",
                        "door", "ceiling", "void", "poster", "cloth", "skin"}) {
    CHECK(lib.Has(n));
  }
  CHECK(lib.IsPlaceholder("poster"));
  CHECK(!lib.Has("clockface"));
}

// The asset pack: write, mount and read back, and detect tampering.
TEST(VfsPackRoundTripAndMount) {
  const uint64_t key = vfs::KeyFromString("secret");
  std::string big(5000, 'x');
  for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>(i * 7);
  const std::string pack = vfs::BuildPack({{"data/a.json", "{\"a\": 1}"},
                                           {"models/b.obj", big},
                                           {"models/c.obj", ""},
                                           {"top.txt", "t"}},
                                          key);
  CHECK(pack.find("a.json") == std::string::npos);
  CHECK(pack.find("{\"a\"") == std::string::npos);

  vfs::UnmountAll();
  std::string err;
  CHECK(!vfs::MountMemory(pack, "pak://assets", vfs::KeyFromString("wrong"),
                          &err));
  CHECK(!vfs::MountMemory("junk", "pak://assets", key, &err));
  CHECK(vfs::MountMemory(pack, "pak://assets", key, &err));

  std::string out;
  CHECK(vfs::ReadFile("pak://assets/data/a.json", out));
  CHECK(out == std::string("{\"a\": 1}"));
  CHECK(vfs::ReadFile("pak://assets//models/b.obj", out));
  CHECK(out == big);
  CHECK(vfs::ReadFile("pak://assets/models/c.obj", out));
  CHECK(out.empty());
  CHECK(!vfs::ReadFile("pak://assets/data/missing.json", out));
  CHECK(vfs::Exists("pak://assets/top.txt"));
  CHECK(!vfs::Exists("pak://assets/nope"));
  CHECK(!vfs::Exists("pak://assetsx/top.txt"));

  const auto models = vfs::List("pak://assets/models");
  CHECK_EQ(models.size(), size_t(2));
  CHECK(models[0] == std::string("b.obj"));
  CHECK_EQ(vfs::List("pak://assets").size(), size_t(1));

  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "mythos_vfs_test.txt";
  {
    std::ofstream(tmp, std::ios::binary) << "disk";
  }
  CHECK(vfs::ReadFile(tmp.string(), out));
  CHECK(out == std::string("disk"));
  std::filesystem::remove(tmp);
  vfs::UnmountAll();
}

TEST(VfsDetectsTampering) {
  const uint64_t key = vfs::KeyFromString("k");
  std::string pack =
      vfs::BuildPack({{"a.txt", "hello world, a longer line"}}, key);
  pack.back() = static_cast<char>(pack.back() ^ 0x40);
  vfs::UnmountAll();
  CHECK(vfs::MountMemory(pack, "pak://t", key));
  std::string out;
  CHECK(!vfs::ReadFile("pak://t/a.txt", out));
  std::string cut = vfs::BuildPack({{"a.txt", "hello"}}, key);
  cut.resize(cut.size() - 2);
  CHECK(!vfs::MountMemory(cut, "pak://u", key));
  vfs::UnmountAll();
}

namespace {

namespace fs = std::filesystem;

// A temporary project folder with `assets/levels`, removed afterwards.
struct EditorFixture {
  fs::path root;
  EditorFixture() {
    static int n = 0;
    root = fs::temp_directory_path() /
           ("mythos_editor_test_" + std::to_string(++n));
    fs::remove_all(root);
    fs::create_directories(root / "assets" / "levels");
    fs::create_directories(root / "assets" / "data");
    Write("assets/data/tiles.json",
          "{\n  \"defaultWall\": \"brick\",\n  \"defaultFloor\": \"wood\",\n"
          "  \"tiles\": {\n    \"#\": {\"solid\": true},\n"
          "    \"U\": {\"openCeiling\": true},\n"
          "    \"v\": {\"openFloor\": true}\n  }\n}\n");
    Write("assets/levels/ground.txt",
          "[map]\n#####\n#.U.#\n#...#\n#####\n[furniture]\n1 2 table 0\n"
          "[decor]\n2 2 vase 90 0.25 -0.25 0.9\n");
    Write("assets/levels/up.txt", "[map]\n#####\n#.v.#\n#...#\n#####\n");
    Write("assets/data/things.json",
          "{\n  \"floors\": {\n    \"ground\": 0,\n    \"up\": 1\n  },\n"
          "  \"things\": [\n    {\n      \"at\": [\n        2.5,\n        1.5\n"
          "      ],\n      \"model\": \"lamp\",\n      \"lift\": 1.0\n    }\n"
          "  ]\n}\n");
  }
  ~EditorFixture() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
  void Write(const std::string& rel, const std::string& text) {
    std::ofstream(root / rel, std::ios::binary) << text;
  }
  std::string Read(const std::string& rel) {
    return ReadTextFile((root / rel).string());
  }
};

constexpr int kThing = editor::kFirstGameKind;

class FakeAdapter : public editor::Adapter {
 public:
  std::vector<std::string> DataFiles() const override { return {"things"}; }
  editor::LevelInfo Info(const editor::DataSet& d,
                         const std::string& level) const override {
    editor::LevelInfo info;
    info.stacked = true;
    info.group = "Floors";
    info.floor = d.at("things")["floors"].value(level, 0);
    return info;
  }
  std::vector<std::string> Contexts(const editor::DataSet&) const override {
    return {"day"};
  }
  std::vector<std::pair<int, std::string>> Stack(
      const editor::DataSet&, const editor::Where&) const override {
    return {{0, "ground"}, {1, "up"}, {2, "missing"}};
  }
  std::vector<editor::EntityRef> Entities(const editor::DataSet& d,
                                          const editor::Where&) const override {
    std::vector<editor::EntityRef> out;
    for (size_t i = 0; i < d.at("things")["things"].size(); ++i)
      out.push_back({kThing, static_cast<int>(i), ""});
    return out;
  }
  bool Describe(const editor::DataSet& d, const editor::Where&,
                const editor::EntityRef& r,
                editor::Visual* out) const override {
    const json::Value& t =
        d.at("things")["things"][static_cast<size_t>(r.index)];
    out->shape = editor::Shape::kModel;
    out->model = t["model"].get<std::string>();
    return true;
  }
  bool GetPose(const editor::DataSet& d, const editor::Where&,
               const editor::EntityRef& r, editor::Pose* out) const override {
    const json::Value& t =
        d.at("things")["things"][static_cast<size_t>(r.index)];
    *out = {t["at"][0].get<double>(), t["lift"].get<double>(),
            t["at"][1].get<double>(), 0, false};
    return true;
  }
  bool SetPose(editor::DataSet& d, const editor::Where&,
               const editor::EntityRef& r,
               const editor::Pose& p) const override {
    json::Value& t = d["things"]["things"][static_cast<size_t>(r.index)];
    t["at"] = json::Value::array({p.x, p.z});
    t["lift"] = p.y;
    return true;
  }
  std::string Label(const editor::DataSet&, const editor::Where&,
                    const editor::EntityRef& r) const override {
    return "thing " + std::to_string(r.index);
  }
};

}

// The level editor's headless rules: documents, undo, checks and adapters.
TEST(EditorFormatsNumbersLikeLevelFiles) {
  CHECK(editor::FormatNumber(0.1) == std::string("0.1"));
  CHECK(editor::FormatNumber(90) == std::string("90"));
  CHECK(editor::FormatNumber(-0.00001) == std::string("0"));
  CHECK(editor::FormatNumber(1.23456) == std::string("1.2346"));
}

TEST(EditorStampsWhatItSavesSoGeneratorsKeepIt) {
  editor::LevelDoc doc;
  std::string err;
  CHECK(editor::ParseLevelDoc("[map]\n#.\n.#\n[furniture]\n[decor]\n", &doc,
                              &err));
  CHECK(editor::SerializeLevelDoc(doc).substr(0, 400).find("map_editor") !=
        std::string::npos);
  CHECK(!editor::ParseLevelDoc("[map]\n#.\n.\n", &doc, &err));
}

TEST(EditorResizesALevelAndDropsWhatFallsOff) {
  editor::LevelDoc doc;
  std::string err;
  CHECK(editor::ParseLevelDoc(
      "[map]\n....\n....\n[furniture]\n3 1 table 0\n0 0 chair 0\n[decor]\n",
      &doc, &err));
  doc.Resize(2, 3, '#');
  CHECK_EQ(doc.Width(), 2);
  CHECK_EQ(doc.Height(), 3);
  CHECK(doc.At(0, 2) == '#');
  CHECK_EQ(doc.furniture.size(), size_t{1});
}

TEST(EditorEditsLevelsWithNoAdapter) {
  EditorFixture f;
  editor::Project p;
  std::string err;
  CHECK(p.Load(f.root.string(), &err));
  CHECK_EQ(p.LevelNames().size(), size_t{2});
  CHECK(p.data.empty());
  CHECK(p.Contexts().empty());
  const editor::Where w{"ground", ""};
  editor::Pose pose;
  const editor::EntityRef table{editor::kFurniture, 0, ""};
  CHECK(p.GetPose(w, table, &pose));
  pose.x = 3.8;
  pose.z = 1.2;
  CHECK(p.SetPose(w, table, pose));
  CHECK_EQ(p.Level("ground")->furniture[0].x, 3);
  CHECK_EQ(p.Level("ground")->furniture[0].y, 1);
  CHECK(std::abs(p.Level("ground")->furniture[0].off_x - 0.3) < 1e-9);
  const editor::EntityRef vase{editor::kDecor, 0, ""};
  CHECK(p.GetPose(w, vase, &pose));
  CHECK(std::abs(pose.x - 2.75) < 1e-9 && std::abs(pose.y - 0.9) < 1e-9);
  editor::Visual vis;
  CHECK(p.Describe(w, vase, &vis));
  CHECK(vis.model == std::string("vase"));
  CHECK(p.KindName(editor::kDecor) == std::string("Decor"));
  CHECK(p.IsDirty("ground"));
  CHECK(!p.IsDirty("up"));
  CHECK(p.Save(&err));
  CHECK(!p.Dirty());
  CHECK(f.Read("assets/levels/ground.txt").find("map_editor") !=
        std::string::npos);
  CHECK(f.Read("assets/levels/up.txt") ==
        "[map]\n#####\n#.v.#\n#...#\n#####\n");
  editor::Project again;
  CHECK(again.Load(f.root.string(), &err));
  CHECK_EQ(again.Level("ground")->furniture[0].x, 3);
  CHECK(p.CreateLevel("cellar_2", 6, 5));
  CHECK(p.Level("cellar_2")->At(0, 0) == '#' &&
        p.Level("cellar_2")->At(2, 2) == '.');
  CHECK(!p.CreateLevel("a/b", 6, 5));
  CHECK(!p.CreateLevel("ground", 6, 5));
}

TEST(EditorRejectsInvalidTriggersWithoutOverwritingLevel) {
  EditorFixture f;
  editor::Project p;
  std::string err;
  CHECK(p.Load(f.root.string(), &err));
  const std::string before = f.Read("assets/levels/ground.txt");
  Trigger invalid;
  invalid.id = "bad trigger";
  p.Level("ground")->triggers.push_back(invalid);
  p.Touch("ground");
  CHECK(!p.Save(&err));
  CHECK(p.IsDirty("ground"));
  CHECK(f.Read("assets/levels/ground.txt") == before);
}

TEST(EditorTriggerChangesSaveAndUndoRedo) {
  EditorFixture f;
  editor::Project p;
  std::string err;
  CHECK(p.Load(f.root.string(), &err));
  editor::History history;
  history.Reset(p);
  Trigger trigger;
  trigger.id = "entry";
  trigger.shape = TriggerShape::kCell;
  trigger.cell_x = 1;
  trigger.cell_z = 1;
  p.Level("ground")->triggers.push_back(trigger);
  p.Touch("ground");
  CHECK(history.Commit(p));
  p.Level("ground")->triggers[0].event = TriggerEvent::kUse;
  p.Touch("ground");
  CHECK(history.Commit(p));
  CHECK(history.Undo(&p));
  CHECK(p.Level("ground")->triggers[0].event == TriggerEvent::kEnter);
  CHECK(history.Redo(&p));
  CHECK(p.Level("ground")->triggers[0].event == TriggerEvent::kUse);
  CHECK(p.Save(&err));
  editor::Project loaded;
  CHECK(loaded.Load(f.root.string(), &err));
  CHECK(loaded.Level("ground")->triggers == p.Level("ground")->triggers);
  p.Level("ground")->triggers.clear();
  p.Touch("ground");
  CHECK(history.Commit(p));
  CHECK(history.Undo(&p));
  CHECK_EQ(p.Level("ground")->triggers.size(), size_t{1});
  CHECK(history.Redo(&p));
  CHECK(p.Level("ground")->triggers.empty());
}

TEST(EditorChecksFurnitureInWallsMissingModelsAndStairwells) {
  EditorFixture f;
  const FakeAdapter adapter;
  editor::Project p(&adapter);
  std::string err;
  CHECK(p.Load(f.root.string(), &err));
  const editor::Where w{"ground", "day"};
  const std::vector<std::string> models = {"table", "vase", "lamp"};
  CHECK(p.Check(w, models).empty());
  editor::LevelDoc* doc = p.Level("ground");
  doc->furniture.push_back({0, 0, "table", 0});
  doc->furniture.push_back({1, 1, "no_such_model", 0});
  bool wall = false, missing = false;
  for (const auto& pr : p.Check(w, models)) {
    wall |= pr.text.find("inside a wall") != std::string::npos;
    missing |= pr.text.find("no_such_model") != std::string::npos;
  }
  CHECK(wall);
  CHECK(missing);

  doc->furniture.clear();
  p.Level("up")->Set(2, 1, '.');
  bool well = false;
  for (const auto& pr : p.Check(w, models))
    well |= pr.text.find("no hole in the floor above") != std::string::npos;
  CHECK(well);
}

TEST(EditorStackListsTheFloorsThatExistWithTheOpenOneStandingIn) {
  EditorFixture f;
  const FakeAdapter adapter;
  editor::Project p(&adapter);
  std::string err;
  CHECK(p.Load(f.root.string(), &err));
  const auto stack = p.Stack("day", "up");
  CHECK_EQ(stack.size(), size_t{2});
  CHECK_EQ(stack[0].first, 0);
  CHECK(stack[1].second == std::string("up"));
  CHECK(p.Info("up").stacked);
  CHECK_EQ(p.Info("up").floor, 1);
}

TEST(EditorAdaptersThingsMoveSaveAndUndoLikeLevels) {
  EditorFixture f;
  const FakeAdapter adapter;
  editor::Project p(&adapter);
  std::string err;
  CHECK(p.Load(f.root.string(), &err));
  const editor::Where w{"ground", "day"};
  CHECK_EQ(p.Entities(w).size(), size_t{3});
  const editor::EntityRef thing{kThing, 0, ""};
  CHECK(p.KindName(kThing) == std::string("Things"));
  editor::Visual vis;
  CHECK(p.Describe(w, thing, &vis));
  CHECK(vis.model == std::string("lamp"));
  editor::History h;
  h.Reset(p);
  editor::Pose pose;
  CHECK(p.GetPose(w, thing, &pose));
  pose.x = 3.5;
  CHECK(p.SetPose(w, thing, pose));
  CHECK(p.Dirty());
  CHECK(h.Commit(p));
  CHECK(p.Save(&err));
  editor::Project again(&adapter);
  CHECK(again.Load(f.root.string(), &err));
  CHECK(again.data["things"]["things"][0]["at"][0].get<double>() == 3.5);
  const std::string text = f.Read("assets/data/things.json");
  CHECK(text == p.data["things"].dump(2) + "\n");
  CHECK(h.Undo(&p));
  CHECK(p.data["things"]["things"][0]["at"][0].get<double>() == 2.5);
  CHECK(p.Dirty());
  CHECK(h.Redo(&p));
  CHECK(p.data["things"]["things"][0]["at"][0].get<double>() == 3.5);
  CHECK(!p.Remove(w, thing));
  CHECK(p.Duplicate(w, thing).index < 0);
}

// The glitching FM radio is finite and repeatable for a given seed.
TEST(RadioFmGlitchesAndStaysFiniteAndRepeatsForASeed) {
  const int rate = 44100;
  mythos::RadioFm a(rate, 7), b(rate, 7), c(rate, 8);
  bool saw[5] = {false, false, false, false, false};
  double energy = 0;
  bool same = true, differs = false, finite = true;
  for (int i = 0; i < rate * 30; ++i) {
    const float in = 0.5f * std::sin(static_cast<float>(i) * 0.05f) *
                     std::sin(static_cast<float>(i) * 0.0007f);
    const float ya = a.Process(in), yb = b.Process(in), yc = c.Process(in);
    same = same && ya == yb;
    differs = differs || ya != yc;
    finite = finite && std::isfinite(ya) && std::fabs(ya) <= 2.0f;
    energy += ya * ya;
    saw[static_cast<int>(a.Current())] = true;
  }
  CHECK(finite);
  CHECK(same);
  CHECK(differs);
  CHECK(energy > 1.0);
  for (int k = 1; k < 5; ++k) CHECK(saw[k]);
}

// ---- tone mapping and colour grading ----

TEST(grade_default_is_the_engine_look) {
  const mythos::ColorGrade g;
  CHECK(g.PostIsIdentity());
  CHECK(g.tonemap == mythos::Tonemap::kAces);
  // ACES of 1.0 is (2.51 + 0.03) / (2.43 + 0.59 + 0.14), then gamma.
  const float aces = std::pow(2.54f / 3.16f, 1.0f / 2.2f);
  CHECK(std::fabs(mythos::TonemapChannel(mythos::Tonemap::kAces, 1.0f) - aces) <
        1e-5f);
  CHECK(mythos::TonemapChannel(mythos::Tonemap::kAces, 0.0f) < 0.1f);
}

TEST(grade_tonemaps_stay_in_range_and_rise) {
  for (int t = 0; t < mythos::kTonemapCount; ++t) {
    const auto mode = static_cast<mythos::Tonemap>(t);
    float prev = -1.0f;
    for (int step = 0; step <= 160; ++step) {
      const float x = static_cast<float>(step) * 0.25f;
      const float v = mythos::TonemapChannel(mode, x);
      CHECK(v >= 0.0f && v <= 1.0f);
      CHECK(v >= prev - 1e-6f);
      prev = v;
    }
    CHECK(mythos::TonemapChannel(mode, -3.0f) <= 1e-3f);
  }
  CHECK(std::fabs(mythos::TonemapChannel(mythos::Tonemap::kNone, 0.5f) -
                  std::pow(0.5f, 1.0f / 2.2f)) < 1e-5f);
}

TEST(grade_exposure_brightens) {
  mythos::ColorGrade g;
  const float lit[3] = {0.2f, 0.2f, 0.2f};
  const float base = mythos::ToDisplay(g, lit)[0];
  g.exposure = 2.0f;
  CHECK(mythos::ToDisplay(g, lit)[0] > base);
  g.exposure = 0.0f;
  CHECK(mythos::ToDisplay(g, lit)[0] < 0.01f);
}

TEST(grade_contrast_saturation_tint) {
  mythos::ColorGrade g;
  const float c[3] = {0.8f, 0.4f, 0.2f};
  const auto same = mythos::GradeDisplay(g, c);
  CHECK(std::fabs(same[0] - 0.8f) < 1e-5f && std::fabs(same[2] - 0.2f) < 1e-5f);

  g.saturation = 0.0f;
  const auto grey = mythos::GradeDisplay(g, c);
  CHECK(std::fabs(grey[0] - grey[1]) < 1e-5f &&
        std::fabs(grey[1] - grey[2]) < 1e-5f);

  g = mythos::ColorGrade();
  g.contrast = 0.0f;
  const auto flat = mythos::GradeDisplay(g, c);
  CHECK(std::fabs(flat[0] - 0.5f) < 1e-5f);

  g = mythos::ColorGrade();
  g.tint[0] = 0.5f;
  const auto tinted = mythos::GradeDisplay(g, c);
  CHECK(std::fabs(tinted[0] - 0.4f) < 1e-5f &&
        std::fabs(tinted[1] - 0.4f) < 1e-5f);
  CHECK(!g.PostIsIdentity());
}

TEST(grade_sanitize_clamps_and_replaces_nan) {
  mythos::ColorGrade g;
  g.exposure = std::nanf("");
  g.contrast = 100.0f;
  g.saturation = -5.0f;
  g.vignette = 9.0f;
  g.lut_amount = 3.0f;
  g.tint[1] = -1.0f;
  // Invalid on purpose.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  g.tonemap = static_cast<mythos::Tonemap>(77);
  const mythos::ColorGrade s = mythos::Sanitize(g);
  CHECK(s.exposure == 1.0f);
  CHECK(s.contrast == 4.0f);
  CHECK(s.saturation == 0.0f);
  CHECK(s.vignette == 2.0f);
  CHECK(s.lut_amount == 1.0f);
  CHECK(s.tint[1] == 0.0f);
  CHECK(s.tonemap == mythos::Tonemap::kAces);
}

TEST(grade_identity_lut_changes_nothing) {
  const mythos::Surface strip = mythos::IdentityLutStrip(16);
  CHECK_EQ(mythos::LutStripSize(strip), 16);
  CHECK_EQ(strip.w, 256);
  CHECK_EQ(strip.h, 16);
  const float samples[][3] = {{0, 0, 0},
                              {1, 1, 1},
                              {0.5f, 0.25f, 0.75f},
                              {0.1f, 0.9f, 0.33f},
                              {0.7f, 0.0f, 1.0f}};
  for (const auto& rgb : samples) {
    const auto out = mythos::ApplyLut(strip, rgb);
    for (int i = 0; i < 3; ++i) CHECK(std::fabs(out[i] - rgb[i]) < 0.01f);
  }
}

TEST(grade_lut_maps_colours) {
  // Swap red and blue in the table: input (1, 0, 0) must come out (0, 0, 1).
  mythos::Surface strip = mythos::IdentityLutStrip(8);
  for (uint32_t& px : strip.px) {
    const int r = static_cast<int>((px >> 16) & 0xFF);
    const int g = static_cast<int>((px >> 8) & 0xFF);
    const int b = static_cast<int>(px & 0xFF);
    px = mythos::Rgba(b, g, r);
  }
  const float red[3] = {1, 0, 0};
  const auto out = mythos::ApplyLut(strip, red);
  CHECK(out[0] < 0.01f && out[1] < 0.01f && out[2] > 0.99f);
  // Out-of-range and NaN inputs are clamped, never read out of the strip.
  const float wild[3] = {5.0f, -2.0f, std::nanf("")};
  const auto clamped = mythos::ApplyLut(strip, wild);
  CHECK(clamped[0] >= 0.0f && clamped[0] <= 1.0f);
}

TEST(grade_invalid_lut_strips) {
  CHECK_EQ(mythos::LutStripSize(mythos::Surface()), 0);
  CHECK_EQ(mythos::LutStripSize(mythos::Surface(10, 4)), 0);
  CHECK_EQ(mythos::LutStripSize(mythos::Surface(1, 1)), 0);
  CHECK_EQ(mythos::LutStripSize(mythos::Surface(65 * 65, 65)), 0);
  CHECK(mythos::IdentityLutStrip(1).Empty());
  CHECK(mythos::IdentityLutStrip(100).Empty());
  const float c[3] = {0.3f, 0.6f, 0.9f};
  const auto passthrough = mythos::ApplyLut(mythos::Surface(), c);
  CHECK(passthrough[0] == 0.3f && passthrough[2] == 0.9f);
}

// ---- easing, tweens, sequences ----

TEST(tween_easings_hit_their_ends) {
  for (int e = 0; e < mythos::kEaseCount; ++e) {
    const auto ease = static_cast<mythos::Ease>(e);
    CHECK(mythos::ApplyEase(ease, 0.0) == 0.0);
    CHECK(mythos::ApplyEase(ease, 1.0) == 1.0);
    CHECK(mythos::ApplyEase(ease, -4.0) == 0.0);
    CHECK(mythos::ApplyEase(ease, 9.0) == 1.0);
    CHECK(mythos::ApplyEase(ease, std::nan("")) == 0.0);
  }
}

TEST(tween_easings_shape) {
  using mythos::Ease;
  CHECK(std::fabs(mythos::ApplyEase(Ease::kLinear, 0.3) - 0.3) < 1e-12);
  CHECK(mythos::ApplyEase(Ease::kInQuad, 0.5) < 0.5);
  CHECK(mythos::ApplyEase(Ease::kOutQuad, 0.5) > 0.5);
  CHECK(std::fabs(mythos::ApplyEase(Ease::kInOutCubic, 0.5) - 0.5) < 1e-12);
  CHECK(std::fabs(mythos::ApplyEase(Ease::kInOutSine, 0.5) - 0.5) < 1e-12);
  // The in-out and monotone curves never run backwards.
  for (const Ease e :
       {Ease::kSmoothStep, Ease::kInQuad, Ease::kOutQuad, Ease::kInOutQuad,
        Ease::kInCubic, Ease::kOutCubic, Ease::kInOutCubic, Ease::kInOutSine}) {
    double prev = 0;
    for (int i = 1; i <= 100; ++i) {
      const double v = mythos::ApplyEase(e, i / 100.0);
      CHECK(v >= prev - 1e-12);
      prev = v;
    }
  }
  // Back and elastic overshoot 1 on the way.
  double top = 0;
  for (int i = 1; i < 100; ++i) {
    top = std::max(top, mythos::ApplyEase(Ease::kOutBack, i / 100.0));
  }
  CHECK(top > 1.0);
}

TEST(tween_value_delay_and_done) {
  mythos::Tween<double> t(10.0, 20.0, 2.0, mythos::Ease::kLinear, 1.0);
  CHECK(t.Value() == 10.0);
  t.Update(0.5);  // still in the delay
  CHECK(t.Value() == 10.0 && !t.Done());
  t.Update(1.5);  // 1 s into the 2 s run
  CHECK(std::fabs(t.Value() - 15.0) < 1e-9);
  CHECK(std::fabs(t.Progress() - 0.5) < 1e-9);
  t.Update(-5.0);  // ignored
  CHECK(std::fabs(t.Value() - 15.0) < 1e-9);
  t.Update(10.0);
  CHECK(t.Done() && t.Value() == 20.0);
  t.Reset();
  CHECK(!t.Done() && t.Value() == 10.0);
}

TEST(tween_zero_duration_jumps_after_delay) {
  mythos::Tween<double> t(0.0, 5.0, 0.0, mythos::Ease::kLinear, 1.0);
  CHECK(!t.Done() && t.Value() == 0.0);
  t.Update(1.0);
  CHECK(t.Done() && t.Value() == 5.0);
}

TEST(tween_vectors_and_retarget) {
  mythos::Tween<mythos::Vec2> t({0, 0}, {10, -10}, 1.0);
  t.Update(0.5);
  CHECK(std::fabs(t.Value().x - 5.0) < 1e-9);
  CHECK(std::fabs(t.Value().y + 5.0) < 1e-9);
  t.Retarget({0, 0}, 1.0);  // starts from (5, -5): no jump
  CHECK(std::fabs(t.Value().x - 5.0) < 1e-9);
  t.Update(1.0);
  CHECK(t.Done() && t.Value().x == 0.0);

  mythos::Tween<float> f(1.0f, 3.0f, 2.0);
  f.Update(1.0);
  CHECK(std::fabs(f.Value() - 2.0f) < 1e-6f);
  mythos::Tween<mythos::Vec3> v({0, 0, 0}, {2, 4, 6}, 1.0);
  v.Update(0.5);
  CHECK(std::fabs(v.Value().z - 3.0) < 1e-9);
}

TEST(sequence_runs_in_order_with_carry_over) {
  std::vector<std::string> log;
  double last = -1;
  mythos::Sequence seq;
  seq.Call([&] { log.emplace_back("a"); })
      .Wait(1.0)
      .Tween(0.0, 10.0, 2.0, mythos::Ease::kLinear,
             [&](const double v) { last = v; })
      .Call([&] { log.emplace_back("b"); });
  CHECK(!seq.Done());
  seq.Update(0.5);  // the first call, then half the wait
  CHECK(log.size() == 1 && log[0] == "a" && last == -1);
  seq.Update(1.0);  // 0.5 s of wait left, then 0.5 s into the tween
  CHECK(std::fabs(last - 2.5) < 1e-9);
  CHECK(log.size() == 1);
  seq.Update(5.0);  // the rest of the tween and the last call
  CHECK(last == 10.0);
  CHECK(log.size() == 2 && log[1] == "b");
  CHECK(seq.Done());
  seq.Update(1.0);  // nothing more happens
  CHECK(log.size() == 2);
}

TEST(sequence_one_long_frame_skips_nothing) {
  int calls = 0;
  mythos::Sequence seq;
  seq.Wait(0.1).Call([&] { ++calls; }).Wait(0.1).Call([&] { ++calls; });
  seq.Update(100.0);
  CHECK_EQ(calls, 2);
  CHECK(seq.Done());
  seq.Reset();
  CHECK(!seq.Done());
  seq.Update(100.0);
  CHECK_EQ(calls, 4);
}

TEST(sequence_tween_starts_at_from_and_ends_at_to) {
  std::vector<double> seen;
  mythos::Sequence seq;
  seq.Tween(2.0, 4.0, 1.0, mythos::Ease::kOutQuad,
            [&](const double v) { seen.push_back(v); });
  seq.Update(0.0);
  CHECK(!seen.empty() && seen.front() == 2.0);
  seq.Update(0.5);
  CHECK(seen.back() > 3.0 && seen.back() < 4.0);  // out-quad runs ahead
  seq.Update(1.0);
  CHECK(seen.back() == 4.0 && seq.Done());
}

TEST(sequence_empty_and_clear) {
  mythos::Sequence seq;
  CHECK(seq.Done());
  seq.Update(1.0);
  seq.Wait(1.0).Wait(-3.0);  // a negative wait counts as none
  CHECK_EQ(static_cast<int>(seq.StepCount()), 2);
  CHECK(!seq.Done());
  seq.Update(1.0);
  CHECK(seq.Done());
  seq.Clear();
  CHECK_EQ(static_cast<int>(seq.StepCount()), 0);
}

TEST(sequence_callback_may_reset) {
  int runs = 0;
  mythos::Sequence seq;
  seq.Call([&] {
    if (++runs < 3) seq.Reset();
  });
  seq.Update(0.0);
  CHECK_EQ(runs, 3);
  CHECK(seq.Done());
}

// ---- camera shake and field-of-view kick ----

TEST(camerafx_idle_is_neutral) {
  mythos::CameraFx fx;
  fx.Update(1.0);
  const mythos::CameraFxOffsets o = fx.Sample();
  CHECK(o.yaw == 0 && o.pitch == 0 && o.right == 0 && o.up == 0);
  CHECK(o.fov_scale == 1.0);
}

TEST(camerafx_shake_is_bounded_and_fades) {
  mythos::CameraFx fx(7);
  fx.AddShake(5.0);  // the total stays within 1
  CHECK(fx.Trauma() == 1.0);
  const mythos::ShakeParams& p = fx.params;
  bool moved = false;
  for (int i = 0; i < 400; ++i) {
    const mythos::CameraFxOffsets o = fx.Sample();
    CHECK(std::fabs(o.yaw) <= p.max_yaw + 1e-12);
    CHECK(std::fabs(o.pitch) <= p.max_pitch + 1e-12);
    CHECK(std::fabs(o.right) <= p.max_right + 1e-12);
    CHECK(std::fabs(o.up) <= p.max_up + 1e-12);
    if (o.yaw != 0) moved = true;
    fx.Update(1.0 / 240.0);
  }
  CHECK(moved);
  fx.Update(5.0);
  CHECK(fx.Trauma() == 0.0);
  CHECK(fx.Sample().yaw == 0.0);
}

TEST(camerafx_is_deterministic_and_seeded) {
  mythos::CameraFx a(3), b(3), c(4);
  a.AddShake(0.8);
  b.AddShake(0.8);
  c.AddShake(0.8);
  bool differs = false;
  for (int i = 0; i < 60; ++i) {
    a.Update(0.016);
    b.Update(0.016);
    c.Update(0.016);
    CHECK(a.Sample().yaw == b.Sample().yaw);
    if (a.Sample().yaw != c.Sample().yaw) differs = true;
  }
  CHECK(differs);
}

TEST(camerafx_shake_is_continuous) {
  mythos::CameraFx fx(5);
  fx.AddShake(1.0);
  fx.params.decay = 0.0;
  double prev = fx.Sample().yaw;
  for (int i = 0; i < 500; ++i) {
    fx.Update(0.004);
    const double now = fx.Sample().yaw;
    CHECK(std::fabs(now - prev) < fx.params.max_yaw * 0.25);  // no jumps
    prev = now;
  }
}

TEST(camerafx_power_keeps_small_hits_small) {
  mythos::CameraFx small(1), big(1);
  small.AddShake(0.2);
  big.AddShake(1.0);
  double s = 0, b = 0;
  for (int i = 0; i < 200; ++i) {
    s = std::max(s, std::fabs(small.Sample().yaw));
    b = std::max(b, std::fabs(big.Sample().yaw));
    small.Update(0.005);
    big.Update(0.005);
  }
  CHECK(s < b * 0.2);
}

TEST(camerafx_ignores_bad_input) {
  mythos::CameraFx fx;
  fx.AddShake(-1.0);
  fx.AddShake(std::nan(""));
  fx.KickFov(std::nan(""));
  fx.Update(-3.0);
  CHECK(fx.Trauma() == 0.0);
  CHECK(fx.Sample().fov_scale == 1.0);
}

TEST(camerafx_fov_kick_eases_back_and_is_limited) {
  mythos::CameraFx fx;
  fx.KickFov(0.1);
  CHECK(std::fabs(fx.Sample().fov_scale - 1.1) < 1e-9);
  fx.Update(0.1);
  const double mid = fx.Sample().fov_scale;
  CHECK(mid > 1.0 && mid < 1.1);
  fx.Update(10.0);
  CHECK(fx.Sample().fov_scale == 1.0);
  fx.KickFov(5.0);
  CHECK(std::fabs(fx.Sample().fov_scale - (1.0 + fx.params.fov_limit)) < 1e-9);
  fx.KickFov(-50.0);
  CHECK(std::fabs(fx.Sample().fov_scale - (1.0 - fx.params.fov_limit)) < 1e-9);
}

TEST(camerafx_intensity_scales_and_clear_stops) {
  mythos::CameraFx fx(2);
  fx.AddShake(1.0);
  fx.KickFov(0.2);
  fx.SetIntensity(0.0);
  const mythos::CameraFxOffsets off = fx.Sample();
  CHECK(off.yaw == 0 && off.up == 0 && off.fov_scale == 1.0);
  fx.SetIntensity(1.0);
  fx.Update(0.05);
  CHECK(fx.Sample().fov_scale > 1.0);
  fx.Clear();
  const mythos::CameraFxOffsets cleared = fx.Sample();
  CHECK(cleared.yaw == 0 && cleared.fov_scale == 1.0);
  fx.SetIntensity(std::nan(""));  // falls back to full
  fx.AddShake(1.0);
  fx.Update(0.05);
  CHECK(fx.Sample().yaw != 0.0 || fx.Sample().pitch != 0.0);
}

// ---- particles ----

namespace {
mythos::EmitterParams PlainEmitter() {
  mythos::EmitterParams p;
  p.life_min = p.life_max = 1.0;
  p.speed_min = p.speed_max = 1.0;
  p.spread = 0.0;
  return p;
}

std::vector<mythos::ParticleSprite> Sprites(const mythos::ParticleSystem& sys) {
  std::vector<mythos::ParticleSprite> out;
  sys.Collect(out, 100000);
  return out;
}
}  // namespace

TEST(particles_rate_spawns_and_lives_end) {
  mythos::ParticleSystem sys;
  mythos::EmitterParams p = PlainEmitter();
  p.rate = 10.0;
  const int id = sys.AddEmitter(p, {0, 0, 0});
  CHECK(id > 0);
  sys.Update(1.0);
  CHECK_EQ(sys.Count(), 10);
  sys.SetActive(id, false);
  sys.Update(0.5);
  CHECK_EQ(sys.Count(), 10);  // none new, none dead yet
  sys.Update(2.0);
  CHECK_EQ(sys.Count(), 0);
}

TEST(particles_rate_carries_the_fraction) {
  mythos::ParticleSystem sys;
  mythos::EmitterParams p = PlainEmitter();
  p.rate = 3.0;
  p.life_min = p.life_max = 1000.0;
  sys.AddEmitter(p, {0, 0, 0});
  for (int i = 0; i < 10; ++i) sys.Update(0.1);  // 3 per second for 1 s
  CHECK_EQ(sys.Count(), 3);
}

TEST(particles_burst_and_cap) {
  mythos::ParticleSystem sys;
  mythos::EmitterParams p = PlainEmitter();
  p.max_particles = 5;
  const int id = sys.AddEmitter(p, {1, 2, 3});
  sys.Burst(id, 3);
  CHECK_EQ(sys.Count(), 3);
  sys.Burst(id, 100);
  CHECK_EQ(sys.Count(), 5);
  sys.Burst(0, 5);  // unknown ids are ignored
  sys.Burst(99, 5);
  CHECK_EQ(sys.Count(), 5);
  for (const auto& s : Sprites(sys)) {
    CHECK(s.x == 1.0f && s.y == 2.0f && s.z == 3.0f);
  }
}

TEST(particles_a_long_frame_cannot_flood) {
  mythos::ParticleSystem sys;
  mythos::EmitterParams p = PlainEmitter();
  p.rate = 1000.0;
  p.max_particles = 50;
  p.life_min = p.life_max = 1000.0;
  sys.AddEmitter(p, {0, 0, 0});
  sys.Update(3600.0);
  CHECK_EQ(sys.Count(), 50);
}

TEST(particles_are_deterministic_and_seeded) {
  const auto run = [](const uint32_t seed) {
    mythos::ParticleSystem sys(seed);
    mythos::EmitterParams p;
    p.spread = 1.0;
    p.radius = 0.5;
    p.rate = 40.0;
    p.size_jitter = 0.5;
    sys.AddEmitter(p, {0, 1, 0});
    for (int i = 0; i < 20; ++i) sys.Update(0.05);
    return Sprites(sys);
  };
  const auto a = run(5), b = run(5), c = run(6);
  CHECK(!a.empty() && a.size() == b.size());
  bool same = true, differs = false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].size != b[i].size) {
      same = false;
    }
    if (i < c.size() && a[i].x != c[i].x) differs = true;
  }
  CHECK(same);
  CHECK(differs);
}

TEST(particles_launch_inside_the_cone) {
  mythos::ParticleSystem sys(3);
  mythos::EmitterParams p = PlainEmitter();
  p.direction = {0, 0, 5};  // not a unit vector: only its direction counts
  p.spread = 0.4;
  p.speed_min = p.speed_max = 2.0;
  p.life_min = p.life_max = 100.0;
  p.max_particles = 500;
  const int id = sys.AddEmitter(p, {0, 0, 0});
  sys.Burst(id, 500);
  sys.Update(0.5);  // each is now 1 m out along its own direction
  for (const auto& s : Sprites(sys)) {
    const double len = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
    CHECK(std::fabs(len - 1.0) < 1e-4);
    CHECK(s.z / len >= std::cos(0.4) - 1e-4);
  }
}

TEST(particles_full_sphere_goes_everywhere) {
  mythos::ParticleSystem sys(9);
  mythos::EmitterParams p = PlainEmitter();
  p.spread = mythos::kPi;
  p.life_min = p.life_max = 100.0;
  p.max_particles = 400;
  const int id = sys.AddEmitter(p, {0, 0, 0});
  sys.Burst(id, 400);
  sys.Update(0.1);
  bool up = false, down = false;
  for (const auto& s : Sprites(sys)) {
    up = up || s.y > 0.05f;
    down = down || s.y < -0.05f;
  }
  CHECK(up && down);
}

TEST(particles_gravity_and_drag) {
  mythos::ParticleSystem sys;
  mythos::EmitterParams p = PlainEmitter();
  p.direction = {0, 1, 0};
  p.speed_min = p.speed_max = 0.0;
  p.gravity = {0, -10, 0};
  p.life_min = p.life_max = 100.0;
  const int id = sys.AddEmitter(p, {0, 5, 0});
  sys.Burst(id, 1);
  for (int i = 0; i < 100; ++i) sys.Update(0.01);  // 1 s of falling
  const float y = Sprites(sys)[0].y;
  CHECK(y < 5.0f - 4.0f && y > 5.0f - 6.0f);  // about 5 - 0.5 * 10 * 1^2

  mythos::ParticleSystem drag_sys;
  mythos::EmitterParams d = PlainEmitter();
  d.speed_min = d.speed_max = 10.0;
  d.drag = 5.0;
  d.life_min = d.life_max = 100.0;
  const int did = drag_sys.AddEmitter(d, {0, 0, 0});
  drag_sys.Burst(did, 1);
  for (int i = 0; i < 400; ++i) drag_sys.Update(0.01);
  // Without drag it would be 40 m up; with drag it stops near speed / drag.
  CHECK(Sprites(drag_sys)[0].y < 2.5f);
}

TEST(particles_size_and_colour_over_a_life) {
  mythos::ParticleSystem sys;
  mythos::EmitterParams p = PlainEmitter();
  p.speed_min = p.speed_max = 0.0;
  p.size_start = 1.0;
  p.size_end = 3.0;
  p.color_start[0] = 1.0f;
  p.color_end[0] = 0.0f;
  p.color_start[3] = 1.0f;
  p.color_end[3] = 0.0f;
  const int id = sys.AddEmitter(p, {0, 0, 0});
  sys.Burst(id, 1);
  auto s = Sprites(sys)[0];
  CHECK(std::fabs(s.size - 1.0f) < 1e-5f && s.color[3] == 1.0f);
  sys.Update(0.5);
  s = Sprites(sys)[0];
  CHECK(std::fabs(s.size - 2.0f) < 1e-5f);
  CHECK(std::fabs(s.color[0] - 0.5f) < 1e-5f);
  CHECK(std::fabs(s.color[3] - 0.5f) < 1e-5f);
}

TEST(particles_collide_with_walls_floor_and_ceiling) {
  mythos::Level level;
  CHECK(level.ParseText("[map]\n#####\n#...#\n#####\n"));
  mythos::ParticleSystem sys;
  mythos::EmitterParams p = PlainEmitter();
  p.collide = true;
  p.bounce = 0.5;
  p.speed_min = p.speed_max = 3.0;
  p.direction = {1, 0, 0};  // at the east wall
  p.life_min = p.life_max = 100.0;
  const int id = sys.AddEmitter(p, {2.5, 1.0, 1.5});
  sys.Burst(id, 1);
  for (int i = 0; i < 200; ++i) sys.Update(0.01, &level);
  const auto s = Sprites(sys)[0];
  CHECK(s.x < 4.0f && s.x > 0.9f);  // never through the wall

  // Without a level there is no wall to hit.
  mythos::ParticleSystem free_sys;
  const int fid = free_sys.AddEmitter(p, {2.5, 1.0, 1.5});
  free_sys.Burst(fid, 1);
  for (int i = 0; i < 200; ++i) free_sys.Update(0.01);
  CHECK(Sprites(free_sys)[0].x > 4.0f);

  // A dropped particle bounces on the floor and ends up resting on it.
  mythos::ParticleSystem drop;
  mythos::EmitterParams g = p;
  g.direction = {0, -1, 0};
  g.speed_min = g.speed_max = 0.0;
  g.gravity = {0, -9.8, 0};
  const int gid = drop.AddEmitter(g, {2.5, 1.0, 1.5});
  drop.Burst(gid, 1);
  float lowest = 10.0f;
  for (int i = 0; i < 300; ++i) {
    drop.Update(0.01, &level);
    lowest = std::min(lowest, Sprites(drop)[0].y);
  }
  CHECK(lowest >= 0.0f);
  CHECK(Sprites(drop)[0].y < 0.05f);
}

TEST(particles_ceiling_and_die_on_hit) {
  mythos::Level level;
  CHECK(level.ParseText("[map]\n###\n#.#\n###\n"));
  mythos::ParticleSystem sys;
  mythos::EmitterParams p = PlainEmitter();
  p.collide = true;
  p.speed_min = p.speed_max = 20.0;
  p.life_min = p.life_max = 100.0;
  const int id = sys.AddEmitter(p, {1.5, 1.0, 1.5});  // straight up
  sys.Burst(id, 1);
  for (int i = 0; i < 50; ++i) sys.Update(0.01, &level);
  CHECK(Sprites(sys)[0].y <= mythos::kWallHeight + 1e-4f);

  mythos::ParticleSystem dying;
  mythos::EmitterParams d = p;
  d.die_on_hit = true;
  const int did = dying.AddEmitter(d, {1.5, 1.0, 1.5});
  dying.Burst(did, 5);
  dying.Update(0.5, &level);
  CHECK_EQ(dying.Count(), 0);
}

TEST(particles_removed_emitter_drains_then_goes) {
  mythos::ParticleSystem sys;
  mythos::EmitterParams p = PlainEmitter();
  p.rate = 20.0;
  const int id = sys.AddEmitter(p, {0, 0, 0});
  sys.Update(0.5);
  const int live = sys.Count();
  CHECK(live > 0);
  sys.RemoveEmitter(id);
  sys.Update(0.1);  // no new particles, existing ones fly on
  CHECK(sys.Count() <= live);
  CHECK_EQ(sys.EmitterCount(), 1);
  sys.Burst(id, 10);  // a removed emitter takes no more
  sys.SetActive(id, true);
  sys.Update(5.0);
  CHECK_EQ(sys.Count(), 0);
  CHECK_EQ(sys.EmitterCount(), 0);
}

TEST(particles_emitter_limit_and_clear) {
  mythos::ParticleSystem sys;
  for (int i = 0; i < mythos::kMaxEmitters; ++i) {
    CHECK(sys.AddEmitter(PlainEmitter(), {0, 0, 0}) > 0);
  }
  CHECK_EQ(sys.AddEmitter(PlainEmitter(), {0, 0, 0}), 0);
  sys.Clear();
  CHECK_EQ(sys.EmitterCount(), 0);
  CHECK_EQ(sys.Count(), 0);
  CHECK(sys.AddEmitter(PlainEmitter(), {0, 0, 0}) > 0);
}

TEST(particles_collect_respects_the_limit) {
  mythos::ParticleSystem sys;
  mythos::EmitterParams p = PlainEmitter();
  p.max_particles = 100;
  const int id = sys.AddEmitter(p, {0, 0, 0});
  sys.Burst(id, 100);
  std::vector<mythos::ParticleSprite> out;
  sys.Collect(out, 30);
  CHECK_EQ(static_cast<int>(out.size()), 30);
  sys.Collect(out, 0);
  CHECK_EQ(static_cast<int>(out.size()), 30);  // appends nothing
}

TEST(particles_bad_numbers_are_made_safe) {
  mythos::EmitterParams p;
  p.life_min = -4.0;
  p.life_max = std::nan("");
  p.speed_max = 1e12;
  p.rate = std::nan("");
  p.max_particles = -3;
  p.spread = 99.0;
  p.size_start = std::nan("");
  p.direction = {0, 0, 0};
  p.gravity = {1e9, 0, 0};
  p.color_start[0] = std::nanf("");
  const mythos::EmitterParams s = mythos::Sanitize(p);
  CHECK(s.life_min > 0.0 && s.life_max >= s.life_min);
  CHECK(s.speed_max <= 100.0);
  CHECK(s.rate == 0.0);
  CHECK(s.max_particles >= 1);
  CHECK(s.spread <= mythos::kPi);
  CHECK(std::isfinite(s.size_start));
  CHECK(s.gravity.x <= 100.0);
  CHECK(s.color_start[0] == 1.0f);

  mythos::ParticleSystem sys;
  const int id = sys.AddEmitter(p, {0, 0, 0});  // a zero direction still works
  sys.Burst(id, 5);
  sys.Update(std::nan(""));
  sys.Update(-1.0);
  sys.Update(0.1);
  for (const auto& sp : Sprites(sys)) CHECK(std::isfinite(sp.x + sp.y + sp.z));
}

// ---- console variables, the debug console, the free camera ----

TEST(cvar_bind_get_set_and_clamp) {
  mythos::CvarRegistry reg;
  bool b = false;
  int n = 5;
  double d = 1.5;
  CHECK(reg.AddBool("flag", &b));
  CHECK(reg.AddInt("count", &n, 0, 10));
  CHECK(reg.AddDouble("ratio", &d, 0.0, 2.0));
  CHECK(reg.Get("flag") == "0" && reg.Get("count") == "5");
  CHECK(reg.Get("ratio") == "1.5");
  CHECK(reg.Set("flag", "on").ok && b);
  CHECK(reg.Set("flag", "FALSE").ok && !b);
  CHECK(reg.Set("count", "99").ok);
  CHECK_EQ(n, 10);  // clamped
  CHECK(reg.Set("count", "-4").ok);
  CHECK_EQ(n, 0);
  CHECK(reg.Set("ratio", "0.25").ok);
  CHECK(std::fabs(d - 0.25) < 1e-12);
  CHECK(reg.Set("ratio", "9").ok);
  CHECK(d == 2.0);
}

TEST(cvar_rejects_bad_values_and_names) {
  mythos::CvarRegistry reg;
  bool b = true;
  int n = 3;
  double d = 1.0;
  reg.AddBool("flag", &b);
  reg.AddInt("count", &n, 0, 10);
  reg.AddDouble("ratio", &d, 0.0, 2.0);
  CHECK(!reg.Set("flag", "maybe").ok && b);
  CHECK(!reg.Set("count", "3x").ok && n == 3);
  CHECK(!reg.Set("count", "").ok);
  CHECK(!reg.Set("count", "2.5").ok);
  CHECK(!reg.Set("ratio", "nan").ok && d == 1.0);
  CHECK(!reg.Set("ratio", "abc").ok);
  CHECK(!reg.Set("nothing", "1").ok);
  CHECK(!reg.AddBool("", &b));
  CHECK(!reg.AddBool("two words", &b));
  CHECK(!reg.AddBool("quo\"te", &b));
  CHECK(!reg.AddBool("ok", nullptr));
  CHECK(!reg.AddInt("bad", &n, 5, 1));
  CHECK(!reg.Has("bad"));
}

TEST(cvar_names_ignore_case_and_replace) {
  mythos::CvarRegistry reg;
  int a = 1, b = 2;
  reg.AddInt("Debug.Level", &a, 0, 9);
  CHECK(reg.Has("debug.level") && reg.Has("DEBUG.LEVEL"));
  CHECK(reg.Set("DEBUG.level", "7").ok);
  CHECK_EQ(a, 7);
  reg.AddInt("debug.level", &b, 0, 9);  // replaces the first
  CHECK(reg.Get("Debug.Level") == "2");
  CHECK(reg.Remove("DEBUG.LEVEL"));
  CHECK(!reg.Has("debug.level"));
  CHECK(!reg.Remove("debug.level"));
}

TEST(cvar_execute_lines) {
  mythos::CvarRegistry reg;
  int n = 4;
  reg.AddInt("count", &n, 0, 10);
  std::vector<std::string> got;
  reg.AddCommand("say", [&](const mythos::CvarRegistry::Args& args) {
    got = args;
    return mythos::CvarResult{.ok = true, .text = "said"};
  });
  CHECK(reg.Execute("").text.empty() && reg.Execute("   ").ok);
  CHECK(reg.Execute("count").text == "count = 4");
  CHECK(reg.Execute("COUNT 8").text == "count = 8");
  CHECK_EQ(n, 8);
  CHECK(!reg.Execute("count 1 2").ok);
  CHECK(!reg.Execute("nope").ok);
  const mythos::CvarResult r = reg.Execute("say one \"two words\" three");
  CHECK(r.ok && r.text == "said");
  CHECK(got.size() == 3 && got[1] == "two words");
  CHECK(!reg.Set("say", "1").ok);  // a command is not a variable
  CHECK(reg.Get("say").empty());
}

TEST(cvar_split_command_line) {
  using mythos::SplitCommandLine;
  CHECK(SplitCommandLine("").empty());
  CHECK(SplitCommandLine("  a   b ").size() == 2);
  const auto w = SplitCommandLine(R"(say "hello  world" x"y z")");
  CHECK(w.size() == 3 && w[1] == "hello  world" && w[2] == "xy z");
  CHECK(SplitCommandLine("a \"unclosed text").back() == "unclosed text");
  CHECK(SplitCommandLine("\"\"").size() == 1);  // an empty word
}

TEST(cvar_builtin_commands_and_completion) {
  mythos::CvarRegistry reg;
  int a = 1, b = 2, c = 3;
  reg.AddInt("render.scale", &a, 0, 9, "how big");
  reg.AddInt("render.shadows", &b, 0, 9);
  reg.AddInt("audio.volume", &c, 0, 9);
  const auto names = reg.Complete("render.");
  CHECK(names.size() == 2 && names[0] == "render.scale");
  CHECK(reg.Complete("RENDER.SH").size() == 1);
  CHECK(reg.Complete("zzz").empty());
  CHECK(reg.Execute("help render.scale").text == "how big");
  CHECK(reg.Execute("help render.shadows").text == "(no help)");
  CHECK(!reg.Execute("help nothing").ok);
  const std::string listed = reg.Execute("list render.").text;
  CHECK(listed.find("render.scale = 1") != std::string::npos);
  CHECK(listed.find("audio") == std::string::npos);
  CHECK(reg.Execute("find vol").text == "audio.volume");
  CHECK(reg.Execute("find qqq").text == "(none)");
  CHECK(reg.Has("help") && reg.Has("list") && reg.Has("find"));
  const auto all = reg.Names();
  CHECK(std::ranges::is_sorted(all));
}

TEST(cvar_revision_counts_changes) {
  mythos::CvarRegistry reg;
  int n = 0;
  reg.AddInt("n", &n, 0, 9);
  const uint64_t before = reg.Revision();
  reg.Execute("n");  // reading changes nothing
  CHECK(reg.Revision() == before);
  reg.Execute("n 3");
  CHECK(reg.Revision() == before + 1);
  reg.Execute("n bad");  // a failed set changes nothing
  CHECK(reg.Revision() == before + 1);
}

TEST(cvar_every_setting_is_a_variable) {
  mythos::GameSettings s;
  mythos::CvarRegistry reg;
  mythos::RegisterSettingsCvars(reg, s);
  // One variable per settings.txt key, so a new setting is covered for free.
  const auto keys = mythos::ToKeyValues(s);
  CHECK(!keys.empty());
  for (const auto& [key, value] : keys) {
    CHECK(reg.Has(key));
    CHECK(reg.Get(key) == value);
  }
  CHECK(reg.Set("filmGrain", "0").ok && !s.film_grain);
  CHECK(reg.Set("renderScale", "99").ok);
  CHECK_EQ(s.render_scale,
           mythos::kRenderScaleCount - 1);  // clamped as the menu
  CHECK(reg.Set("resolution", "1280x720").ok);
  CHECK(mythos::kResolutions[s.resolution].w == 1280);
  CHECK(reg.Execute("vsync 0").ok && !s.vsync);
  // Whatever the settings hold afterwards is what the variables report.
  for (const auto& [key, value] : mythos::ToKeyValues(s)) {
    CHECK(reg.Get(key) == value);
  }
  CHECK(reg.Revision() > 0);
}

TEST(debug_console_typing_and_editing) {
  mythos::CvarRegistry reg;
  mythos::DebugConsole con(reg);
  CHECK(!con.Visible());
  con.Toggle();
  CHECK(con.Visible());
  con.Type("he`llo\n\t\x7f");  // the backtick and control keys are dropped
  CHECK(con.Input() == "hello");
  con.Backspace();
  CHECK(con.Input() == "hell");
  con.Type("\xCE\xB1");  // a Greek alpha: two bytes
  CHECK(con.Input().size() == 6);
  con.Backspace();
  CHECK(con.Input() == "hell");
  for (int i = 0; i < 10; ++i) con.Backspace();
  CHECK(con.Input().empty());
  con.Type(std::string(500, 'x'));
  CHECK(con.Input().size() == 200);
  con.Hide();
  CHECK(!con.Visible());
}

TEST(debug_console_submit_and_history) {
  mythos::CvarRegistry reg;
  int n = 1;
  reg.AddInt("n", &n, 0, 9);
  mythos::DebugConsole con(reg);
  con.Type("n 5");
  con.Submit();
  CHECK_EQ(n, 5);
  CHECK(con.Input().empty());
  CHECK(con.Lines().size() == 2);
  CHECK(con.Lines()[0] == "> n 5" && con.Lines()[1] == "n = 5");
  con.Type("bogus");
  con.Submit();
  CHECK(con.Lines().back() == "error: unknown: bogus");
  con.Type("   ");
  con.Submit();  // a blank line prints nothing
  CHECK(con.Lines().size() == 4);

  con.Type("draft");
  con.HistoryUp();
  CHECK(con.Input() == "bogus");
  con.HistoryUp();
  CHECK(con.Input() == "n 5");
  con.HistoryUp();  // already the oldest
  CHECK(con.Input() == "n 5");
  con.HistoryDown();
  CHECK(con.Input() == "bogus");
  con.HistoryDown();
  CHECK(con.Input() == "draft");  // back to what was being typed
  con.HistoryDown();
  CHECK(con.Input() == "draft");

  con.Submit();       // runs "draft"
  con.Type("draft");  // a repeat of the last line is not stored twice
  con.Submit();
  con.HistoryUp();
  CHECK(con.Input() == "draft");
  con.HistoryUp();
  CHECK(con.Input() == "bogus");
  con.HistoryUp();
  CHECK(con.Input() == "n 5");
}

TEST(debug_console_completion) {
  mythos::CvarRegistry reg;
  int a = 0, b = 0, c = 0;
  reg.AddInt("render.scale", &a, 0, 9);
  reg.AddInt("render.shadows", &b, 0, 9);
  reg.AddInt("audio", &c, 0, 9);
  mythos::DebugConsole con(reg);
  con.Type("au");
  con.Complete();
  CHECK(con.Input() == "audio ");  // one match fills it in
  con.Backspace();
  con.Backspace();
  con.Backspace();
  con.Backspace();
  con.Backspace();
  con.Backspace();
  con.Type("rend");
  con.Complete();
  CHECK(con.Input() == "render.s");  // the shared start
  CHECK(con.Lines().size() == 1);
  CHECK(con.Lines()[0].find("render.shadows") != std::string::npos);
  con.Type("x");
  con.Complete();  // no match: unchanged
  CHECK(con.Input() == "render.sx");
  con.Type(" 3");
  con.Complete();  // only the first word completes
  CHECK(con.Input() == "render.sx 3");
}

TEST(debug_console_log_scroll_and_limits) {
  mythos::CvarRegistry reg;
  mythos::DebugConsole con(reg);
  con.Log("one\ntwo\nthree");
  CHECK(con.Lines().size() == 3 && con.Lines()[1] == "two");
  for (int i = 0; i < 500; ++i) con.Log("line " + std::to_string(i));
  CHECK_EQ(static_cast<int>(con.Lines().size()),
           mythos::DebugConsole::kMaxLines);
  CHECK(con.Lines().back() == "line 499");
  con.Scroll(5);
  CHECK_EQ(con.ScrollBack(), 5);
  con.Scroll(-99);
  CHECK_EQ(con.ScrollBack(), 0);
  con.Scroll(9999);
  CHECK(con.ScrollBack() < mythos::DebugConsole::kMaxLines);
  con.Log("new");  // new output snaps back to the newest
  CHECK_EQ(con.ScrollBack(), 0);
  con.Clear();
  CHECK(con.Lines().empty());
  // The history is capped too.
  int n = 0;
  reg.AddInt("n", &n, 0, 999);
  for (int i = 0; i < 80; ++i) {
    con.Type("n " + std::to_string(i));
    con.Submit();
  }
  for (int i = 0; i < 80; ++i) con.HistoryUp();
  CHECK(con.Input() == "n 30");  // the oldest of the last 50
}

TEST(frame_history_average_order_and_wrap) {
  mythos::FrameHistory h;
  CHECK_EQ(h.Count(), 0);
  CHECK(h.AverageMs() == 0.0 && h.Fps() == 0.0 && h.MaxMs() == 0.0);
  h.Add(0.010);
  h.Add(0.020);
  h.Add(0.030);
  CHECK_EQ(h.Count(), 3);
  const auto ms = h.Milliseconds();
  CHECK(ms.size() == 3 && std::fabs(ms[0] - 10.0) < 1e-9);
  CHECK(std::fabs(ms[2] - 30.0) < 1e-9);
  CHECK(std::fabs(h.AverageMs() - 20.0) < 1e-9);
  CHECK(std::fabs(h.AverageMs(2) - 25.0) < 1e-9);  // the last two
  CHECK(std::fabs(h.Fps(2) - 40.0) < 1e-9);
  CHECK(std::fabs(h.MaxMs() - 30.0) < 1e-9);
  h.Add(-1.0);
  h.Add(std::nan(""));
  h.Add(99.0);
  CHECK_EQ(h.Count(), 3);  // bad samples are ignored
  for (int i = 0; i < 1000; ++i) h.Add(0.016);
  CHECK_EQ(h.Count(), mythos::FrameHistory::kCapacity);
  CHECK(std::fabs(h.AverageMs(500) - 16.0) < 1e-9);
  CHECK(std::fabs(h.MaxMs() - 16.0) < 1e-9);  // the old 30 ms has gone
  CHECK(h.AverageMs(0) == 0.0 && h.AverageMs(-4) == 0.0);
}

TEST(debug_overlay_cvars_toggle_the_panels) {
  mythos::CvarRegistry reg;
  mythos::DebugOverlayOptions o;
  mythos::RegisterDebugCvars(reg, o);
  CHECK(!o.stats && !o.graph && !o.fly);
  reg.Execute("debug.stats 1");
  reg.Execute("debug.fly on");
  CHECK(o.stats && o.fly && !o.graph);
}

TEST(debug_overlay_draws_only_with_a_font) {
  mythos::Surface fb(320, 200, 0);
  mythos::TextLayer layer;
  mythos::UiCanvas canvas{fb, layer};
  mythos::DebugOverlayOptions o;
  o.stats = o.graph = true;
  mythos::DebugOverlayTheme theme;
  theme.text = {.font = nullptr, .size = 8.0f, .color = 0xFFFFFFFF, .fx = {}};
  mythos::FrameHistory frames;
  for (int i = 0; i < 100; ++i) frames.Add(0.016 + 0.001 * (i % 7));
  mythos::RenderStats stats;
  stats.draw_calls = 120;
  mythos::CvarRegistry reg;
  mythos::DebugConsole con(reg);
  con.Show();
  mythos::DrawDebugOverlay(canvas, o, theme, frames, stats, &con, 0.0);
  CHECK(layer.runs.empty());
  for (const uint32_t px : fb.px) CHECK_EQ(px, 0u);  // nothing without a font

  mythos::SdfFont font;
  CHECK(font.LoadFile(
      std::string(MYTHOS_TEST_ASSET_DIR) + "/fonts/Alegreya.ttf", nullptr));
  if (!font.Loaded()) return;
  theme.text.font = &font;
  con.Log("hello console");
  con.Type("typing");
  mythos::DrawDebugOverlay(canvas, o, theme, frames, stats, &con, 0.0);
  CHECK(layer.runs.size() >= 6);  // the stats lines, a log line, the input
  bool drew = false;
  for (const uint32_t px : fb.px) drew = drew || px != 0u;
  CHECK(drew);
  bool saw_input = false, saw_draws = false;
  for (const auto& run : layer.runs) {
    if (run.text.find("typing") != std::string::npos) saw_input = true;
    if (run.text.find("draws 120") != std::string::npos) saw_draws = true;
  }
  CHECK(saw_input && saw_draws);

  // Nothing on: nothing drawn; an empty surface is fine too.
  mythos::Surface fb2(320, 200, 0);
  mythos::TextLayer layer2;
  mythos::UiCanvas canvas2{fb2, layer2};
  mythos::DrawDebugOverlay(canvas2, mythos::DebugOverlayOptions{}, theme,
                           frames, stats, nullptr, 0.0);
  CHECK(layer2.runs.empty());
  mythos::Surface empty;
  mythos::UiCanvas canvas3{empty, layer2};
  mythos::DrawDebugOverlay(canvas3, o, theme, frames, stats, &con, 0.0);
}

TEST(free_camera_flies_along_the_view) {
  mythos::FreeCamera cam;
  cam.x = cam.z = 0;
  cam.y = 2.0;
  cam.yaw = 0;  // facing +x
  cam.pitch = 0;
  mythos::MoveInput in;
  in.forward = true;
  cam.Update(in, 0.0, 1.0, 0.0);
  CHECK(std::fabs(cam.x - 4.0) < 1e-9 && std::fabs(cam.z) < 1e-9);
  CHECK(std::fabs(cam.y - 2.0) < 1e-9);
  in.forward = false;
  in.back = true;
  cam.Update(in, 0.0, 0.5, 0.0);
  CHECK(std::fabs(cam.x - 2.0) < 1e-9);

  mythos::FreeCamera up;
  up.pitch = mythos::kPi / 4;
  mythos::MoveInput fwd;
  fwd.forward = true;
  up.Update(fwd, 0.0, 1.0, 0.0);
  CHECK(up.y > 1.55 + 2.0);  // looking up, flying forward climbs

  mythos::FreeCamera side;
  side.yaw = 0;  // facing +x, so right is +z
  mythos::MoveInput strafe;
  strafe.strafe_right = true;
  side.Update(strafe, 0.0, 1.0, 0.0);
  CHECK(std::fabs(side.z - 4.0) < 1e-9 && std::fabs(side.x) < 1e-9);
}

TEST(free_camera_rise_run_look_and_place) {
  mythos::FreeCamera cam;
  const double y0 = cam.y;
  mythos::MoveInput none;
  cam.Update(none, 1.0, 1.0, 0.0);
  CHECK(std::fabs(cam.y - (y0 + cam.speed)) < 1e-9);
  cam.Update(none, 9.0, 1.0, 0.0);  // rise is clamped to 1
  CHECK(std::fabs(cam.y - (y0 + 2 * cam.speed)) < 1e-9);

  mythos::FreeCamera fast;
  mythos::MoveInput run;
  run.forward = true;
  run.run = true;
  fast.Update(run, 0.0, 1.0, 0.0);
  CHECK(std::fabs(fast.x - fast.speed * fast.fast) < 1e-9);

  mythos::FreeCamera look;
  mythos::MoveInput mouse;
  mouse.mouse_dx = 100;
  mouse.mouse_dy = -100;               // up is negative
  look.Update(mouse, 0.0, 0.0, 0.01);  // dt 0: looks but does not move
  CHECK(std::fabs(look.yaw - 1.0) < 1e-9 && std::fabs(look.pitch - 1.0) < 1e-9);
  CHECK(look.x == 0.0);
  mouse.mouse_dy = -10000;
  look.Update(mouse, 0.0, 0.0, 0.01);
  CHECK(look.pitch == look.max_pitch);

  mythos::FreeCamera analog;
  mythos::MoveInput stick;
  stick.analog_forward = 0.5;
  analog.Update(stick, 0.0, 1.0, 0.0);
  CHECK(std::fabs(analog.x - 2.0) < 1e-9);
  mythos::MoveInput bad;
  bad.forward = true;
  analog.Update(bad, 0.0, std::nan(""), 0.0);  // a bad dt moves nothing
  CHECK(std::fabs(analog.x - 2.0) < 1e-9);

  mythos::Player p;
  p.pos = {3.0, 7.0};
  p.angle = 1.0;
  p.pitch = 0.2;
  mythos::FreeCamera placed;
  placed.Place(p, 1.6);
  CHECK(placed.x == 3.0 && placed.z == 7.0 && placed.y == 1.6);
  CHECK(placed.yaw == 1.0 && placed.pitch == 0.2);
}

TEST(save_text_round_trips_and_rejects_bad_files) {
  mythos::SaveData data;
  data.version = 2;
  data.Set("game", mythos::json::Value{{"level", 3}});
  mythos::SaveData back;
  CHECK(mythos::SaveFromText(mythos::SaveToText(data), 2, {}, back) ==
        mythos::SaveStatus::kOk);
  CHECK_EQ((*back.Section("game"))["level"].get<int>(), 3);
  CHECK(mythos::SaveFromText("", 2, {}, back) == mythos::SaveStatus::kMissing);
  CHECK(mythos::SaveFromText("{oops", 2, {}, back) ==
        mythos::SaveStatus::kCorrupt);
  CHECK(mythos::SaveFromText("{\"version\":1}", 2, {}, back) ==
        mythos::SaveStatus::kCorrupt);
  // Written by a newer build.
  CHECK(mythos::SaveFromText(mythos::SaveToText(data), 1, {}, back) ==
        mythos::SaveStatus::kTooNew);
}

TEST(save_migrations_run_in_order) {
  mythos::SaveData old;
  old.version = 1;
  old.Set("n", 1);
  const std::vector<mythos::SaveMigration> steps = {
      [](mythos::json::Value& s) {
        s["n"] = s["n"].get<int>() + 10;
        return true;
      },
      [](mythos::json::Value& s) {
        s["n"] = s["n"].get<int>() * 2;
        return true;
      }};
  mythos::SaveData out;
  CHECK(mythos::SaveFromText(mythos::SaveToText(old), 3, steps, out) ==
        mythos::SaveStatus::kOk);
  CHECK_EQ(out.version, 3);
  CHECK_EQ((*out.Section("n")).get<int>(), 22);
  // A missing step fails rather than guessing.
  CHECK(mythos::SaveFromText(mythos::SaveToText(old), 4, steps, out) ==
        mythos::SaveStatus::kMigrationFailed);
}

TEST(save_slots_write_atomically_and_delete) {
  const auto dir = std::filesystem::temp_directory_path() / "mythos_save_test";
  std::filesystem::remove_all(dir);
  const mythos::SaveSlots slots(dir.string(), 1);
  mythos::SaveData out;
  CHECK(slots.Load(0, out) == mythos::SaveStatus::kMissing);
  CHECK(!slots.Exists(0));
  mythos::SaveData data;
  data.Set("a", "b");
  CHECK(slots.Save(0, data) == mythos::SaveStatus::kOk);
  CHECK(slots.Exists(0));
  CHECK(slots.Load(0, out) == mythos::SaveStatus::kOk);
  CHECK((*out.Section("a")).get<std::string>() == "b");
  CHECK(slots.Delete(0));
  CHECK(!slots.Exists(0));
  std::filesystem::remove_all(dir);
}

TEST(save_state_round_trips_progress_and_player) {
  mythos::Progress p;
  p.AddNote("n1");
  mythos::Progress::AddUnique(p.clues, "c1");
  p.held = "key";
  p.flags["door"] = 2;
  mythos::Progress p2;
  CHECK(mythos::ProgressFromJson(mythos::ProgressToJson(p), p2));
  CHECK(p2.clues == p.clues && p2.notebook == p.notebook);
  CHECK(p2.held == "key" && p2.flags.at("door") == 2);
  CHECK(!mythos::ProgressFromJson(mythos::json::Value(5), p2));

  mythos::Player a;
  a.pos = {3.5, 7.25};
  a.angle = 1.5;
  a.pitch = -0.2;
  a.flashlight = true;
  mythos::Player b;
  CHECK(mythos::PlayerFromJson(mythos::PlayerToJson(a), b));
  CHECK(b.pos.x == 3.5 && b.pos.y == 7.25 && b.angle == 1.5);
  CHECK(b.pitch == -0.2 && b.flashlight);
}

TEST(input_map_parses_and_prints_bindings) {
  const auto k = mythos::ParseBinding("key:W");
  CHECK(k && k->kind == mythos::BindKind::kKey && k->code == "W");
  const auto a = mythos::ParseBinding("axis:LeftX-");
  CHECK(a && a->code == "LeftX" && a->sign == -1);
  CHECK(mythos::BindingToText(*a) == "axis:LeftX-");
  CHECK(!mythos::ParseBinding("axis:LeftX"));
  CHECK(!mythos::ParseBinding("bogus:W"));
  CHECK(!mythos::ParseBinding("key:"));
  CHECK(!mythos::ParseBinding("W"));
}

TEST(input_map_dead_zone_and_axis_strength) {
  CHECK_EQ(mythos::ApplyDeadZone(0.1, 0.2), 0.0);
  CHECK_EQ(mythos::ApplyDeadZone(1.0, 0.2), 1.0);
  CHECK(std::fabs(mythos::ApplyDeadZone(-0.6, 0.2) + 0.5) < 1e-9);
  const mythos::Binding plus{mythos::BindKind::kPadAxis, "LeftY", 1};
  CHECK(mythos::AxisStrength(plus, 0.6, 0.2) > 0.49);
  CHECK_EQ(mythos::AxisStrength(plus, -0.9, 0.2), 0.0);
}

TEST(input_map_rebind_conflicts_contexts_and_persistence) {
  const mythos::Binding w{mythos::BindKind::kKey, "W", 0};
  const mythos::Binding s{mythos::BindKind::kKey, "S", 0};
  const mythos::Binding e{mythos::BindKind::kKey, "E", 0};
  mythos::InputMap map;
  map.Define("forward", "play", {w});
  map.Define("back", "play", {s});
  map.Define("confirm", "menu", {w});
  CHECK(map.Resolve("play", w) == "forward");
  CHECK(map.Resolve("menu", w) == "confirm");
  // Same input in another context is no conflict.
  CHECK(map.FindConflict("confirm", w).empty());
  // Refused, then stolen.
  CHECK(map.Rebind("back", 0, w) == "forward");
  CHECK(map.Bindings("back")[0] == s);
  CHECK(map.Rebind("back", 0, w, true).empty());
  CHECK(map.Bindings("forward").empty());
  CHECK(map.Rebind("forward", 0, e).empty());

  const mythos::json::Value saved = map.ToJson();
  CHECK(!saved.contains("confirm"));
  mythos::InputMap fresh;
  fresh.Define("forward", "play", {w});
  fresh.Define("back", "play", {s});
  fresh.Define("confirm", "menu", {w});
  CHECK(fresh.FromJson(saved));
  CHECK(fresh.Bindings("forward")[0] == e);
  CHECK(fresh.Bindings("back")[0] == w);
  CHECK(!fresh.FromJson(mythos::json::Value(3)));
  fresh.ResetAll();
  CHECK(fresh.Bindings("forward")[0] == w);
  // Unknown actions and junk are skipped.
  const mythos::json::Value junk = {{"nope", {"key:Q"}}, {"back", {"zzz"}}};
  CHECK(fresh.FromJson(junk));
  CHECK(fresh.Bindings("back").empty());
}

TEST(ai_selector_and_sequence_are_reactive) {
  using mythos::ai::Status;
  int chased = 0, patrolled = 0;
  bool sees = false;
  std::vector<mythos::ai::NodePtr> chase;
  chase.push_back(mythos::ai::Condition([&] { return sees; }));
  chase.push_back(mythos::ai::Leaf([&](double) {
    ++chased;
    return Status::kRunning;
  }));
  std::vector<mythos::ai::NodePtr> root;
  root.push_back(mythos::ai::Sequence(std::move(chase)));
  root.push_back(mythos::ai::Leaf([&](double) {
    ++patrolled;
    return Status::kSuccess;
  }));
  auto tree = mythos::ai::Selector(std::move(root));
  CHECK(tree->Tick(0.1) == Status::kSuccess);
  CHECK_EQ(patrolled, 1);
  sees = true;
  CHECK(tree->Tick(0.1) == Status::kRunning);
  CHECK_EQ(chased, 1);
  CHECK_EQ(patrolled, 1);
  // Losing sight interrupts the chase at once.
  sees = false;
  CHECK(tree->Tick(0.1) == Status::kSuccess);
  CHECK_EQ(patrolled, 2);
}

TEST(ai_decorators) {
  using mythos::ai::Status;
  auto ok = [] {
    return mythos::ai::Leaf([](double) { return Status::kSuccess; });
  };
  auto bad = [] {
    return mythos::ai::Leaf([](double) { return Status::kFailure; });
  };
  CHECK(mythos::ai::Inverter(ok())->Tick(0) == Status::kFailure);
  CHECK(mythos::ai::Inverter(bad())->Tick(0) == Status::kSuccess);
  CHECK(mythos::ai::Succeeder(bad())->Tick(0) == Status::kSuccess);
  int runs = 0;
  auto cd = mythos::ai::Cooldown(1.0, mythos::ai::Leaf([&](double) {
                                   ++runs;
                                   return Status::kSuccess;
                                 }));
  CHECK(cd->Tick(0.1) == Status::kSuccess);
  CHECK(cd->Tick(0.5) == Status::kFailure);
  CHECK(cd->Tick(0.5) == Status::kFailure);
  CHECK_EQ(runs, 1);
  CHECK(cd->Tick(0.5) == Status::kSuccess);
  CHECK_EQ(runs, 2);
  // Empty composites: a sequence passes, a selector fails.
  CHECK(mythos::ai::Sequence({})->Tick(0) == Status::kSuccess);
  CHECK(mythos::ai::Selector({})->Tick(0) == Status::kFailure);
}

TEST(ai_sight_and_hearing) {
  mythos::Level l;
  CHECK(l.Parse({"#######", "#..#..#", "#..#..#", "#.....#", "#######"}));
  mythos::ai::Sight eye;
  eye.eye = {1.5, 3.5};
  eye.angle = 0;  // facing +x
  eye.fov = 1.5;
  eye.range = 6;
  CHECK(mythos::ai::CanSee(l, eye, {4.5, 3.5}));
  // Behind, out of range, and behind the wall.
  eye.angle = 3.14159;
  CHECK(!mythos::ai::CanSee(l, eye, {4.5, 3.5}));
  eye.angle = 0;
  eye.range = 2;
  CHECK(!mythos::ai::CanSee(l, eye, {4.5, 3.5}));
  eye.range = 6;
  eye.eye = {1.5, 1.5};
  CHECK(!mythos::ai::CanSee(l, eye, {4.5, 1.5}));
  // Hearing: a wall doubles the distance.
  const mythos::ai::Noise n{{4.5, 1.5}, 4.0};
  CHECK(!mythos::ai::CanHear(l, {1.5, 1.5}, 1.0, n));
  CHECK(mythos::ai::CanHear(l, {1.5, 1.5}, 2.0, n));
  CHECK(mythos::ai::CanHear(l, {4.5, 3.5}, 1.0, n));
  CHECK(!mythos::ai::CanHear(l, {4.5, 3.5}, 0.0, n));
}

namespace {
// Two bones: a root at the origin and a child one metre up.
mythos::Skeleton TwoBones() {
  mythos::Bone root;
  root.name = "root";
  mythos::Bone arm;
  arm.name = "arm";
  arm.parent = 0;
  arm.bind.t = {0, 1, 0};
  mythos::Skeleton s;
  CHECK(s.Set({root, arm}));
  return s;
}

mythos::Clip SwingClip() {
  mythos::Clip c;
  c.name = "swing";
  c.duration = 2.0;
  mythos::BoneTrack t;
  t.bone = 0;
  t.rotation = {{0.0, {}},
                {2.0, mythos::QuatAxisAngle({0, 0, 1}, mythos::kPi / 2)}};
  t.translation = {{0.0, {0, 0, 0}}, {2.0, {4, 0, 0}}};
  c.tracks.push_back(t);
  return c;
}
}

TEST(skeleton_validates_and_bind_pose_skins_to_identity) {
  mythos::Skeleton s = TwoBones();
  CHECK_EQ(s.Count(), 2);
  CHECK_EQ(s.Find("arm"), 1);
  CHECK_EQ(s.Find("none"), -1);
  mythos::Bone bad;
  bad.parent = 0;  // a parent that does not come first
  CHECK(!s.Set({bad}));
  CHECK_EQ(s.Count(), 2);
  const auto skin = mythos::SkinMatrices(s, mythos::BindPose(s));
  CHECK_EQ(skin.size(), 2u);
  for (const auto& m : skin) {
    for (int i = 0; i < 16; ++i) {
      CHECK(std::fabs(m[static_cast<size_t>(i)] - ((i % 5 == 0) ? 1.f : 0.f)) <
            1e-5);
    }
  }
}

TEST(skeleton_samples_keys_and_moves_children) {
  const mythos::Skeleton s = TwoBones();
  const mythos::Clip c = SwingClip();
  const mythos::Pose mid = mythos::SamplePose(s, c, 1.0);
  CHECK(std::fabs(mid[0].t.x - 2.0) < 1e-9);
  // Looping wraps; the end of the loop is the start again.
  CHECK(std::fabs(mythos::SamplePose(s, c, 3.0)[0].t.x - 2.0) < 1e-9);
  mythos::Clip once = c;
  once.loop = false;
  CHECK(std::fabs(mythos::SamplePose(s, once, 9.0)[0].t.x - 4.0) < 1e-9);
  // At the end the root is turned 90 degrees about z: the child that stood at
  // (0,1,0) above it ends up at (-1,0,0) from the root, so (3,0,0) overall.
  const auto skin = mythos::SkinMatrices(s, mythos::SamplePose(s, once, 2.0));
  const auto& m = skin[1];             // maps the bind position of the arm bone
  const float x = m[12] + m[4] * 1.f;  // bind-space point (0,1,0)
  const float y = m[13] + m[5] * 1.f;
  CHECK(std::fabs(x - 3.0) < 1e-4 && std::fabs(y - 0.0) < 1e-4);
  // A track for a bone that is not there is ignored.
  mythos::Clip stray = c;
  stray.tracks[0].bone = 7;
  CHECK(std::fabs(mythos::SamplePose(s, stray, 1.0)[0].t.x) < 1e-9);
}

TEST(skeleton_blend_slerp_and_crossfade) {
  const mythos::Quat a, b = mythos::QuatAxisAngle({0, 0, 1}, mythos::kPi);
  const mythos::Quat h = mythos::Slerp(a, b, 0.5);
  CHECK(std::fabs(h.z - std::sin(mythos::kPi / 4)) < 1e-9);
  // The long way round is avoided.
  const mythos::Quat neg{-a.x, -a.y, -a.z, -a.w};
  CHECK(std::fabs(mythos::Slerp(a, neg, 0.5).w) > 0.99);

  const mythos::Skeleton s = TwoBones();
  mythos::Clip swing = SwingClip();
  swing.loop = false;  // holds its last pose while fading out
  mythos::Clip still;
  still.duration = 1.0;
  mythos::Animator anim(&s);
  anim.Play(&swing);
  anim.Update(2.0 - 1e-9);  // root.x nearly 4
  anim.Play(&still, 1.0);
  CHECK(anim.Current() == &still);
  CHECK(std::fabs(anim.CurrentPose()[0].t.x - 4.0) < 1e-6);
  anim.Update(0.5);  // halfway through the fade
  const double mid = anim.CurrentPose()[0].t.x;
  CHECK(std::fabs(mid - 2.0) < 1e-6);
  anim.Update(0.6);
  CHECK(std::fabs(anim.CurrentPose()[0].t.x) < 1e-9);
  CHECK(!anim.Finished());
}

TEST(EditorRenamingAndRemovingTriggersKeepsRulesInStep) {
  mythos::editor::LevelDoc doc;
  mythos::Trigger a, b;
  a.id = "door";
  b.id = "lamp";
  doc.triggers = {a, b};
  mythos::EventRule r1, r2, r3;
  r1.id = "r1";
  r1.event = {mythos::EventKind::kTrigger, "door", "enter", false};
  r2.id = "r2";
  r2.event = {mythos::EventKind::kTrigger, "door", "exit", false};
  r3.id = "r3";
  r3.event = {mythos::EventKind::kFlag, "door", "", true};  // not a trigger
  doc.rules = {r1, r2, r3};
  int n = -1;
  CHECK(!mythos::editor::RenameTrigger(doc, "door", "lamp", &n));  // taken
  CHECK(!mythos::editor::RenameTrigger(doc, "nope", "x", &n));
  CHECK(!mythos::editor::RenameTrigger(doc, "door", "", &n));
  CHECK_EQ(n, 0);
  CHECK(mythos::editor::RenameTrigger(doc, "door", "gate", &n));
  CHECK_EQ(n, 2);
  CHECK(doc.triggers[0].id == "gate" && doc.rules[0].event.id == "gate");
  CHECK(doc.rules[2].event.id == "door");  // a flag of that name is its own
  CHECK_EQ(mythos::editor::RemoveTrigger(doc, "nope"), -1);
  CHECK_EQ(mythos::editor::RemoveTrigger(doc, "gate"), 2);
  CHECK_EQ(doc.triggers.size(), 1u);
  CHECK_EQ(doc.rules.size(), 1u);
  CHECK(mythos::editor::NewRuleId(doc) == "rule_1");
  doc.rules[0].id = "rule_1";
  CHECK(mythos::editor::NewRuleId(doc) == "rule_2");
}

namespace {
// A .glb of one triangle skinned to two joints (a root and a child), with a
// two-key clip moving the child.
std::string TinyGlb() {
  std::string bin;
  const auto f = [&](std::initializer_list<float> v) {
    for (const float x : v) bin.append(reinterpret_cast<const char*>(&x), 4);
  };
  f({0, 0, 0, 1, 0, 0, 0, 1, 0});  // positions: 0..35
  const unsigned char joints[12] = {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
  bin.append(reinterpret_cast<const char*>(joints), 12);  // 36..47
  f({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});                // weights: 48..95
  f({0, 1});                                              // times: 96..103
  f({0, 0, 0, 0, 2, 0});                                  // translations
  mythos::json::Value j = {
      {"asset", {{"version", "2.0"}}},
      {"scene", 0},
      {"scenes", {{{"nodes", {0, 2}}}}},
      {"nodes",
       {{{"name", "root"}, {"children", {1}}, {"translation", {0, 1, 0}}},
        {{"name", "child"}, {"translation", {0, 1, 0}}},
        {{"name", "body"}, {"mesh", 0}, {"skin", 0}}}},
      {"skins", {{{"joints", {0, 1}}}}},
      {"meshes",
       {{{"primitives",
          {{{"attributes",
             {{"POSITION", 0}, {"JOINTS_0", 1}, {"WEIGHTS_0", 2}}},
            {"material", 0}}}}}}},
      {"materials",
       {{{"name", "skin"},
         {"pbrMetallicRoughness", {{"baseColorFactor", {1, 0, 0, 1}}}}}}},
      {"animations",
       {{{"name", "lift"},
         {"samplers", {{{"input", 3}, {"output", 4}}}},
         {"channels",
          {{{"sampler", 0},
            {"target", {{"node", 1}, {"path", "translation"}}}}}}}}},
      {"accessors",
       {{{"bufferView", 0},
         {"componentType", 5126},
         {"count", 3},
         {"type", "VEC3"}},
        {{"bufferView", 1},
         {"componentType", 5121},
         {"count", 3},
         {"type", "VEC4"}},
        {{"bufferView", 2},
         {"componentType", 5126},
         {"count", 3},
         {"type", "VEC4"}},
        {{"bufferView", 3},
         {"componentType", 5126},
         {"count", 2},
         {"type", "SCALAR"}},
        {{"bufferView", 4},
         {"componentType", 5126},
         {"count", 2},
         {"type", "VEC3"}}}},
      {"bufferViews",
       {{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
        {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 12}},
        {{"buffer", 0}, {"byteOffset", 48}, {"byteLength", 48}},
        {{"buffer", 0}, {"byteOffset", 96}, {"byteLength", 8}},
        {{"buffer", 0}, {"byteOffset", 104}, {"byteLength", 24}}}},
      {"buffers", {{{"byteLength", static_cast<int>(bin.size())}}}}};
  std::string text = j.dump();
  while (text.size() % 4) text += ' ';
  while (bin.size() % 4) bin += '\0';
  std::string out = "glTF";
  const auto u32 = [&](uint32_t v) {
    out.append(reinterpret_cast<const char*>(&v), 4);
  };
  u32(2);
  u32(static_cast<uint32_t>(12 + 8 + text.size() + 8 + bin.size()));
  u32(static_cast<uint32_t>(text.size()));
  u32(0x4E4F534A);
  out += text;
  u32(static_cast<uint32_t>(bin.size()));
  u32(0x004E4942);
  out += bin;
  return out;
}
}

TEST(gltf_loads_a_skinned_mesh_skeleton_and_clip) {
  mythos::GltfModel m;
  std::string error;
  CHECK(mythos::ParseGlb(TinyGlb(), m, &error));
  CHECK(error.empty());
  CHECK_EQ(m.skeleton.Count(), 2);
  CHECK_EQ(m.skeleton.Find("child"), 1);
  CHECK_EQ(m.skeleton.Bones()[1].parent, 0);
  CHECK_EQ(m.batches.size(), 1u);
  CHECK_EQ(m.batches[0].verts.size(), 3u);
  CHECK_EQ(m.batches[0].skin.size(), 3u);
  CHECK(m.batches[0].texture == "skin");
  CHECK_EQ(m.base_colors[0][0], 1.0f);
  CHECK_EQ(m.batches[0].skin[0].joints[0], 1.0f);
  const mythos::Clip* lift = m.FindClip("lift");
  CHECK(lift != nullptr && lift->duration == 1.0);
  CHECK(m.FindClip("nope") == nullptr);
  const mythos::Pose mid = mythos::SamplePose(m.skeleton, *lift, 0.5);
  CHECK(std::fabs(mid[1].t.y - 1.0) < 1e-6);
  // The bind pose skins to identity.
  const auto skin =
      mythos::SkinMatrices(m.skeleton, mythos::BindPose(m.skeleton));
  CHECK(std::fabs(skin[1][13]) < 1e-5);
}

TEST(gltf_rejects_bad_files) {
  mythos::GltfModel m;
  std::string error;
  CHECK(!mythos::ParseGlb("", m, &error));
  CHECK(!mythos::ParseGlb("not a glb at all, but long enough", m, &error));
  CHECK(error.find("glb") != std::string::npos);
  std::string cut = TinyGlb();
  cut.resize(cut.size() - 40);
  CHECK(!mythos::ParseGlb(cut, m, &error));
  CHECK(!mythos::LoadGlb("/no/such/file.glb", m, &error));
}

TEST(gltf_loads_the_shipped_character) {
  mythos::GltfModel m;
  std::string error;
  CHECK(mythos::LoadGlb(
      std::string(MYTHOS_TEST_ASSET_DIR) + "/characters/prototype_pete.glb", m,
      &error));
  CHECK(error.empty());
  CHECK(m.skeleton.Count() > 1 && m.skeleton.Count() <= mythos::kMaxBones);
  CHECK(!m.batches.empty() &&
        m.batches[0].skin.size() == m.batches[0].verts.size());
  const mythos::Clip* walk = m.FindClip("Walk");
  CHECK(walk != nullptr && m.FindClip("Idle") != nullptr);
  if (walk == nullptr) return;
  // Every sampled skin matrix is finite, and the mesh stays human sized.
  for (const double t : {0.0, 0.3, 0.7}) {
    for (const auto& mat : mythos::SkinMatrices(
             m.skeleton, mythos::SamplePose(m.skeleton, *walk, t))) {
      for (const float v : mat) CHECK(std::isfinite(v));
    }
  }
  float top = -1e9f;
  for (const auto& v : m.batches[0].verts) top = std::max(top, v.py);
  CHECK(top > 0.5f && top < 4.0f);
}

TEST(anim_state_machine_transitions_on_params_and_finish) {
  mythos::Bone root;
  root.name = "root";
  mythos::Skeleton skeleton;
  CHECK(skeleton.Set({root}));
  mythos::Clip idle, walk, wave;
  idle.name = "Idle";
  idle.duration = 1;
  walk.name = "Walk";
  walk.duration = 1;
  wave.name = "Wave";
  wave.duration = 0.5;
  wave.loop = false;
  const std::vector<mythos::Clip> clips = {idle, walk, wave};
  mythos::AnimStateMachine m(&skeleton, &clips);
  CHECK(!m.AddState("x", "Nope"));
  CHECK(m.AddState("idle", "Idle") && m.AddState("walk", "Walk") &&
        m.AddState("wave", "Wave"));
  CHECK(!m.AddState("idle", "Idle"));
  m.AddTransition({"idle", "walk", {{"speed", mythos::AnimOp::kGreater, 0.1}}});
  m.AddTransition({"walk", "idle", {{"speed", mythos::AnimOp::kLess, 0.1}}});
  m.AddTransition({"*", "wave", {{"greet", mythos::AnimOp::kEqual, 1}}});
  m.AddTransition({"wave", "idle", {}, true});
  CHECK(m.State() == "idle");
  m.Update(0.1);
  CHECK(m.State() == "idle");
  m.SetFloat("speed", 1.0);
  m.Update(0.1);
  CHECK(m.State() == "walk");
  m.SetFloat("speed", 0.0);
  m.Update(0.1);
  CHECK(m.State() == "idle");
  // Any state can wave; the wave ends by itself and returns to idle.
  m.SetBool("greet", true);
  m.Update(0.1);
  CHECK(m.State() == "wave");
  m.SetBool("greet", false);
  m.Update(0.2);
  CHECK(m.State() == "wave");
  m.Update(0.4);
  m.Update(0.1);
  CHECK(m.State() == "idle");
  CHECK(m.CurrentPose().size() == 1u);
}

TEST(input_map_action_tracker_edges_and_default_map) {
  const mythos::InputMap map = mythos::DefaultGameplayMap();
  CHECK(map.Has("forward") && map.Has("interact") && map.Has("run"));
  // No input is bound to two play actions.
  for (const std::string& a : map.Actions(mythos::kPlayContext)) {
    for (const mythos::Binding& b : map.Bindings(a)) {
      CHECK(map.Resolve(mythos::kPlayContext, b) == a);
    }
  }
  bool w_down = false;
  double stick = 0;
  const mythos::BindingProbe probe = [&](const mythos::Binding& b) {
    if (b.kind == mythos::BindKind::kKey && b.code == "W")
      return w_down ? 1.0 : 0.0;
    if (b.kind == mythos::BindKind::kPadAxis) {
      return mythos::AxisStrength(b, stick, 0.2);
    }
    return 0.0;
  };
  mythos::ActionTracker t;
  t.Update(map, mythos::kPlayContext, probe);
  CHECK(!t.Down("forward") && !t.Pressed("forward"));
  w_down = true;
  t.Update(map, mythos::kPlayContext, probe);
  CHECK(t.Down("forward") && t.Pressed("forward") && !t.Released("forward"));
  t.Update(map, mythos::kPlayContext, probe);
  CHECK(t.Down("forward") && !t.Pressed("forward"));
  w_down = false;
  t.Update(map, mythos::kPlayContext, probe);
  CHECK(!t.Down("forward") && t.Released("forward"));
  CHECK(!t.Down("no_such_action"));
  // An axis binding gives strength, and the strongest binding wins.
  mythos::InputMap custom = map;
  custom.Rebind("forward", 1, {mythos::BindKind::kPadAxis, "LeftY", -1});
  stick = -0.6;
  t.Update(custom, mythos::kPlayContext, probe);
  CHECK(t.Strength("forward") > 0.49 && t.Strength("forward") < 0.51);
}

TEST(spatial_occlusion_muffle_and_reverb) {
  const mythos::Occlusion open = mythos::ComputeOcclusion(0, 0);
  CHECK_EQ(open.gain, 1.0);
  CHECK_EQ(open.muffle, 0.0);
  const mythos::Occlusion one = mythos::ComputeOcclusion(1, 0);
  const mythos::Occlusion two = mythos::ComputeOcclusion(2, 0);
  CHECK(one.gain < 1 && two.gain < one.gain);
  CHECK(two.muffle > one.muffle);
  CHECK(mythos::ComputeOcclusion(10, 5).muffle <= 1.0);
  CHECK(mythos::ComputeOcclusion(0, 1).gain > one.gain);  // a door muffles less
  CHECK(mythos::ComputeOcclusion(-3, -1).gain == 1.0);
  CHECK_EQ(mythos::MuffleCutoffHz(0), 16000.0);
  CHECK(std::fabs(mythos::MuffleCutoffHz(1) - 500.0) < 1e-6);
  CHECK(mythos::MuffleCutoffHz(0.5) < 16000 &&
        mythos::MuffleCutoffHz(0.5) > 500);
  const double a = mythos::LowpassCoefficient(500, 48000);
  CHECK(a > 0 && a < mythos::LowpassCoefficient(16000, 48000));
  CHECK_EQ(mythos::LowpassCoefficient(0, 48000), 0.0);
  const mythos::ReverbSend closet = mythos::ReverbForRoom(4);
  const mythos::ReverbSend hall = mythos::ReverbForRoom(300);
  CHECK(hall.send > closet.send && hall.decay_seconds > closet.decay_seconds);
  CHECK(mythos::ReverbForRoom(100000).send <= 0.5);
}

TEST(spatial_mixer_groups_duck_and_release) {
  mythos::MixerGroups g;
  CHECK_EQ(g.Gain("music"), 1.0);
  g.SetVolume("music", 0.8);
  CHECK_EQ(g.Gain("music"), 0.8);
  g.AddDuck("music", "voice", 0.25, 0.1, 0.5);
  g.Update(0.1);
  CHECK_EQ(g.Gain("music"), 0.8);  // nobody talking
  g.SetActive("voice", true);
  g.Update(0.05);
  const double half = g.Gain("music");
  CHECK(half < 0.8 && half > 0.8 * 0.25);
  g.Update(0.1);
  CHECK(std::fabs(g.Gain("music") - 0.8 * 0.25) < 1e-9);
  g.SetActive("voice", false);
  g.Update(0.25);
  CHECK(g.Gain("music") > 0.8 * 0.25 && g.Gain("music") < 0.8);
  g.Update(0.5);
  CHECK(std::fabs(g.Gain("music") - 0.8) < 1e-9);
  CHECK_EQ(g.Gain("effects"), 1.0);  // untouched groups stay full
  // ClearActive is what the mixer does each block before marking the groups
  // that are playing: a duck releases when its trigger stops.
  g.SetActive("voice", true);
  g.Update(0.2);
  CHECK(g.Gain("music") < 0.8 * 0.3);
  g.ClearActive();
  g.Update(1.0);
  CHECK(std::fabs(g.Gain("music") - 0.8) < 1e-9);
}

TEST(sound_path_counts_walls_and_closed_doors) {
  mythos::Level l;
  CHECK(l.Parse(
      {"#########", "#...#...#", "#...N...#", "#...#...#", "#########"}));
  auto tiles = std::make_shared<mythos::TileSet>();
  (*tiles)['N'].solid = true;
  (*tiles)['N'].door = mythos::DoorKind::kClosed;
  l.SetTiles(tiles);
  // Same room: nothing between.
  mythos::SoundBlockers b =
      mythos::CountSoundBlockers(l, {1.5, 1.5}, {3.5, 1.5});
  CHECK(b.walls == 0 && b.closed_doors == 0);
  // Across the wall (row 1): one wall.
  b = mythos::CountSoundBlockers(l, {3.5, 1.5}, {5.5, 1.5});
  CHECK(b.walls == 1 && b.closed_doors == 0);
  // Through the closed door (row 2): one door, no wall.
  b = mythos::CountSoundBlockers(l, {3.5, 2.5}, {5.5, 2.5});
  CHECK(b.walls == 0 && b.closed_doors == 1);
  // Standing in the doorway is not behind it.
  b = mythos::CountSoundBlockers(l, {4.5, 2.5}, {6.5, 2.5});
  CHECK(b.walls == 0 && b.closed_doors == 0);
  // Across the middle wall into the far room: still one wall.
  b = mythos::CountSoundBlockers(l, {1.5, 1.5}, {7.5, 1.5});
  CHECK_EQ(b.walls, 1);
}

TEST(sky_follows_the_day_and_matches_the_old_night) {
  const mythos::SkyLook night = mythos::NightLook();
  const mythos::SkyLook midnight = mythos::SkyAt(0);
  CHECK(midnight.light_color == night.light_color);
  CHECK(midnight.ambient == night.ambient && midnight.zenith == night.zenith);
  CHECK(midnight.sun == 0 && midnight.stars == 1);
  const mythos::SkyLook noon = mythos::SkyAt(12);
  CHECK(noon.sun == 1 && noon.stars == 0);
  CHECK(noon.light_dir[1] > 0.9f);
  CHECK(noon.light_color[0] > 2.0f && noon.zenith[2] > 0.5f);
  // Morning and evening are warm: more red than blue in the light.
  const mythos::SkyLook dawn = mythos::SkyAt(6.5);
  CHECK(dawn.light_color[0] > dawn.light_color[2] * 2.0f);
  CHECK(mythos::SkyAt(7).light_dir[1] < noon.light_dir[1]);
  // The sun is in the east in the morning and the west in the evening.
  CHECK(mythos::SkyAt(8).light_dir[0] > 0 &&
        mythos::SkyAt(16).light_dir[0] < 0);
  // Wraps, and every direction is a unit vector.
  CHECK(mythos::SkyAt(25).light_color == mythos::SkyAt(1).light_color);
  CHECK(mythos::SkyAt(-1).light_color == mythos::SkyAt(23).light_color);
  for (double h = 0; h < 24; h += 0.25) {
    const auto d = mythos::SkyAt(h).light_dir;
    CHECK(std::fabs(d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - 1.0f) < 1e-3f);
  }
  // Continuous: no frame-to-frame jump in light across the whole day.
  for (double h = 0; h < 24; h += 0.01) {
    const auto a = mythos::SkyAt(h).light_color;
    const auto b = mythos::SkyAt(h + 0.01).light_color;
    for (size_t i = 0; i < 3; ++i) CHECK(std::fabs(a[i] - b[i]) < 0.05f);
  }
}

TEST(lod_plan_culls_swaps_and_sorts) {
  mythos::ModelLods lods;
  lods.Set(5, 6, 10.0);
  CHECK_EQ(lods.Choose(5, 3.0), 5);
  CHECK_EQ(lods.Choose(5, 10.0), 5);  // at the distance still near
  CHECK_EQ(lods.Choose(5, 10.5), 6);
  CHECK_EQ(lods.Choose(7, 99.0), 7);  // no entry: unchanged
  // A chain and a loop.
  lods.Set(6, 8, 30.0);
  CHECK_EQ(lods.Choose(5, 20.0), 6);
  CHECK_EQ(lods.Choose(5, 40.0), 8);
  lods.Set(8, 5, 0.0);
  const int looped = lods.Choose(5, 40.0);  // ends: no hang
  CHECK(looped == 5 || looped == 6 || looped == 8);
  lods.Clear();
  CHECK_EQ(lods.Choose(5, 99.0), 5);

  mythos::ModelLods table;
  table.Set(2, 3, 5.0);
  const std::vector<mythos::LodRequest> requests = {
      {2, 1.0, 0},    // 0: near model 2
      {1, 2.0, 0},    // 1: model 1
      {2, 9.0, 0},    // 2: far, becomes 3
      {4, 50.0, 20},  // 3: culled
      {-1, 0, 0},     // 4: no model
      {1, 3.0, 0},    // 5: model 1 again
      {4, 10.0, 20},  // 6: within its limit
  };
  const std::vector<mythos::LodDraw> plan = mythos::PlanDraws(requests, table);
  CHECK_EQ(plan.size(), 5u);
  const std::vector<size_t> order = {1, 5, 0, 2, 6};
  const std::vector<int> models = {1, 1, 2, 3, 4};
  for (size_t i = 0; i < plan.size(); ++i) {
    CHECK_EQ(plan[i].request, order[i]);  // ties keep the order given
    CHECK_EQ(plan[i].model, models[i]);
  }
}

TEST(file_watcher_reports_added_changed_and_removed) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "mythos_watch_test";
  fs::remove_all(dir);
  fs::create_directories(dir / "sub");
  CHECK(mythos::WriteFileAtomic((dir / "a.txt").string(), "one"));
  mythos::FileWatcher watcher(0.0);
  CHECK(!watcher.Add((dir / "nope").string()));
  CHECK(watcher.Add(dir.string()));
  // What was there at the start is the baseline, not a change.
  CHECK(watcher.Poll(0.1).empty());
  // A change in content (size differs, so the clock's grain does not matter).
  CHECK(mythos::WriteFileAtomic((dir / "a.txt").string(), "longer text"));
  std::vector<std::string> changed = watcher.Poll(0.1);
  CHECK_EQ(changed.size(), 1u);
  CHECK(!changed.empty() && changed[0] == (dir / "a.txt").string());
  CHECK(watcher.Poll(0.1).empty());
  // A new file in a sub folder, then its removal.
  CHECK(mythos::WriteFileAtomic((dir / "sub" / "b.txt").string(), "b"));
  changed = watcher.Poll(0.1);
  CHECK(changed.size() == 1 && changed[0] == (dir / "sub" / "b.txt").string());
  fs::remove(dir / "sub" / "b.txt");
  changed = watcher.Poll(0.1);
  CHECK(changed.size() == 1 && changed[0] == (dir / "sub" / "b.txt").string());
  // The interval holds scans back.
  mythos::FileWatcher slow(1.0);
  CHECK(slow.Add(dir.string()));
  CHECK(mythos::WriteFileAtomic((dir / "c.txt").string(), "c"));
  CHECK(slow.Poll(0.4).empty());
  CHECK_EQ(slow.Poll(0.7).size(), 1u);
  fs::remove_all(dir);
}

namespace {
mythos::editor::Item MakeItem(int x, int y, const std::string& model,
                              double yaw = 0, double ox = 0, double oz = 0) {
  mythos::editor::Item i;
  i.x = x;
  i.y = y;
  i.model = model;
  i.yaw = yaw;
  i.off_x = ox;
  i.off_z = oz;
  return i;
}
}

TEST(prefab_round_trips_through_its_file_format) {
  mythos::editor::Prefab p;
  p.name = "dining";
  p.width = 3;
  p.height = 2;
  p.furniture = {MakeItem(1, 0, "table"),
                 MakeItem(0, 1, "chair", 90, 0.1, -0.2)};
  p.decor = {MakeItem(2, 1, "vase", 45, 0.25, 0.0)};
  const std::string text = mythos::editor::SerializePrefab(p);
  mythos::editor::Prefab back;
  std::string error;
  CHECK(mythos::editor::ParsePrefab(text, &back, &error));
  back.name = "dining";
  CHECK(back == p);
  // No [size]: taken from the items. Bad input is refused and changes nothing.
  CHECK(
      mythos::editor::ParsePrefab("[furniture]\n2 1 table 0\n", &back, &error));
  CHECK(back.width == 3 && back.height == 2);
  mythos::editor::Prefab kept = back;
  CHECK(!mythos::editor::ParsePrefab("[furniture]\nx y\n", &back, &error));
  CHECK(!mythos::editor::ParsePrefab("[size]\n1 1\n[decor]\n0 0 vase 0\n",
                                     &back, &error));
  CHECK(!mythos::editor::ParsePrefab("[size]\n1 1\n[furniture]\n5 0 table 0\n",
                                     &back, &error));
  CHECK(!mythos::editor::ParsePrefab("[map]\n#\n", &back, &error));
  CHECK(back == kept);
}

TEST(prefab_rotates_cells_yaw_and_offsets) {
  mythos::editor::Prefab p;
  p.width = 3;
  p.height = 2;
  p.furniture = {MakeItem(0, 0, "a", 0, 0.1, 0.2)};  // top-left, facing south
  const mythos::editor::Prefab r1 = mythos::editor::Rotated(p, 1);
  CHECK(r1.width == 2 && r1.height == 3);
  // The top-left cell goes to the top-right after a clockwise quarter turn;
  // south-facing now faces west (yaw 270); offsets turn with it.
  CHECK(r1.furniture[0].x == 1 && r1.furniture[0].y == 0);
  CHECK(r1.furniture[0].yaw == 270);
  CHECK(std::fabs(r1.furniture[0].off_x + 0.2) < 1e-9 &&
        std::fabs(r1.furniture[0].off_z - 0.1) < 1e-9);
  // Four turns are the identity; turning back is the inverse.
  CHECK(mythos::editor::Rotated(p, 4) == p);
  CHECK(mythos::editor::Rotated(mythos::editor::Rotated(p, 1), -1) == p);
  CHECK(mythos::editor::Rotated(p, 5) == r1);
  const mythos::editor::Prefab r2 = mythos::editor::Rotated(p, 2);
  CHECK(r2.width == 3 && r2.furniture[0].x == 2 && r2.furniture[0].y == 1);
  CHECK(r2.furniture[0].yaw == 180);
}

TEST(prefab_captures_and_places_with_checks) {
  mythos::editor::LevelDoc doc;
  doc.rows = {"#####", "#...#", "#...#", "#####"};
  doc.furniture = {MakeItem(1, 1, "table"), MakeItem(2, 1, "chair", 90)};
  doc.decor = {MakeItem(1, 2, "vase", 0, 0.1, 0)};
  // Corners in any order; the rectangle is clamped to the map.
  mythos::editor::Prefab p =
      mythos::editor::CapturePrefab(doc, 2, 2, 1, 1, "set");
  CHECK(p.width == 2 && p.height == 2 && p.name == "set");
  CHECK(p.furniture.size() == 2 && p.decor.size() == 1);
  CHECK(p.furniture[0].x == 0 && p.furniture[0].y == 0);
  CHECK(mythos::editor::CapturePrefab(doc, 50, 50, 60, 60, "x")
            .furniture.empty());

  // Place it in the other half of the room, over the walls and on top of
  // existing furniture.
  const auto wall = [](char c) { return c == '#'; };
  mythos::editor::PlaceResult r =
      mythos::editor::PlacePrefab(doc, p, 3, 2, 0, wall);
  // (3,2) is floor, (4,2) a wall, (3,3) and (4,3) wall/outside: the table goes
  // in, the chair (4,2) is refused; the vase at (3,3) is inside the map.
  CHECK(r.furniture == 1 && r.decor == 1 && r.skipped == 1);
  // The same spot twice: the table's cell is taken now.
  r = mythos::editor::PlacePrefab(doc, p, 3, 2, 0, wall);
  CHECK(r.furniture == 0);
  // Off the map entirely.
  r = mythos::editor::PlacePrefab(doc, p, 40, 40);
  CHECK(r.furniture == 0 && r.decor == 0 && r.skipped == 3);
  CHECK(mythos::editor::SafePrefabName("my set/1") == "my_set_1");
  CHECK(mythos::editor::SafePrefabName("") == "prefab");
}

namespace {
mythos::Cutscene TestScene() {
  mythos::Cutscene s;
  s.name = "intro";
  s.camera = {{0, {0, 2, 0}, 0, 0, 1, mythos::Ease::kLinear},
              {2, {4, 2, 0}, 1.0, 0.2, 1.2, mythos::Ease::kLinear}};
  s.cues = {{1.0, {"message", "intro.line one"}},
            {0.0, {"set_flag", "begun"}},
            {1.5, {"set_flag", "half"}}};
  s.values = {
      {"fade", {{0, 1, mythos::Ease::kLinear}, {1, 0, mythos::Ease::kLinear}}}};
  s.anims = {{1.0, "guide", "Wave", 0.3}};
  return s;
}
}

TEST(cutscene_file_round_trips_and_sorts) {
  const mythos::Cutscene scene = TestScene();
  mythos::Cutscene back;
  std::string error;
  CHECK(mythos::ParseCutscene(mythos::SerializeCutscene(scene), &back, &error));
  // Keys come back sorted by time; the name is not in the file.
  mythos::Cutscene sorted = scene;
  std::ranges::stable_sort(sorted.cues, [](const auto& a, const auto& b) {
    return a.time < b.time;
  });
  back.name = "intro";
  CHECK(back == sorted);
  CHECK(back.cues[1].action.argument == "intro.line one");  // spaces kept
  CHECK_EQ(mythos::CutsceneLength(back), 2.0);
  // Bad input is refused and changes nothing.
  mythos::Cutscene kept = back;
  CHECK(!mythos::ParseCutscene("[camera]\n0 1 2\n", &back, &error));
  CHECK(!mythos::ParseCutscene("[camera]\n0 0 0 0 0 0 1 wobble\n", &back,
                               &error));
  CHECK(!mythos::ParseCutscene("[sound]\n", &back, &error));
  CHECK(!mythos::ParseCutscene("duration -1\n", &back, &error));
  CHECK(back == kept);
  mythos::Ease e;
  CHECK(mythos::ParseEase("out_back", &e) && e == mythos::Ease::kOutBack);
  for (int i = 0; i < mythos::kEaseCount; ++i) {
    CHECK(
        mythos::ParseEase(mythos::EaseName(static_cast<mythos::Ease>(i)), &e));
    CHECK_EQ(static_cast<int>(e), i);
  }
}

TEST(cutscene_player_fires_cues_once_in_order_and_samples) {
  const mythos::Cutscene scene = TestScene();
  mythos::CutscenePlayer player;
  CHECK(!player.Playing() && !player.Finished());
  player.Start(&scene);
  std::vector<std::string> fired, anims;
  const auto cue = [&](const mythos::EventAction& a) {
    fired.push_back(a.type + ":" + a.argument);
  };
  const auto anim = [&](const mythos::AnimKey& k) {
    anims.push_back(k.actor + ":" + k.clip);
  };
  player.Update(0.0, cue, anim);  // the key at 0 fires at once
  CHECK(fired.size() == 1 && fired[0] == "set_flag:begun");
  player.Update(0.5, cue, anim);
  CHECK_EQ(fired.size(), 1u);
  mythos::CameraSample cam;
  CHECK(player.Camera(&cam));
  CHECK(std::fabs(cam.pos.x - 1.0) < 1e-9 && std::fabs(cam.yaw - 0.25) < 1e-9);
  CHECK(std::fabs(player.Value("fade") - 0.5) < 1e-9);
  CHECK_EQ(player.Value("nope", 7), 7.0);
  player.Update(0.6, cue, anim);  // t = 1.1: the line and the wave
  CHECK(fired.size() == 2 && fired[1] == "message:intro.line one");
  CHECK(anims.size() == 1 && anims[0] == "guide:Wave");
  player.Update(0.1, cue, anim);
  CHECK_EQ(fired.size(), 2u);  // nothing fires twice
  CHECK(player.Playing());
  player.Update(5.0, cue, anim);  // past the end
  CHECK(player.Finished() && !player.Playing());
  CHECK(fired.size() == 3 && fired[2] == "set_flag:half");
  CHECK_EQ(player.Time(), 2.0);
  CHECK(player.Camera(&cam) && cam.pos.x == 4.0 && cam.fov_scale == 1.2);
  CHECK_EQ(player.Value("fade"), 0.0);
  player.Update(1.0, cue, anim);  // finished: no more
  CHECK_EQ(fired.size(), 3u);
}

TEST(cutscene_skip_fires_the_rest_and_yaw_turns_the_short_way) {
  mythos::Cutscene scene = TestScene();
  mythos::CutscenePlayer player;
  player.Start(&scene);
  std::vector<std::string> fired;
  const auto cue = [&](const mythos::EventAction& a) {
    fired.push_back(a.argument);
  };
  player.Update(0.2, cue, {});
  player.Skip(cue, {});
  CHECK(player.Finished());
  CHECK_EQ(fired.size(), 3u);  // all of them, once
  player.Skip(cue, {});
  CHECK_EQ(fired.size(), 3u);
  // 350 degrees to 10 degrees goes through 0, not back round.
  mythos::Cutscene turn;
  turn.camera = {{0, {0, 0, 0}, 6.1086, 0, 1, mythos::Ease::kLinear},
                 {1, {0, 0, 0}, 0.1745, 0, 1, mythos::Ease::kLinear}};
  mythos::CutscenePlayer p2;
  p2.Start(&turn);
  p2.Update(0.5, {}, {});
  mythos::CameraSample c;
  CHECK(p2.Camera(&c));
  CHECK(std::fabs(std::remainder(c.yaw, 2 * mythos::kPi)) < 0.01);
  // A scene with no camera keys has no camera.
  mythos::Cutscene none;
  none.duration = 1;
  mythos::CutscenePlayer p3;
  p3.Start(&none);
  CHECK(!p3.Camera(&c));
  p3.Update(2.0, {}, {});
  CHECK(p3.Finished());
}

TEST(settings_ambient_occlusion_is_off_by_default_and_persists) {
  GameSettings s;
  CHECK(!s.ambient_occlusion);
  const auto& rows = SettingsPageItems(SettingsPage::kGraphics);
  CHECK(std::ranges::find(rows, kSetAmbientOcclusion) != rows.end());
  s.ambient_occlusion = true;
  GameSettings back;
  ApplyKeyValues(back, ToKeyValues(s));
  CHECK(back.ambient_occlusion);
  // An old settings file without the key keeps the default.
  GameSettings old;
  ApplyKeyValues(old, {{"filmGrain", "0"}});
  CHECK(!old.ambient_occlusion);
  CHECK(!Tr("settings.ambientOcclusion").empty() &&
        Tr("settings.ambientOcclusion") != "settings.ambientOcclusion");
}

TEST(controls_page_rebinds_resets_and_leaves) {
  mythos::InputMap map = mythos::DefaultGameplayMap();
  const std::string ctx = mythos::kPlayContext;
  const int actions = static_cast<int>(map.Actions(ctx).size());
  CHECK_EQ(mythos::ControlsRowCount(map, ctx), actions + 2);
  mythos::ControlsState state;
  mythos::UiInput none;
  using mythos::ControlsResult;
  CHECK(mythos::UpdateControlsPage(map, ctx, state, none, std::nullopt) ==
        ControlsResult::kNone);
  // Down to the second action ("back"), confirm: it waits for a key.
  mythos::UiInput down;
  down.down_pressed = true;
  mythos::UpdateControlsPage(map, ctx, state, down, std::nullopt);
  CHECK_EQ(state.index, 1);
  mythos::UiInput confirm;
  confirm.confirm_pressed = true;
  // The key that confirmed (Enter) is in `pressed` that frame and is not taken.
  const mythos::Binding enter{mythos::BindKind::kKey, "Return", 0};
  CHECK(mythos::UpdateControlsPage(map, ctx, state, confirm, enter) ==
        ControlsResult::kNone);
  CHECK(state.capturing);
  CHECK(map.Bindings("back")[0].code == "S");
  // The next key is the binding; "E" belongs to interact, so it is taken from
  // it.
  const mythos::Binding e{mythos::BindKind::kKey, "E", 0};
  CHECK(mythos::UpdateControlsPage(map, ctx, state, none, e) ==
        ControlsResult::kChanged);
  CHECK(!state.capturing && map.Bindings("back")[0] == e);
  CHECK(state.note == "interact");
  CHECK(map.Bindings("interact").size() == 1 &&
        map.Bindings("interact")[0].code == "Space");
  // Escape cancels a capture without changing anything.
  state.capturing = true;
  mythos::UiInput esc;
  esc.pause_pressed = true;
  CHECK(mythos::UpdateControlsPage(map, ctx, state, esc, e) ==
        ControlsResult::kNone);
  CHECK(!state.capturing);
  // The second slot: right picks it.
  mythos::UiInput right;
  right.right_pressed = true;
  mythos::UpdateControlsPage(map, ctx, state, right, std::nullopt);
  CHECK_EQ(state.slot, 1);
  // Reset (the row after the actions) restores the defaults.
  state.index = actions;
  CHECK(mythos::UpdateControlsPage(map, ctx, state, confirm, std::nullopt) ==
        ControlsResult::kChanged);
  CHECK(map.Bindings("back")[0].code == "S" &&
        map.Bindings("interact").size() == 2);
  // Back row and Escape leave.
  state.index = actions + 1;
  CHECK(mythos::UpdateControlsPage(map, ctx, state, confirm, std::nullopt) ==
        ControlsResult::kBack);
  state.index = 0;
  CHECK(mythos::UpdateControlsPage(map, ctx, state, esc, std::nullopt) ==
        ControlsResult::kBack);
  CHECK(mythos::BindingLabel({mythos::BindKind::kPadButton, "South", 0}) ==
        "PAD SOUTH");
  CHECK(mythos::BindingLabel({mythos::BindKind::kPadAxis, "LeftX", -1}) ==
        "LEFTX -");
  CHECK(mythos::BindingLabel({mythos::BindKind::kMouseButton, "Left", 0}) ==
        "MOUSE LEFT");
}

TEST(controls_page_draws_rows_and_hotspots) {
  mythos::SdfFont font;
  std::string err;
  CHECK(font.LoadFile(
      std::string(MYTHOS_TEST_ASSET_DIR) + "/fonts/Alegreya.ttf", &err));
  mythos::MenuTheme theme;
  theme.heading = theme.item = theme.hint =
      theme.number = {&font, 12, 0xFFFFFFFF, {}};
  mythos::Surface fb(640, 360, 0);
  mythos::TextLayer layer;
  std::vector<mythos::Hotspot> hot;
  mythos::UiCanvas canvas{fb, layer, &hot};
  const mythos::InputMap map = mythos::DefaultGameplayMap();
  mythos::ControlsState state;
  mythos::DrawControlsPage(canvas, theme, map, mythos::kPlayContext, state, 0);
  CHECK(!layer.runs.empty());
  // Every action has a row hotspot and one per slot; reset and back one each.
  const size_t actions = map.Actions(mythos::kPlayContext).size();
  CHECK_EQ(hot.size(), actions * 3 + 2);
  // Capturing shows the prompt instead of the selected binding.
  state.capturing = true;
  layer.Clear();
  hot.clear();
  mythos::DrawControlsPage(canvas, theme, map, mythos::kPlayContext, state, 0);
  bool prompt = false;
  for (const auto& run : layer.runs) {
    prompt = prompt || run.text == mythos::Tr("controls.press");
  }
  CHECK(prompt);
}

TEST(reverb_adds_a_decaying_tail_and_does_nothing_when_off) {
  constexpr int kRate = 48000;
  const auto run = [&](double send, double decay, int seconds_listened,
                       std::vector<float>& tail_energy) {
    mythos::Reverb r;
    r.Prepare(kRate);
    r.Set(send, decay);
    std::vector<float> block(2 * 480, 0.0f);
    tail_energy.clear();
    // An impulse in the first block, then silence, in 10 ms blocks.
    for (int b = 0; b < seconds_listened * 100; ++b) {
      std::ranges::fill(block, 0.0f);
      if (b == 0) block[0] = block[1] = 1.0f;
      r.Process(block.data(), 480);
      float e = 0;
      for (const float v : block) {
        CHECK(std::isfinite(v));
        e += v * v;
      }
      tail_energy.push_back(e);
    }
  };
  std::vector<float> off, on, longer;
  run(0.0, 1.0, 2, off);
  CHECK_EQ(off[0], 2.0f);  // only the dry impulse: untouched
  for (size_t i = 1; i < off.size(); ++i) CHECK_EQ(off[i], 0.0f);
  run(0.5, 1.0, 2, on);
  // The tail is there after the dry sound, and falls away.
  float early = 0, late = 0;
  for (size_t i = 5; i < 25; ++i) early += on[i];
  for (size_t i = 150; i < 200; ++i) late += on[i];
  CHECK(early > 1e-4f);
  CHECK(late < early * 0.01f);
  // A longer decay rings longer.
  run(0.5, 3.0, 2, longer);
  float longer_mid = 0, on_mid = 0;
  for (size_t i = 60; i < 120; ++i) {
    longer_mid += longer[i];
    on_mid += on[i];
  }
  CHECK(longer_mid > on_mid * 2.0f);
  // Loud noise for a while stays finite and bounded.
  mythos::Reverb r;
  r.Prepare(kRate);
  r.Set(1.0, 10.0);
  std::vector<float> noise(2 * 480);
  uint32_t seed = 1;
  for (int b = 0; b < 300; ++b) {
    for (float& v : noise) {
      seed = seed * 1664525u + 1013904223u;
      v = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
    }
    r.Process(noise.data(), 480);
    for (const float v : noise) CHECK(std::isfinite(v) && std::fabs(v) < 20.0f);
  }
}

TEST(model_library_reloads_one_file_and_keeps_the_old_one_on_error) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "mythos_model_reload";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const std::string tri =
      "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\n";
  const std::string quad = tri + "f 1//1 3//1 2//1\n";
  const std::string path = (dir / "thing.obj").string();
  CHECK(mythos::WriteFileAtomic(path, tri));
  mythos::ModelLibrary lib;
  std::string error;
  CHECK(lib.LoadFile(path, &error));
  const mythos::Model* m = lib.Find("thing");
  CHECK(m != nullptr && m->batches[0].verts.size() == 3u);
  // Changed on disk: the new triangles replace the old.
  CHECK(mythos::WriteFileAtomic(path, quad));
  CHECK(lib.LoadFile(path, &error));
  CHECK(lib.Find("thing")->batches[0].verts.size() == 6u);
  // A broken file keeps what was there.
  CHECK(mythos::WriteFileAtomic(path, "f 1 2 3 4 five\n"));
  const bool bad = lib.LoadFile(path, &error);
  CHECK(!bad && !error.empty());
  CHECK(lib.Find("thing")->batches[0].verts.size() == 6u);
  CHECK(!lib.LoadFile((dir / "none.obj").string(), &error));
  fs::remove_all(dir);
}

TEST(probes_bake_light_with_walls_and_sample_smoothly) {
  mythos::Level l;
  CHECK(l.Parse(
      {"##########", "#...#....#", "#...#....#", "#........#", "##########"}));
  const mythos::ProbeLamp lamp{2.5, 2.0, 1.5, 1.0, 0.5, 0.25, 4.0};
  mythos::ProbeOptions options;
  options.spacing = 0.5;
  const mythos::ProbeGrid g = mythos::BakeProbes(l, {lamp}, options);
  CHECK(!g.Empty() && g.width == 20 && g.depth == 10);
  // Bright at the lamp, dimmer with distance, colour in the lamp's ratio.
  const auto near_lamp = g.Sample(2.4, 1.5);
  const auto farther = g.Sample(3.8, 1.5);
  CHECK(near_lamp[0] > farther[0] && farther[0] > 0.0f);
  CHECK(std::fabs(near_lamp[1] / near_lamp[0] - 0.5f) < 1e-3f);
  CHECK(std::fabs(near_lamp[2] / near_lamp[0] - 0.25f) < 1e-3f);
  // The wall at column 4 shadows the room beyond it (row 1); a lamp in the
  // row of the opening (row 3) lights through it.
  const auto behind_wall = g.Sample(6.0, 1.5);
  CHECK_EQ(behind_wall[0], 0.0f);
  const mythos::ProbeLamp low{2.5, 2.0, 3.5, 1.0, 1.0, 1.0, 4.0};
  CHECK(mythos::BakeProbes(l, {low}, options).Sample(5.5, 3.5)[0] > 0.0f);
  // Beyond the radius: none. A baked ambient is added everywhere open.
  CHECK_EQ(g.Sample(8.5, 1.5)[0], 0.0f);
  options.ambient = {0.1f, 0.1f, 0.1f};
  const mythos::ProbeGrid lit = mythos::BakeProbes(l, {lamp}, options);
  CHECK(std::fabs(lit.Sample(8.5, 1.5)[0] - 0.1f) < 1e-6f);
  // Probes inside walls are filled from their neighbours, so sampling at a
  // wall's face is not black.
  CHECK(g.Sample(4.1, 1.5)[0] > 0.0f);
  // Sampling is continuous: no jump between close points.
  for (double x = 1.1; x < 3.9; x += 0.05) {
    CHECK(std::fabs(g.Sample(x, 1.5)[0] - g.Sample(x + 0.05, 1.5)[0]) < 0.1f);
  }
  CHECK(mythos::ProbeGrid{}.Sample(1, 1)[0] == 0.0f);
  // The text file round-trips (to its printed precision) and refuses junk.
  mythos::ProbeGrid back;
  std::string error;
  CHECK(mythos::ParseProbes(mythos::SerializeProbes(g), &back, &error));
  CHECK(back.width == g.width && back.depth == g.depth);
  CHECK(std::fabs(back.Sample(2.4, 1.5)[0] - near_lamp[0]) < 1e-4f);
  mythos::ProbeGrid kept = back;
  CHECK(!mythos::ParseProbes("probes 0.5 2 2\n1 2 3\n", &back, &error));
  CHECK(!mythos::ParseProbes("nope", &back, &error));
  CHECK(back == kept);
}

namespace {
// A world point through a column-major matrix, divided by w.
void Project(const float* m, const float* p, float* out) {
  float v[4];
  for (int r = 0; r < 4; ++r) {
    v[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
  }
  for (int i = 0; i < 3; ++i) out[i] = v[i] / v[3];
}
}

TEST(sun_shadow_box_covers_the_viewer_and_does_not_shimmer) {
  const float eye[3] = {10.3f, 1.6f, 7.7f};
  const float sun[3] = {0.4f, 0.8f, 0.3f};
  const mythos::SunShadow s =
      mythos::ComputeSunShadow(eye, sun, 20.0f, 60.0f, 2048);
  CHECK(std::fabs(s.texel - 40.0f / 2048.0f) < 1e-6f);
  // The viewer is near the middle of the box (within a texel) and in front.
  float c[3];
  Project(s.view_proj, eye, c);
  CHECK(std::fabs(c[0]) < 0.01f && std::fabs(c[1]) < 0.01f);
  CHECK(c[2] > -1.0f && c[2] < 1.0f);
  // A point 15 m to the side is inside; 25 m outside; one toward the light is
  // nearer (smaller depth) than the viewer, one away is farther.
  const float side[3] = {eye[0] + 15.0f, eye[1], eye[2]};
  float p[3];
  Project(s.view_proj, side, p);
  CHECK(std::fabs(p[0]) <= 1.0f && std::fabs(p[1]) <= 1.0f);
  // 25 m along the light's own right axis, (0.6, 0, -0.8) for this sun.
  const float far_side[3] = {eye[0] + 15.0f, eye[1], eye[2] - 20.0f};
  float q[3];
  Project(s.view_proj, far_side, q);
  CHECK(std::fabs(q[0]) > 1.0f || std::fabs(q[1]) > 1.0f);
  const float toward[3] = {eye[0] + sun[0] * 5, eye[1] + sun[1] * 5,
                           eye[2] + sun[2] * 5};
  const float away[3] = {eye[0] - sun[0] * 5, eye[1] - sun[1] * 5,
                         eye[2] - sun[2] * 5};
  float t[3], a[3];
  Project(s.view_proj, toward, t);
  Project(s.view_proj, away, a);
  CHECK(t[2] < c[2] && c[2] < a[2]);
  // Moving the viewer less than a texel leaves the matrices as they were;
  // moving it by a texel or more changes them.
  float nudged[3] = {eye[0] + 0.001f, eye[1], eye[2]};
  const mythos::SunShadow same =
      mythos::ComputeSunShadow(nudged, sun, 20.0f, 60.0f, 2048);
  bool equal = true;
  for (int i = 0; i < 16; ++i)
    equal = equal && same.view_proj[i] == s.view_proj[i];
  CHECK(equal);
  float moved[3] = {eye[0] + 1.0f, eye[1], eye[2]};
  const mythos::SunShadow other =
      mythos::ComputeSunShadow(moved, sun, 20.0f, 60.0f, 2048);
  bool differs = false;
  for (int i = 0; i < 16; ++i)
    differs = differs || other.view_proj[i] != s.view_proj[i];
  CHECK(differs);
  // A light straight overhead and a zero vector still give finite matrices.
  const float overhead[3] = {0, 1, 0}, none[3] = {0, 0, 0};
  for (const float* dir : {overhead, none}) {
    const mythos::SunShadow o =
        mythos::ComputeSunShadow(eye, dir, 10.0f, 30.0f, 1024);
    for (const float v : o.view_proj) CHECK(std::isfinite(v));
  }
}

TEST(settings_menu_opens_the_controls_page_only_when_the_game_has_one) {
  GameSettings s;
  CHECK(!s.controls_page);
  CHECK(
      std::ranges::find(SettingsPageItems(SettingsPage::kMain), kSetControls) ==
      SettingsPageItems(SettingsPage::kMain).end());
  const auto& with = SettingsPageItems(SettingsPage::kMain, true);
  CHECK(std::ranges::find(with, kSetControls) != with.end());
  CHECK_EQ(with.back(), kSetBack);
  s.controls_page = true;
  SettingsPage page = SettingsPage::kMain;
  int index = kSetGraphics;
  UiInput down;
  down.down_pressed = true;
  UpdateSettingsPage(s, page, index, down, false, 2);  // audio
  UpdateSettingsPage(s, page, index, down, false, 2);  // controls
  CHECK_EQ(index, kSetControls);
  UiInput confirm;
  confirm.confirm_pressed = true;
  CHECK(!UpdateSettingsPage(s, page, index, confirm, false, 2));
  CHECK(page == SettingsPage::kControls);
  // The menu leaves the controls page alone; the game runs it, then returns.
  UiInput esc;
  esc.pause_pressed = true;
  CHECK(!UpdateSettingsPage(s, page, index, esc, false, 2));
  CHECK(page == SettingsPage::kControls);
  LeaveControlsPage(page, index);
  CHECK(page == SettingsPage::kMain && index == kSetControls);
  // Escape from the main page leaves the menu as before.
  CHECK(UpdateSettingsPage(s, page, index, esc, false, 2));
  // Without the flag a Controls row is not reachable.
  GameSettings plain;
  page = SettingsPage::kMain;
  index = kSetGraphics;
  for (int i = 0; i < 4; ++i)
    UpdateSettingsPage(plain, page, index, down, false, 2);
  CHECK(index != kSetControls);
  CHECK(!Tr("settings.controls").empty() &&
        Tr("settings.controls") != "settings.controls");
}

TEST(music_layers_fade_crossfade_and_go_silent) {
  mythos::LayerFader f;
  CHECK(f.Silent());
  CHECK_EQ(f.Gain("drums"), 0.0);
  f.Fade("pad", 1.0, 2.0);
  f.Update(1.0);
  CHECK(std::fabs(f.Gain("pad") - 0.5) < 1e-9);
  CHECK(f.Wanted("pad") && !f.Silent());
  f.Update(5.0);
  CHECK_EQ(f.Gain("pad"), 1.0);  // stops at the target
  f.Fade("drums", 0.6, 0.0);     // at once
  CHECK_EQ(f.Gain("drums"), 0.6);
  // A crossfade to a new set: the named layers up, the others down.
  f.Crossfade({"pad", "strings"}, 1.0);
  f.Update(0.5);
  CHECK(std::fabs(f.Gain("drums") - 0.3) < 1e-9);
  CHECK(std::fabs(f.Gain("strings") - 0.5) < 1e-9);
  CHECK_EQ(f.Gain("pad"), 1.0);
  f.Update(1.0);
  CHECK_EQ(f.Gain("drums"), 0.0);
  CHECK(!f.Wanted("drums") && f.Wanted("strings"));
  // Everything out: silent. Fades never overshoot or go negative.
  f.Crossfade({}, 0.5);
  f.Update(10.0);
  CHECK(f.Silent());
  CHECK_EQ(f.Gain("pad"), 0.0);
  f.Fade("pad", 5.0, 1.0);  // out of range is clamped
  f.Update(10.0);
  CHECK_EQ(f.Gain("pad"), 1.0);
  f.Clear();
  CHECK(f.Silent());
}

TEST(cutscene_sort_and_validate) {
  mythos::Cutscene s;
  CHECK(!mythos::ValidateCutscene(s).empty());  // nothing to play
  s.duration = 2;
  CHECK(mythos::ValidateCutscene(s).empty());
  s.camera = {{2, {}, 0, 0, 1, mythos::Ease::kLinear},
              {1, {}, 0, 0, 1, mythos::Ease::kLinear}};
  s.cues = {{1.5, {"b", ""}}, {0.5, {"a", ""}}, {0.5, {"c", ""}}};
  s.values = {
      {"fade", {{1, 1, mythos::Ease::kLinear}, {0, 0, mythos::Ease::kLinear}}}};
  s.anims = {{2, "x", "Walk", 0.2}, {1, "x", "Idle", 0.2}};
  mythos::SortCutscene(s);
  CHECK(s.camera[0].time == 1 && s.camera[1].time == 2);
  CHECK(s.cues[0].action.type == "a" &&
        s.cues[1].action.type == "c");  // stable
  CHECK(s.cues[2].action.type == "b");
  CHECK(s.values[0].keys[0].time == 0 && s.anims[0].clip == "Idle");
  CHECK(mythos::ValidateCutscene(s).empty());
  // Problems, one line each.
  s.cues.push_back({-1, {"", ""}});
  s.anims.push_back({0, "", "Wave", 0.2});
  s.values.push_back({"fade", {}});
  s.values.push_back({"", {}});
  s.camera[0].yaw = std::nan("");
  const std::vector<std::string> problems = mythos::ValidateCutscene(s);
  CHECK(problems.size() >= 6);
  mythos::Cutscene worse;
  worse.duration = -1;
  CHECK(!mythos::ValidateCutscene(worse).empty());
}

TEST(save_state_level_diff_records_and_restores_changed_cells) {
  mythos::Level base;
  CHECK(base.Parse({"#####", "#.N.#", "#####"}));
  mythos::Level now = base;
  CHECK(mythos::LevelDiffToJson(base, now).empty());
  now.Set(2, 1, 'n');  // a door opened
  now.Set(1, 1, 'x');
  const mythos::json::Value diff = mythos::LevelDiffToJson(base, now);
  CHECK_EQ(diff.size(), 2u);
  mythos::Level fresh = base;
  CHECK_EQ(mythos::ApplyLevelDiff(fresh, diff), 2);
  CHECK(fresh.At(2, 1) == 'n' && fresh.At(1, 1) == 'x');
  // Junk is skipped: wrong shapes, out of range cells, long glyphs.
  const mythos::json::Value junk = {
      {9, 9, "z"}, {1, 1, "toolong"}, {"a", 1, "z"}, 5};
  CHECK_EQ(mythos::ApplyLevelDiff(fresh, junk), 0);
  CHECK_EQ(mythos::ApplyLevelDiff(fresh, mythos::json::Value(3)), 0);
  CHECK(fresh.At(1, 1) == 'x');
}

TEST(nav_steer_around_bends_the_way_from_a_mover_in_the_path) {
  const mythos::Vec2 pos{0, 0};
  const mythos::Vec2 east{1, 0};
  // Nothing in the way: straight on.
  mythos::Vec2 d = mythos::SteerAround(pos, {5, 0}, 0.3, 1.5, {});
  CHECK(std::fabs(d.x - 1.0) < 1e-9 && std::fabs(d.y) < 1e-9);
  // Something far off to the side does nothing.
  d = mythos::SteerAround(pos, east, 0.3, 1.5, {{{2, 3}, {0, 0}, 0.3}});
  CHECK(std::fabs(d.y) < 1e-9);
  // A stopper just ahead and slightly north: bend south.
  d = mythos::SteerAround(pos, east, 0.3, 1.5, {{{1.5, 0.2}, {0, 0}, 0.3}});
  CHECK(d.y < -0.05 && d.x > 0);
  // The same one slightly south: bend north.
  d = mythos::SteerAround(pos, east, 0.3, 1.5, {{{1.5, -0.2}, {0, 0}, 0.3}});
  CHECK(d.y > 0.05);
  // Coming head on, dead centre: still bends (to the left), unit length.
  d = mythos::SteerAround(pos, east, 0.3, 1.5, {{{3, 0}, {-1.5, 0}, 0.3}});
  CHECK(std::fabs(d.y) > 0.05);
  CHECK(std::fabs(std::hypot(d.x, d.y) - 1.0) < 1e-9);
  CHECK_EQ(mythos::SteerAround(pos, {0, 0}, 0.3, 1.5, {}).x, 0.0);
}

TEST(music_tempo_map_finds_bar_and_phrase_ends) {
  // 120 bpm 4/4 (2 s bars) for 6 s, then 60 bpm 4/4 (4 s bars).
  const std::vector<mythos::TempoSection> map = {{0.0, 120, 4}, {6.0, 60, 4}};
  CHECK(std::fabs(mythos::NextBarTime(map, 0.5) - 2.0) < 1e-9);
  CHECK(std::fabs(mythos::NextBarTime(map, 5.0) - 6.0) < 1e-9);
  CHECK(std::fabs(mythos::NextBarTime(map, 6.0) - 6.0) < 1e-9);
  CHECK(std::fabs(mythos::NextBarTime(map, 6.5) - 10.0) < 1e-9);
  // Two-bar phrases: 4 s long in the first section, cut by the change at 6.
  CHECK(std::fabs(mythos::NextPhraseTime(map, 4.5, 2) - 6.0) < 1e-9);
  CHECK(std::fabs(mythos::NextPhraseTime(map, 1.0, 2) - 4.0) < 1e-9);
  CHECK(std::fabs(mythos::NextPhraseTime(map, 7.0, 2) - 14.0) < 1e-9);
  CHECK_EQ(mythos::NextBarTime({}, 3.0), 3.0);
}

TEST(save_state_furniture_diff_records_removed_and_added_pieces) {
  mythos::Level base;
  CHECK(base.Parse({"#####", "#...#", "#####"}));
  mythos::Placement vase;
  vase.x = 1;
  vase.y = 1;
  vase.model = "vase";
  vase.id = "vase1";
  CHECK(base.AddFurniture(vase));
  mythos::Level now = base;
  CHECK_EQ(
      mythos::ApplyFurnitureDiff(now, mythos::FurnitureDiffToJson(base, now)),
      0);
  CHECK(now.RemovePlacement("vase1"));  // taken
  mythos::Placement crate;
  crate.x = 3;
  crate.y = 1;
  crate.model = "crate";
  CHECK(now.AddFurniture(crate));  // put down
  const mythos::json::Value diff = mythos::FurnitureDiffToJson(base, now);
  CHECK_EQ(diff["removed"].size(), 1u);
  CHECK_EQ(diff["added"].size(), 1u);
  mythos::Level fresh = base;
  CHECK_EQ(mythos::ApplyFurnitureDiff(fresh, diff), 2);
  CHECK(fresh.FurnitureAt(1, 1) == nullptr);
  CHECK(fresh.FurnitureAt(3, 1) != nullptr);
  CHECK_EQ(mythos::ApplyFurnitureDiff(fresh, mythos::json::Value(3)), 0);
}

TEST(settings_keep_the_key_bindings_on_one_line) {
  GameSettings s;
  CHECK(s.bindings.empty());
  // Default: no key is written with a value that matters, and an old file
  // without it keeps the default.
  GameSettings old;
  ApplyKeyValues(old, {{"filmGrain", "0"}});
  CHECK(old.bindings.empty());
  s.bindings = "{\"forward\":[\"key:I\"]}";
  GameSettings back;
  ApplyKeyValues(back, ToKeyValues(s));
  CHECK(back.bindings == s.bindings);
  // The file format is key=value lines: a value with '=' survives, a newline
  // is cut at the first one.
  GameSettings eq;
  ApplyKeyValues(eq, {{"bindings", "a=b"}});
  CHECK(eq.bindings == "a=b");
  GameSettings nl;
  ApplyKeyValues(nl, {{"bindings", "one\ntwo"}});
  CHECK(nl.bindings == "one");
  // The bindings round trip through the input map.
  InputMap map = DefaultGameplayMap();
  map.Rebind("forward", 0, {BindKind::kKey, "I", 0}, true);
  s.bindings = map.ToJson().dump();
  InputMap restored = DefaultGameplayMap();
  json::Value parsed;
  CHECK(json::Parse(s.bindings, parsed) && restored.FromJson(parsed));
  CHECK(restored.Bindings("forward")[0].code == "I");
}

TEST(ai_noise_board_last_seen_and_smoothed_paths) {
  mythos::Level l;
  CHECK(l.Parse(
      {"#########", "#...#...#", "#...#...#", "#.......#", "#########"}));
  // Noises: heard when near and in range, older ones are forgotten, the
  // loudest left after the distance wins.
  mythos::ai::NoiseBoard board(3.0);
  board.Add({{6.5, 3.5}, 6.0}, 0.0);
  board.Add({{2.5, 1.5}, 3.0}, 0.0);
  const mythos::Vec2 ear{7.5, 3.5};
  auto heard = board.Loudest(l, ear, 1.0, 1.0);
  CHECK(heard && heard->pos.x == 6.5);
  // The quiet one in the other room is out of range: sensitivity brings it in.
  CHECK(!mythos::ai::CanHear(l, ear, 1.0, {{2.5, 1.5}, 3.0}));
  CHECK(!board.Loudest(l, {1.5, 1.5}, 0.1, 1.0));
  // Forgotten after the lifetime.
  CHECK(!board.Loudest(l, ear, 1.0, 5.0));
  board.Add({{7.5, 3.5}, 2.0}, 5.0);  // also clears the old ones
  CHECK_EQ(board.Size(), 1u);
  board.Clear();
  CHECK_EQ(board.Size(), 0u);

  mythos::ai::LastSeen seen;
  CHECK(!seen.Known() && !seen.Remembers(0, 5) && std::isinf(seen.Age(1)));
  seen.Saw({3, 4}, 10.0);
  CHECK(seen.Known() && seen.Where().x == 3);
  CHECK(seen.Remembers(12.0, 5.0) && !seen.Remembers(16.0, 5.0));
  CHECK_EQ(seen.Age(12.5), 2.5);
  seen.Forget();
  CHECK(!seen.Known());

  // Paths pulled taut: a straight run across an open room is two points; round
  // a wall the corner stays.
  mythos::Level open;
  CHECK(open.Parse({"#######", "#.....#", "#.....#", "#.....#", "#######"}));
  const auto path = mythos::FindPath(open, {1, 1}, {5, 3});
  CHECK(path.size() > 3);
  const auto smooth = mythos::SmoothPath(open, path);
  CHECK_EQ(smooth.size(), 2u);
  CHECK(smooth.front().x == 1.5 && smooth.back().x == 5.5);
  const auto around = mythos::FindPath(l, {1, 1}, {7, 1});
  const auto bent = mythos::SmoothPath(l, around);
  CHECK(bent.size() >= 3 && bent.size() < around.size());
  CHECK(mythos::SmoothPath(l, {}).empty());
  CHECK_EQ(mythos::SmoothPath(l, {{1, 1}}).size(), 1u);
}

namespace {
// The JSON and the binary chunk of a glb.
void SplitGlb(const std::string& glb, std::string* json, std::string* bin) {
  size_t at = 12;
  while (at + 8 <= glb.size()) {
    uint32_t len = 0, type = 0;
    std::memcpy(&len, glb.data() + at, 4);
    std::memcpy(&type, glb.data() + at + 4, 4);
    (type == 0x4E4F534A ? *json : *bin) = glb.substr(at + 8, len);
    at += 8 + len;
  }
}

std::string Base64(const std::string& in) {
  static const char* kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const uint32_t v = (static_cast<uint8_t>(in[i]) << 16) |
                       (static_cast<uint8_t>(in[i + 1]) << 8) |
                       static_cast<uint8_t>(in[i + 2]);
    for (int s = 18; s >= 0; s -= 6) out += kAlphabet[(v >> s) & 63];
  }
  if (i < in.size()) {
    uint32_t v = static_cast<uint32_t>(static_cast<uint8_t>(in[i])) << 16;
    if (i + 1 < in.size())
      v |= static_cast<uint32_t>(static_cast<uint8_t>(in[i + 1])) << 8;
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += i + 1 < in.size() ? kAlphabet[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}
}

TEST(gltf_text_files_with_data_uri_external_and_sparse_buffers) {
  std::string json_text, bin;
  SplitGlb(TinyGlb(), &json_text, &bin);
  mythos::json::Value j;
  CHECK(mythos::json::Parse(json_text, j));
  mythos::GltfModel reference;
  CHECK(mythos::ParseGlb(TinyGlb(), reference));

  // A base64 data URI buffer.
  mythos::json::Value inline_doc = j;
  inline_doc["buffers"] = {
      {{"byteLength", bin.size()},
       {"uri", "data:application/octet-stream;base64," + Base64(bin)}}};
  mythos::GltfModel a;
  std::string error;
  CHECK(mythos::ParseGltf(inline_doc.dump(), ".", a, &error));
  CHECK(error.empty());
  CHECK(a.skeleton.Count() == 2 && a.batches[0].verts.size() == 3u);
  CHECK(a.batches[0].verts[1].px == reference.batches[0].verts[1].px);

  // An external .bin beside the .gltf, and two buffers (the bin split in two).
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "mythos_gltf_test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  CHECK(
      mythos::WriteFileAtomic((dir / "first.bin").string(), bin.substr(0, 48)));
  CHECK(mythos::WriteFileAtomic((dir / "second part.bin").string(),
                                bin.substr(48)));
  mythos::json::Value two = j;
  two["buffers"] = {
      {{"byteLength", 48}, {"uri", "first.bin"}},
      {{"byteLength", bin.size() - 48}, {"uri", "second%20part.bin"}}};
  // Views at 48 and beyond move to the second buffer.
  for (auto& view : two["bufferViews"]) {
    const size_t off = view["byteOffset"].get<size_t>();
    if (off >= 48) {
      view["buffer"] = 1;
      view["byteOffset"] = off - 48;
    }
  }
  CHECK(mythos::WriteFileAtomic((dir / "m.gltf").string(), two.dump()));
  mythos::GltfModel b;
  CHECK(mythos::LoadGltf((dir / "m.gltf").string(), b, &error));
  CHECK(b.FindClip("lift") != nullptr);
  CHECK(b.batches[0].skin[0].weights[0] ==
        reference.batches[0].skin[0].weights[0]);
  // A missing buffer file and a bad data URI are errors, not crashes.
  mythos::json::Value broken = j;
  broken["buffers"] = {{{"byteLength", 4}, {"uri", "nothere.bin"}}};
  CHECK(!mythos::ParseGltf(broken.dump(), dir.string(), b, &error));
  broken["buffers"] = {{{"byteLength", 4}, {"uri", "data:text/plain,hello"}}};
  CHECK(!mythos::ParseGltf(broken.dump(), dir.string(), b, &error));
  CHECK(!mythos::ParseGltf("{", dir.string(), b, &error));
  CHECK(!mythos::LoadGltf((dir / "none.gltf").string(), b, &error));

  // A sparse accessor: positions start at zero and three values are replaced.
  std::string sparse_bin = bin;
  const size_t indices_at = sparse_bin.size();
  const uint8_t idx[3] = {0, 1, 2};  // unsigned bytes, padded to 4
  sparse_bin.append(reinterpret_cast<const char*>(idx), 3);
  sparse_bin.push_back('\0');
  const size_t values_at = sparse_bin.size();
  const float vals[9] = {0, 0, 0, 2, 0, 0, 0, 3, 0};  // x = 2 at 1, y = 3 at 2
  sparse_bin.append(reinterpret_cast<const char*>(vals), sizeof(vals));
  mythos::json::Value sparse_doc = j;
  sparse_doc["buffers"] = {
      {{"byteLength", sparse_bin.size()},
       {"uri", "data:application/octet-stream;base64," + Base64(sparse_bin)}}};
  sparse_doc["bufferViews"].push_back(
      {{"buffer", 0}, {"byteOffset", indices_at}, {"byteLength", 3}});
  sparse_doc["bufferViews"].push_back(
      {{"buffer", 0}, {"byteOffset", values_at}, {"byteLength", sizeof(vals)}});
  const size_t views = sparse_doc["bufferViews"].size();
  mythos::json::Value& pos = sparse_doc["accessors"][0];
  pos.erase("bufferView");
  pos["sparse"] = {
      {"count", 3},
      {"indices", {{"bufferView", views - 2}, {"componentType", 5121}}},
      {"values", {{"bufferView", views - 1}}}};
  mythos::GltfModel sp;
  CHECK(mythos::ParseGltf(sparse_doc.dump(), ".", sp, &error));
  CHECK(sp.batches[0].verts[1].px == 2.0f && sp.batches[0].verts[2].py == 3.0f);
  CHECK(sp.batches[0].verts[0].px == 0.0f);
  fs::remove_all(dir);
}

TEST(gltf_decodes_material_textures_and_flips_v) {
  std::string json_text, bin;
  SplitGlb(TinyGlb(), &json_text, &bin);
  mythos::json::Value j;
  CHECK(mythos::json::Parse(json_text, j));
  // A 2 x 2 24-bit BMP: bottom row red, green; top row blue, white.
  std::string bmp = "BM";
  const auto le32 = [&](uint32_t v) {
    bmp.append(reinterpret_cast<const char*>(&v), 4);
  };
  const auto le16 = [&](uint16_t v) {
    bmp.append(reinterpret_cast<const char*>(&v), 2);
  };
  le32(54 + 16);
  le32(0);
  le32(54);
  le32(40);
  le32(2);
  le32(2);
  le16(1);
  le16(24);
  le32(0);
  le32(16);
  le32(2835);
  le32(2835);
  le32(0);
  le32(0);
  const unsigned char rows[16] = {
      0,   0, 255, 0,   255, 0,   0, 0,   // red, green (BGR)
      255, 0, 0,   255, 255, 255, 0, 0};  // blue, white
  bmp.append(reinterpret_cast<const char*>(rows), 16);
  const size_t bmp_at = bin.size();
  bin += bmp;
  while (bin.size() % 4) bin.push_back('\0');
  const size_t uv_at = bin.size();
  const float uvs[6] = {0, 0, 1, 0, 0, 0.25f};
  bin.append(reinterpret_cast<const char*>(uvs), sizeof(uvs));
  j["buffers"] = {{{"byteLength", bin.size()}}};
  j["bufferViews"].push_back(
      {{"buffer", 0}, {"byteOffset", bmp_at}, {"byteLength", bmp.size()}});
  j["bufferViews"].push_back(
      {{"buffer", 0}, {"byteOffset", uv_at}, {"byteLength", sizeof(uvs)}});
  const size_t views = j["bufferViews"].size();
  j["images"] = {{{"bufferView", views - 2}, {"mimeType", "image/bmp"}}};
  j["textures"] = {{{"source", 0}}};
  j["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"] = {
      {"index", 0}};
  j["accessors"].push_back({{"bufferView", views - 1},
                            {"componentType", 5126},
                            {"count", 3},
                            {"type", "VEC2"}});
  j["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"] =
      j["accessors"].size() - 1;
  std::string text = j.dump();
  while (text.size() % 4) text += ' ';
  std::string glb = "glTF";
  const auto u32 = [&](uint32_t v) {
    glb.append(reinterpret_cast<const char*>(&v), 4);
  };
  u32(2);
  u32(static_cast<uint32_t>(12 + 8 + text.size() + 8 + bin.size()));
  u32(static_cast<uint32_t>(text.size()));
  u32(0x4E4F534A);
  glb += text;
  u32(static_cast<uint32_t>(bin.size()));
  u32(0x004E4942);
  glb += bin;

  mythos::GltfModel m;
  std::string error;
  CHECK(mythos::ParseGlb(glb, m, &error));
  CHECK(error.empty());
  CHECK_EQ(m.textures.size(), 1u);
  const auto it = m.textures.find("skin");
  CHECK(it != m.textures.end());
  if (it != m.textures.end()) {
    const mythos::Surface& s = it->second;
    CHECK(s.w == 2 && s.h == 2);
    // Surfaces are stored top row first.
    CHECK(s.At(0, 0) == mythos::Rgba(0, 0, 255) &&
          s.At(1, 0) == mythos::Rgba(255, 255, 255));
    CHECK(s.At(0, 1) == mythos::Rgba(255, 0, 0) &&
          s.At(1, 1) == mythos::Rgba(0, 255, 0));
  }
  // Texture coordinates come out with v flipped (the engine's v runs up).
  CHECK(m.batches[0].verts[2].v == 0.75f && m.batches[0].verts[0].v == 1.0f);
  CHECK(m.batches[0].verts[1].u == 1.0f);
  // The shipped character has no textures: only colours.
  mythos::GltfModel pete;
  CHECK(mythos::LoadGlb(
      std::string(MYTHOS_TEST_ASSET_DIR) + "/characters/prototype_pete.glb",
      pete));
  CHECK(pete.textures.empty());
}

TEST(reverb_send_bus_feeds_from_its_own_block) {
  constexpr int kRate = 48000;
  mythos::Reverb r;
  r.Prepare(kRate);
  r.Set(0.5, 1.0);
  std::vector<float> dry(2 * 480, 0.0f), bus(2 * 480, 0.0f);
  dry[0] = dry[1] = 1.0f;  // a loud dry impulse, but nothing sent to the reverb
  r.ProcessSend(bus.data(), dry.data(), 480);
  CHECK_EQ(dry[0], 1.0f);  // the dry sound is as it was
  float energy = 0;
  for (int b = 0; b < 20; ++b) {
    std::ranges::fill(dry, 0.0f);
    r.ProcessSend(bus.data(), dry.data(), 480);
    for (const float v : dry) energy += v * v;
  }
  CHECK_EQ(energy, 0.0f);  // nothing sent: no tail
  // The same impulse sent to the bus rings.
  mythos::Reverb wet;
  wet.Prepare(kRate);
  wet.Set(0.5, 1.0);
  bus[0] = bus[1] = 1.0f;
  std::ranges::fill(dry, 0.0f);
  wet.ProcessSend(bus.data(), dry.data(), 480);
  std::ranges::fill(bus, 0.0f);
  for (int b = 0; b < 20; ++b) {
    std::ranges::fill(dry, 0.0f);
    wet.ProcessSend(bus.data(), dry.data(), 480);
    for (const float v : dry) energy += v * v;
  }
  CHECK(energy > 1e-6f);
}

TEST(music_fades_wait_for_the_bar_line) {
  // 120 bpm, four beats: a bar is two seconds.
  CHECK_EQ(mythos::NextBarTime(0.0, 120, 4), 0.0);
  CHECK_EQ(mythos::NextBarTime(0.1, 120, 4), 2.0);
  CHECK_EQ(mythos::NextBarTime(2.0, 120, 4), 2.0);
  CHECK(std::fabs(mythos::NextBarTime(3.5, 120, 4) - 4.0) < 1e-9);
  CHECK_EQ(mythos::NextBarTime(3.0, 0, 4), 3.0);  // no tempo: now
  mythos::LayerFader f;
  f.Fade("pad", 0.0, 0.0);
  f.Update(0.5);
  f.FadeAt("pad", 1.0, 0.0, mythos::NextBarTime(f.Time(), 120, 4));  // at 2 s
  f.Update(1.0);
  CHECK_EQ(f.Gain("pad"), 0.0);  // still waiting at 1.5 s
  f.Update(0.6);
  CHECK_EQ(f.Gain("pad"), 1.0);  // the bar line passed
  // A time already past starts at once; a crossfade waits like a fade.
  f.FadeAt("pad", 0.5, 0.0, 0.1);
  CHECK_EQ(f.Gain("pad"), 0.5);
  f.CrossfadeAt({"strings"}, 0.0, f.Time() + 1.0);
  CHECK_EQ(f.Gain("strings"), 0.0);
  f.Update(1.1);
  CHECK(f.Gain("strings") == 1.0 && f.Gain("pad") == 0.0);
  f.Clear();
  CHECK_EQ(f.Time(), 0.0);
}

TEST(cutscene_samples_without_playing) {
  mythos::Cutscene s;
  s.camera = {{2, {4, 2, 0}, 1.0, 0.2, 1.2, mythos::Ease::kLinear},
              {0, {0, 2, 0}, 0, 0, 1, mythos::Ease::kLinear}};  // out of order
  s.values = {
      {"fade", {{0, 1, mythos::Ease::kLinear}, {1, 0, mythos::Ease::kLinear}}}};
  mythos::CameraSample c;
  CHECK(mythos::SampleCutsceneCamera(s, 1.0, &c));
  CHECK(std::fabs(c.pos.x - 2.0) < 1e-9 && std::fabs(c.yaw - 0.5) < 1e-9);
  CHECK(mythos::SampleCutsceneCamera(s, -5.0, &c) && c.pos.x == 0.0);
  CHECK(mythos::SampleCutsceneCamera(s, 99.0, &c) && c.pos.x == 4.0);
  CHECK_EQ(mythos::SampleCutsceneValue(s, "fade", 0.25), 0.75);
  CHECK_EQ(mythos::SampleCutsceneValue(s, "nope", 0.25, 7.0), 7.0);
  mythos::Cutscene empty;
  CHECK(!mythos::SampleCutsceneCamera(empty, 1.0, &c));
}

TEST(cutscene_console_commands_run_the_hooks) {
  mythos::CvarRegistry reg;
  std::string started;
  int skips = 0, stops = 0;
  mythos::CutsceneHooks hooks;
  hooks.start = [&](const std::string& name, std::string* error) {
    if (name != "intro") {
      *error = "no cutscene called " + name;
      return false;
    }
    started = name;
    return true;
  };
  hooks.skip = [&] { ++skips; };
  hooks.stop = [&] { ++stops; };
  hooks.list = [] { return std::vector<std::string>{"intro", "outro"}; };
  mythos::RegisterCutsceneCommands(reg, hooks);
  CHECK(reg.Has("cutscene.play") && reg.Has("cutscene.skip"));
  auto r = reg.Execute("cutscene.play intro");
  CHECK(r.ok && started == "intro");
  r = reg.Execute("cutscene.play nope");
  CHECK(!r.ok && r.text.find("nope") != std::string::npos);
  CHECK(!reg.Execute("cutscene.play").ok);
  CHECK(reg.Execute("cutscene.skip").ok && skips == 1);
  CHECK(reg.Execute("cutscene.stop").ok && stops == 1);
  r = reg.Execute("cutscene.list");
  CHECK(r.ok && r.text == "intro\noutro");
  // With no hooks the commands say so instead of crashing.
  mythos::CvarRegistry bare;
  mythos::RegisterCutsceneCommands(bare, {});
  CHECK(!bare.Execute("cutscene.skip").ok);
  CHECK(!bare.Execute("cutscene.play intro").ok);
  CHECK(!bare.Execute("cutscene.list").ok);
}

TEST(probes_are_ambient_cubes_with_direction) {
  mythos::Level l;
  CHECK(l.Parse(
      {"#########", "#.......#", "#.......#", "#.......#", "#########"}));
  // A lamp at the probe's own height, straight east of it.
  mythos::ProbeOptions options;
  options.height = 1.2;
  const mythos::ProbeLamp lamp{6.25, 1.2, 2.25, 1.0, 1.0, 1.0, 8.0};
  const mythos::ProbeGrid g = mythos::BakeProbes(l, {lamp}, options);
  const double x = 2.25, z = 2.25;  // exactly on a probe
  // Surfaces along the line to the lamp (x) get the full angle term, across it
  // (z, and up) only the floor of 0.35: x faces are about 2.9 times z faces.
  const auto east = g.SampleNormal(x, z, 1, 0, 0);
  const auto south = g.SampleNormal(x, z, 0, 0, 1);
  const auto up = g.SampleNormal(x, z, 0, 1, 0);
  CHECK(east[0] > south[0] * 2.5f && east[0] < south[0] * 3.2f);
  CHECK(std::fabs(south[0] - up[0]) < 1e-3f);
  // The renderer's angle term is two-sided: opposite faces match.
  CHECK(std::fabs(g.SampleFace(x, z, 0)[0] - g.SampleFace(x, z, 1)[0]) < 1e-4f);
  // A slanted normal is between its axes' values.
  const double s = std::sqrt(0.5);
  const auto diag = g.SampleNormal(x, z, s, 0, s);
  CHECK(diag[0] > south[0] && diag[0] < east[0]);
  // The mean of the faces is what Sample gives.
  const auto mean = g.Sample(x, z);
  float sum = 0;
  for (int f = 0; f < mythos::kProbeFaces; ++f) sum += g.SampleFace(x, z, f)[0];
  CHECK(std::fabs(mean[0] - sum / 6.0f) < 1e-5f);

  // The new file form round-trips; the old one-colour form still reads.
  mythos::ProbeGrid back;
  std::string error;
  CHECK(mythos::ParseProbes(mythos::SerializeProbes(g), &back, &error));
  CHECK(back.rgb.size() == g.rgb.size());
  CHECK(std::fabs(back.SampleNormal(x, z, 1, 0, 0)[0] - east[0]) < 1e-4f);
  mythos::ProbeGrid old;
  CHECK(mythos::ParseProbes("probes 0.5 2 1\n0.5 0.25 0.125 1 2 3\n", &old,
                            &error));
  CHECK(old.rgb.size() == 2 * 1 * 6 * 3);
  CHECK(old.Face(0, 0, 3)[1] == 0.25f && old.Face(1, 0, 5)[2] == 3.0f);
  CHECK(!mythos::ParseProbes("probes6 0.5 1 1\n1 2 3\n", &old, &error));
}

TEST(camera_roll_basis_and_shake_roll) {
  // Level: right on the horizontal plane, orthonormal axes.
  const mythos::ViewBasis level = mythos::MakeViewBasis(0.7, 0.3);
  CHECK(std::fabs(level.right[1]) < 1e-12);
  const auto dot = [](const double* a, const double* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  CHECK(std::fabs(dot(level.forward, level.right)) < 1e-12);
  CHECK(std::fabs(dot(level.forward, level.up)) < 1e-12);
  CHECK(std::fabs(dot(level.right, level.up)) < 1e-12);
  CHECK(std::fabs(dot(level.up, level.up) - 1.0) < 1e-12);
  // The same yaw / pitch as the sky used: forward and up of the old formulas.
  CHECK(std::fabs(level.forward[0] - std::cos(0.7) * std::cos(0.3)) < 1e-12);
  CHECK(std::fabs(level.up[1] - std::cos(0.3)) < 1e-12);
  // A quarter turn of roll puts `right` where `up` was (and drops it).
  const mythos::ViewBasis rolled =
      mythos::MakeViewBasis(0.7, 0.3, mythos::kPi / 2);
  for (int i = 0; i < 3; ++i) {
    CHECK(std::fabs(rolled.right[i] + level.up[i]) < 1e-12);
    CHECK(std::fabs(rolled.up[i] - level.right[i]) < 1e-12);
    CHECK(std::fabs(rolled.forward[i] - level.forward[i]) < 1e-12);
  }
  CHECK(rolled.right[1] < 0);  // the right side dropped
  // Shake: no roll by default (a level picture as before), a roll when asked.
  mythos::CameraFx fx;
  fx.AddShake(1.0);
  fx.Update(0.05);
  CHECK_EQ(fx.Sample().roll, 0.0);
  mythos::CameraFx tilting;
  tilting.params.max_roll = 0.05;
  tilting.AddShake(1.0);
  bool moved = false;
  for (int i = 0; i < 40; ++i) {
    tilting.Update(0.02);
    const double r = tilting.Sample().roll;
    CHECK(std::fabs(r) <= 0.05 + 1e-12);
    if (std::fabs(r) > 1e-4) moved = true;
  }
  CHECK(moved);
}

}

// Runs the registered tests whose name contains argv[1] (all by default);
// exits non-zero if any check failed.
int main(int argc, char* const* argv) {
  const std::string filter = argc > 1 ? argv[1] : "";
  int ran = 0;
  for (const auto& t : mythos::Registry()) {
    if (!filter.empty() &&
        std::string(t.name).find(filter) == std::string::npos) {
      continue;
    }
    const int before = mythos::g_failures;
    std::printf("[ RUN  ] %s\n", t.name);
    t.fn();
    std::printf("[ %s ] %s\n", mythos::g_failures == before ? " OK " : "FAIL",
                t.name);
    ++ran;
  }
  std::printf("\n%d test(s), %d check(s), %d failure(s)\n", ran,
              mythos::g_checks, mythos::g_failures);
  return mythos::g_failures == 0 ? 0 : 1;
}
