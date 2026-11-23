

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
#include "engine/audio/radio_fx.h"
#include "engine/audio/sound_bank.h"
#include "engine/core/cvar.h"
#include "engine/core/fileio.h"
#include "engine/core/json.h"
#include "engine/core/json_reader.h"
#include "engine/core/mat4.h"
#include "engine/core/noise.h"
#include "engine/core/tween.h"
#include "engine/core/vfs.h"
#include "engine/data/achievements.h"
#include "engine/data/credits.h"
#include "engine/data/i18n.h"
#include "engine/data/translation.h"
#include "engine/editor/adapter.h"
#include "engine/editor/history.h"
#include "engine/editor/level_doc.h"
#include "engine/editor/project.h"
#include "engine/gfx/flicker.h"
#include "engine/gfx/font.h"
#include "engine/gfx/grade.h"
#include "engine/gfx/materials.h"
#include "engine/gfx/mesh.h"
#include "engine/gfx/model.h"
#include "engine/gfx/normalmap.h"
#include "engine/gfx/particles.h"
#include "engine/gfx/pbr.h"
#include "engine/gfx/render_stats.h"
#include "engine/gfx/surface.h"
#include "engine/gfx/text.h"
#include "engine/gfx/textures.h"
#include "engine/ui/debug_console.h"
#include "engine/ui/debug_overlay.h"
#include "engine/ui/menu.h"
#include "engine/ui/paging.h"
#include "engine/ui/settings.h"
#include "engine/ui/settings_cvars.h"
#include "engine/ui/ui.h"
#include "engine/world/body.h"
#include "engine/world/camera_fx.h"
#include "engine/world/free_cam.h"
#include "engine/world/gamepad.h"
#include "engine/world/interact.h"
#include "engine/world/level.h"
#include "engine/world/mirror.h"
#include "engine/world/movement.h"
#include "engine/world/player.h"
#include "engine/world/room_doors.h"
#include "engine/world/tiles.h"

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
        DecalFace::kThinNorth, DecalFace::kThinSouth,
        DecalFace::kThinWest, DecalFace::kThinEast}) {
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
  for (const auto face : {DecalFace::kNorth, DecalFace::kSouth,
                          DecalFace::kWest, DecalFace::kEast,
                          DecalFace::kFloor, DecalFace::kCeiling}) {
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
  CHECK(geometry.decals[0].texture == std::to_string(static_cast<int>(DecalFace::kNorth)));
  CHECK(geometry.decals[1].texture == std::to_string(static_cast<int>(DecalFace::kWest)));
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
    CHECK(level.Parse(along_x ? std::vector<std::string>{".....", ".WwW.", "....."}
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
    CHECK(level.Parse(along_x ? std::vector<std::string>{".....", ".WwW.", "....."}
                              : std::vector<std::string>{".W.", ".w.", ".W."}));
    level.SetTiles(ThinLegend());
    for (const auto face : along_x
             ? std::vector<DecalFace>{DecalFace::kThinNorth, DecalFace::kThinSouth}
             : std::vector<DecalFace>{DecalFace::kThinWest, DecalFace::kThinEast}) {
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
              (u >= 0.4799f && u <= 0.5201f) ||
              (y >= 1.4549f && y <= 1.4951f));
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
