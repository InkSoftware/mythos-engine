

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
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/audio_math.h"
#include "engine/audio/radio_fx.h"
#include "engine/audio/sound_bank.h"
#include "engine/core/fileio.h"
#include "engine/core/json.h"
#include "engine/core/json_reader.h"
#include "engine/core/mat4.h"
#include "engine/core/noise.h"
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
#include "engine/gfx/materials.h"
#include "engine/gfx/mesh.h"
#include "engine/gfx/model.h"
#include "engine/gfx/normalmap.h"
#include "engine/gfx/pbr.h"
#include "engine/gfx/surface.h"
#include "engine/gfx/text.h"
#include "engine/gfx/textures.h"
#include "engine/ui/menu.h"
#include "engine/ui/paging.h"
#include "engine/ui/settings.h"
#include "engine/ui/ui.h"
#include "engine/world/body.h"
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
