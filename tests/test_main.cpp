// Unit tests for mythos-engine. No external framework and no display needed,
// so they run unchanged on macOS, Linux and Windows CI runners.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/audio_math.h"
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
#include "engine/ui/settings.h"
#include "engine/ui/ui.h"
#include "engine/world/body.h"
#include "engine/world/interact.h"
#include "engine/world/level.h"
#include "engine/world/mirror.h"
#include "engine/world/movement.h"
#include "engine/world/player.h"
#include "engine/world/room_doors.h"
#include "engine/world/tiles.h"

namespace mythos {

namespace {

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

}  // namespace

TEST(OverlayTakesTheWindowShapeAtAWholeScale) {
  int w = 0, h = 0;
  OverlaySize(960, 600, 320, 200, w, h);
  CHECK(w == 320 && h == 200);
  OverlaySize(1920, 1080, 320, 200, w, h);  // 16:9 at scale 5
  CHECK(w == 384 && h == 216);
  OverlaySize(1280, 720, 320, 200, w, h);  // scale 3
  CHECK(w == 426 && h == 240);
  OverlaySize(800, 1000, 320, 200, w, h);  // taller than wide: scale 2
  CHECK(w == 400 && h == 500);
  OverlaySize(100, 100, 320, 200, w, h);  // never below the page size
  CHECK(w == 320 && h == 200);
}

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
    CHECK_EQ(out[0].verts.size(), 6u);  // a quad becomes two triangles
  }
}

TEST(ObjParserRejectsBadFaces) {
  std::vector<Batch> out;
  std::string err;
  CHECK(!ParseObj("v 0 0 0\nf 1 2 3\n", out, &err));
  CHECK(!err.empty());
}

TEST(ZoneParserRejectsBadFurniture) {
  Level l;
  std::string err;
  CHECK(!l.ParseText("[map]\n...\n...\n[furniture]\n1 1 bed x\n", &err));
  CHECK(!err.empty());
}

TEST(ADecorSectionPlacesNonSolidPieces) {
  Level l;
  std::string err;
  CHECK(
      l.ParseText("[map]\n...\n...\n[furniture]\n0 0 bed 90\n[decor]\n1 1 vase "
                  "180 0.25 -0.1 0.78\n2 1 duck 0\n",
                  &err));
  CHECK_EQ(l.Furniture().size(), 3u);
  CHECK(!l.Passable(0, 0));                     // furniture blocks its cell
  CHECK(l.Passable(1, 1) && l.Passable(2, 1));  // decor blocks nothing
  CHECK(l.FurnitureAt(1, 1) == nullptr);
  const Placement& vase = l.Furniture()[1];
  CHECK(!vase.solid && vase.model == "vase");
  CHECK(vase.off_x > 0.249 && vase.off_x < 0.251 && vase.off_z < -0.099 &&
        vase.off_z > -0.101);
  CHECK(vase.lift > 0.779 && vase.lift < 0.781);
  CHECK(l.Furniture()[2].lift == 0.0 &&
        l.Furniture()[2].off_x == 0.0);  // the offsets are optional
  CHECK(!l.ParseText("[map]\n...\n[decor]\n5 5 vase 0\n",
                     &err));  // outside the map
  CHECK(!l.ParseText("[map]\n...\n[decor]\n1 0 vase\n", &err));  // no yaw
}

//---- the body and the light in the hand

namespace {
double Dist3(const double a[3], const double b[3]) {
  return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) +
                   (a[1] - b[1]) * (a[1] - b[1]) +
                   (a[2] - b[2]) * (a[2] - b[2]));
}
}  // namespace

TEST(AnArmKeepsItsBoneLengthsAndReachesWhatIsInReach) {
  const BodyParams bt;
  const double shoulder[3] = {0, 1.4, 0}, bend[3] = {0, -1, 0.3};
  for (const double target :
       {0.15, 0.3, 0.5, 0.6,
        0.9}) {  // the last is out of reach: the hand stops short
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
    CHECK(ik.elbow[1] <= shoulder[1] + 1e-9);  // the elbow bends down, not up
  }
}

TEST(TheChestFollowsTheViewButTheHeadMayTurnFirst) {
  const BodyParams bt;
  CHECK(std::abs(TurnBody(0.0, bt.turn_limit * 0.5, 0.0, false, bt)) <
        1e-9);  // a small turn: the chest stays
  const double dragged = TurnBody(0.0, bt.turn_limit + 0.5, 0.0, false,
                                  bt);  // beyond the limit it is pulled along
  CHECK(std::abs(dragged - 0.5) < 1e-9);
  double yaw = 0.0;
  for (int i = 0; i < 300; ++i) {
    yaw = TurnBody(yaw, 0.6, 0.02, true, bt);  // walking, it catches up
  }
  CHECK(std::abs(yaw - 0.6) < 0.01);
  CHECK(std::abs(TurnBody(3.1, -3.1, 0.0, false, bt) - 3.1) < 1e-9 ||
        std::abs(TurnBody(3.1, -3.1, 0.0, false, bt)) >
            3.0);  // across the wrap
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
  // The torso's feet are on the floor, a little behind the eye along the view.
  CHECK(std::abs(p.torso.m[13]) < 1e-6);
  const double dx = p.torso.m[12] - in.eye_x, dz = p.torso.m[14] - in.eye_z;
  CHECK(std::abs(std::hypot(dx, dz) - bt.eye_ahead) < 1e-6);
  CHECK(dx * std::cos(in.yaw) + dz * std::sin(in.yaw) < 0);
  // The held torch is below the eye and to the right of the view, ahead of it.
  const double hx = p.hand_x - in.eye_x, hz = p.hand_z - in.eye_z;
  CHECK(p.hand_y < in.eye_height);
  CHECK(hx * std::cos(in.yaw) + hz * std::sin(in.yaw) > 0.1);
  CHECK(std::abs(p.held.m[12] - p.hand_x) < 1e-5 &&
        std::abs(p.held.m[14] - p.hand_z) < 1e-5);
  // The raised right forearm ends at the hand.
  const double wrist_x = p.fore_arm_r.m[12] + p.fore_arm_r.m[8] * bt.fore_arm,
               wrist_z = p.fore_arm_r.m[14] + p.fore_arm_r.m[10] * bt.fore_arm;
  CHECK(std::hypot(wrist_x - p.hand_x, wrist_z - p.hand_z) < 0.05);
  // Every part's frame is a rotation (unit columns).
  for (const PartMatrix* m :
       {&p.torso, &p.head, &p.leg_l, &p.leg_r, &p.upper_arm_l, &p.fore_arm_l,
        &p.upper_arm_r, &p.fore_arm_r, &p.held}) {
    for (int c = 0; c < 3; ++c) {
      const double len =
          std::sqrt(static_cast<double>(m->m[c * 4]) * m->m[c * 4] +
                    static_cast<double>(m->m[c * 4 + 1]) * m->m[c * 4 + 1] +
                    static_cast<double>(m->m[c * 4 + 2]) * m->m[c * 4 + 2]);
      CHECK(std::abs(len - 1.0) < 1e-4);
    }
  }
}

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
  CHECK(!LoadHeldParams(r2, "held",
                        h));  // no \"held\" object: every key is required
}

TEST(TheHandLagsATurnSettlesAndSwaysAtAWalk) {
  const HandSwayParams p;
  HandSway sway;
  HandPose still;
  for (int i = 0; i < 120; ++i) {
    still = sway.Update(1.0 / 60, i / 60.0, 5, 5, 0.3, 0.0, p);
  }
  CHECK(still.walk < 0.01);
  CHECK(std::abs(still.yaw) < 0.05);
  const HandPose turned = sway.Update(1.0 / 60, 2.0, 5, 5, 0.6, 0.0,
                                      p);  // a sudden turn left the beam behind
  CHECK(turned.yaw < -0.05);
  HandSway walker;
  HandPose w;
  for (int i = 0; i < 180; ++i) {
    w = walker.Update(1.0 / 60, i / 60.0, 5 + i * 0.05, 5, 0, 0, p);  // 3 m/s
  }
  CHECK(w.walk > 0.9);
  CHECK(w.stride > 1.0);
  HandSway jumped;  // a long frame restarts from the pose rather than reading a
                    // huge turn and speed
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

TEST(AMirrorShowsOnlyInRangeAndInSight) {
  MirrorParams m;
  m.range = 5;
  Prop mirror;
  mirror.model = "mirror";
  mirror.at = {.x = 2.5, .y = 1.0};
  mirror.yaw_deg = 0;  // faces south (+z)
  mirror.lift = 1.0;
  Level level;
  std::string err;
  CHECK(level.Parse({"#####", "#...#", "#...#", "#...#", "#####"}, &err));
  const MirrorPlacement near = PlaceMirror(m, mirror, 2.5, 3.0, level);
  CHECK(near.on);
  CHECK(std::abs(near.nz - 1.0) < 1e-9 && std::abs(near.nx) < 1e-9);
  CHECK(std::abs(near.y - (1.0 + m.glass_height)) < 1e-9);
  CHECK(!PlaceMirror(m, mirror, 2.5, 30.0, level).on);  // out of range
  mirror.model.clear();
  CHECK(!PlaceMirror(m, mirror, 2.5, 3.0, level).on);  // nothing hangs there
}

TEST(FontMeasuresAndWrapsText) {
  CHECK_EQ(TextWidth("AB", 1), 11);
  CHECK_EQ(TextWidth("AB", 2), 22);
  CHECK_EQ(TextWidth("", 1), 0);
  auto lines = WrapText("alpha beta gamma", 10);
  CHECK_EQ(lines.size(), 2u);
  if (lines.size() == 2) {
    CHECK(lines[0] == "alpha beta");
    CHECK(lines[1] == "gamma");
  }
}

// The word wrap as it was first written: every candidate line measured from
// scratch.
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
      const std::string candidate = line.empty() ? word : line + " " + word;
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
  const std::string texts[] = {
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
  // Asking again (kerning now cached) and through the flat glyph table agree
  // with the map.
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

TEST(EngineJsonParsesValuesAndReportsErrors) {
  json::Value v;
  std::string err;
  // (The text is kept out of the CHECK: MSVC cannot stringize a raw string with
  // quotes in a macro argument.)
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
  CHECK(v.size() == 3 && std::prev(v.end()).key() == "o");  // key order is kept
  CHECK(!json::Parse("{\"a\": 1,\n \"a\": 2}", v, &err));   // duplicate key
  CHECK(err.find("line 2") == 0);
  CHECK(!json::Parse("[1, 2", v, &err));
  CHECK(!json::Parse("{'a': 1}", v, &err));
  CHECK(!json::Parse("01", v, &err));
  CHECK(!json::Parse("\"\\ud800\"", v, &err));  // unpaired surrogate
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
  CHECK(l.Passable(1, 1));  // 'W' means nothing without the game's legend
  CHECK(l.Passable(2, 1));
  const auto tiles = std::make_shared<TileSet>();
  (*tiles)['W'].solid = true;
  l.SetTiles(tiles);
  CHECK(!l.Passable(1, 1));
  CHECK(l.Passable(2, 1));
  const Level copy = l;  // copies keep their legend
  CHECK(!copy.Passable(1, 1));
}

TEST(EngineCollisionSlidesAlongWalls) {
  Level l;
  CHECK(l.Parse({"#####", "#...#", "#...#", "#####"}));
  Vec2 p{.x = 1.5, .y = 1.5};
  MoveWithCollision(l, p, -1.0, 0.5, 0.22);  // into the west wall, and south
  CHECK(p.x == 1.5);                         // blocked on x
  CHECK(std::fabs(p.y - 2.0) < 1e-9);        // slid on y
}

TEST(EngineFieldWalkerReachesTheGoal) {
  Level l;
  CHECK(l.Parse({"#######", "#.....#", "###.###", "#.....#", "#######"}));
  const std::vector<int> field = l.DistanceField(5, 3);
  Vec2 p{.x = 1.5, .y = 1.5};
  FieldWalker w;
  for (int i = 0; i < 100; ++i) WalkField(l, field, p, w, 0.25);
  CHECK(std::fabs(p.x - 5.5) < 1e-9);
  CHECK(std::fabs(p.y - 3.5) < 1e-9);
}

TEST(EngineRayHitsSphere) {
  const Vec3 eye{.x = 0, .y = 0, .z = 0}, ahead{.x = 1, .y = 0, .z = 0};
  CHECK(RayHitsSphere(eye, ahead, {3, 0, 0}, 0.5, 10) == 3.0);
  CHECK(RayHitsSphere(eye, ahead, {3, 0.4, 0}, 0.5, 10) > 0);  // grazes it
  CHECK(RayHitsSphere(eye, ahead, {3, 0.6, 0}, 0.5, 10) < 0);  // misses it
  CHECK(RayHitsSphere(eye, ahead, {-3, 0, 0}, 0.5, 10) < 0);   // behind
  CHECK(RayHitsSphere(eye, ahead, {30, 0, 0}, 0.5, 10) < 0);   // out of reach
}

TEST(EngineWalkStep) {
  const Vec2 fwd = WalkStep(0, 1, 0, 2.0);
  CHECK(std::abs(fwd.x - 2.0) < 1e-9 && std::abs(fwd.y) < 1e-9);
  const Vec2 right =
      WalkStep(0, 0, 1, 1.0);  // strafing right at angle 0 goes +y
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
  CHECK(WriteFileAtomic(path, "", true));  // empty removes it
  CHECK(ReadTextFile(path) == std::string());
}

TEST(EngineLoadImageDecodesBmpAndOverridesTextures) {
  // A 2x1 24-bit BMP: a red pixel then a blue one (BMP stores BGR, rows padded
  // to 4 bytes).
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
  CHECK_EQ(steps, 1);  // 0.6 walked, one 0.5 stride
  CHECK(p.pos.x > 2.0 && p.pos.x < 2.2);
  const MoveInput idle;
  CHECK(!w.Update(l, p, 0.1, idle, params));
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
  CHECK_EQ(FindAimed(l, p, 1.0, 5.0, c), 1);   // nearest enabled
  CHECK_EQ(FindAimed(l, p, 1.0, 1.0, c), -1);  // out of reach
  p.angle = kPi;                               // looking away
  CHECK_EQ(FindAimed(l, p, 1.0, 5.0, c), -1);
}

TEST(RuntimeVerbs) {
  Verb v = Verb::kTake;
  CHECK(ParseVerb("read", v) && v == Verb::kRead);
  CHECK(ParseVerb("listen", v) && v == Verb::kListen);
  CHECK(!ParseVerb("lick", v) && v == Verb::kListen);  // unchanged on failure
  Prop p;
  p.id = "thing";
  Interactor it;
  p.verb = Verb::kRead;
  CHECK(it.Use(p, false, "", 0.1, false, false, 1.0) == Use::kNone);
  CHECK(it.Use(p, false, "", 0.1, true, true, 1.0) == Use::kRead);
  p.verb = Verb::kInspect;
  CHECK(it.Use(p, false, "", 0.1, true, true, 1.0) == Use::kInspect);
  // The empty place of the held prop puts it back, whatever the verb.
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
  CHECK(it.Use(p, false, "", 0.4, false, false, 1.0) ==
        Use::kNone);  // not holding
  CHECK(it.Use(p, false, "", 0.4, false, true, 1.0) == Use::kListening);
  CHECK(it.ListenTime() > 0.39);
  CHECK(it.Use(p, false, "", 0.4, false, true, 1.0) == Use::kListening);
  CHECK(it.Use(p, false, "", 0.4, false, true, 1.0) == Use::kHeard);
  CHECK(it.Use(p, false, "", 0.4, false, true, 1.0) ==
        Use::kNone);  // still holding: no repeat
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
  CHECK(!ApplyEffects(pr, e));  // the clue is no longer new
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
  CHECK_EQ(p.lift, 0.8);  // not given: unchanged
  const Placement pl = PlacementOf(p);
  CHECK(pl.x == 4 && pl.y == 2 && !pl.solid && pl.id == "mug");
  CHECK(pl.off_x > 0.24 && pl.off_x < 0.26 && pl.off_z < -0.24 &&
        pl.off_z > -0.26);
}

TEST(RuntimeMenuStepWrapsSkipsAndActivates) {
  int index = 0;
  UiInput in;
  const auto usable = [](int i) { return i != 1; };
  in.down_pressed = true;
  CHECK_EQ(MenuStep(index, 4, in, false, usable), -1);
  CHECK_EQ(index, 2);  // item 1 is skipped
  index = 3;
  CHECK_EQ(MenuStep(index, 4, in, false, usable), -1);
  CHECK_EQ(index, 0);  // wraps
  in = UiInput{};
  in.up_pressed = true;
  MenuStep(index, 4, in, false, usable);
  CHECK_EQ(index, 3);  // 0 -> back round to 3
  in = UiInput{};
  CHECK_EQ(MenuStep(index, 4, in, true, usable),
           3);  // activate chooses the selection
  in.hover_item = 1;
  MenuStep(index, 4, in, false, usable);
  CHECK_EQ(index, 3);  // hovering an unusable item does nothing
  in.hover_item = 2;
  MenuStep(index, 4, in, false, usable);
  CHECK_EQ(index, 2);
  in = UiInput{};
  in.click_item = 0;
  CHECK_EQ(MenuStep(index, 4, in, false, usable), 0);
  in.click_item = 1;
  CHECK_EQ(MenuStep(index, 4, in, false, usable),
           -1);  // an unusable item cannot be clicked
}

TEST(RuntimeSettingsPageKeysClicksAndBack) {
  GameSettings s;
  SettingsPage page = SettingsPage::kMain;
  int index = kSetGraphics;
  UiInput in;
  in.confirm_pressed = true;
  CHECK(!UpdateSettingsPage(s, page, index, in, false,
                            2));  // GRAPHICS opens its page
  CHECK(page == SettingsPage::kGraphics);
  CHECK_EQ(index, kSetResolution);
  in = UiInput{};
  in.right_pressed = true;
  for (int i = 0; i < 6; ++i) UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.resolution, 2);  // keys stop at the display's maximum
  in = UiInput{};
  in.click_item = kSetResolution;
  in.click_step = 1;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK_EQ(s.resolution, 0);  // clicks cycle round
  in = UiInput{};
  in.click_item = kSetMaster;  // not a row of this page: ignored
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(page == SettingsPage::kGraphics && index == kSetResolution);
  index = kSetFullscreen;
  in = UiInput{};
  in.confirm_pressed = true;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(s.fullscreen);
  in = UiInput{};
  in.pause_pressed =
      true;  // Esc on a sub-page goes back to the main page, on its link
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
  CHECK_EQ(s.master_volume,
           3);  // clicking the lit last pip again turns it down
  in = UiInput{};
  in.click_item = kSetBack;
  CHECK(!UpdateSettingsPage(s, page, index, in, false,
                            2));  // Back: to the main page
  CHECK(page == SettingsPage::kMain && index == kSetAudio);
  index = kSetLanguage;
  in = UiInput{};
  in.confirm_pressed = true;
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(s.language == Lang::kGreek);
  index = kSetBack;
  CHECK(UpdateSettingsPage(s, page, index, in, false,
                           2));  // Back on the main page closes
  in = UiInput{};
  in.pause_pressed = true;
  CHECK(UpdateSettingsPage(s, page, index, in, false, 2));  // so does Esc
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
  a.motion_blur = 4;  // 100%
  a.camera_effect = false;
  a.vsync = false;
  a.fps_cap = 4;  // 120
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
  CHECK_EQ(GameSettings{}.materials, 2);  // full by default
  CHECK(b.motion_blur == 4 && !b.camera_effect && !b.vsync);
  CHECK_EQ(b.fps_cap, 4);
  CHECK_EQ(b.master_volume, 3);
  CHECK(b.language == Lang::kGreek);
  GameSettings c;
  ApplyKeyValues(c, {{"masterVolume", "99"},
                     {"renderScale", "-4"},
                     {"nonsense", "1"},
                     {"resolution", "bad"}});
  CHECK_EQ(c.master_volume, 10);  // clamped
  CHECK_EQ(c.render_scale, 0);
  CHECK_EQ(c.resolution, 0);  // unparsable: unchanged
  GameSettings d;
  ApplyKeyValues(d, {{"antiAliasing", "99"},
                     {"fpsCap", "77"},
                     {"shadowMaps", "9"},
                     {"motionBlurScale", "33"},
                     {"materials", "7"}});
  CHECK_EQ(d.anti_alias, kAntiAliasCount - 1);
  CHECK_EQ(d.fps_cap, 0);  // not an offered limit: none
  CHECK_EQ(d.shadows, kShadowLevelCount - 1);
  CHECK_EQ(d.materials, kMaterialLevelCount - 1);
  CHECK_EQ(d.motion_blur, 0);  // not an offered strength: off
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
  CHECK_EQ(s.fps_cap, kFpsCapCount - 1);  // keys stop at the end
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
  UpdateSettingsPage(s, page, index, in, false, 2);
  CHECK(!s.camera_effect);
  // Every page ends with Back, and up / down wrap round its rows.
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

TEST(RuntimeEngineStringsCoverBothLanguagesAndYieldToTheGame) {
  const TranslationTable& en = EngineStrings(Lang::kEnglish);
  const TranslationTable& el = EngineStrings(Lang::kGreek);
  CHECK_EQ(en.size(), el.size());
  for (const auto& [key, text] : en) CHECK(el.count(key) == 1);
  SetLanguage(Lang::kEnglish);
  SetTranslations(Lang::kEnglish, nullptr);
  CHECK(Tr("settings.vsync") == "VSYNC");  // no game table: the engine's own
  CHECK(Tr("common.on") == "ON");
  const TranslationTable game = {{"settings.vsync", "SYNC"}};
  SetTranslations(Lang::kEnglish, &game);
  CHECK(Tr("settings.vsync") == "SYNC");  // the game's wins
  CHECK(Tr("settings.title") == "SETTINGS");
  SetTranslations(Lang::kEnglish, nullptr);
}

namespace {
json::Value ParseJson(const std::string& text) {
  json::Value v;
  std::string err;
  CHECK(json::Parse(text, v, &err));
  return v;
}
}  // namespace

TEST(RuntimeDataLoadersReadTheDocumentedFormats) {
  std::string err;
  {  // achievements
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
  {  // player numbers
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
  {  // sounds
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
  {  // translations
    const json::Value v = ParseJson(
        R"({"language":"en","name":"ENGLISH","strings":{"hud":{"a":"A"},"b":"B"}})");
    Translation t;
    CHECK(LoadTranslation(v, "f", t, &err));
    CHECK(t.strings.at("hud.a") == "A" && t.strings.at("b") == "B");
  }
  {  // credits
    const json::Value v = ParseJson(R"({"sections":[{"title":"t","entries":[
            {"name":"n","by":"b","license":"CC0","what":""}]}]})");
    std::vector<CreditSection> secs;
    CHECK(LoadCredits(v, "f", secs, &err));
    CHECK(secs.size() == 1 && secs[0].entries[0].license == "CC0");
  }
}

namespace {
// m * (x, y, z, 1), column-major.
void Transform(const float* m, float x, float y, float z, float out[4]) {
  for (int r = 0; r < 4; ++r) {
    out[r] = m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r];
  }
}
}  // namespace

TEST(RuntimeMat4ViewProjectionAndFrustum) {
  const float eye[3] = {2, 1.5f, 3}, center[3] = {2, 1.5f, -2},
              up[3] = {0, 1, 0};
  float view[16], proj[16], vp[16], out[4];
  Mat4LookAt(eye, center, up, view);
  Transform(view, 2, 1.5f, -2, out);  // five metres straight ahead: (0, 0, -5)
  CHECK(std::abs(out[0]) < 1e-5f && std::abs(out[1]) < 1e-5f &&
        std::abs(out[2] + 5.0f) < 1e-5f);
  Mat4Perspective(1.2f, 1.6f, 0.05f, 40.0f, proj);
  Mat4Multiply(proj, view, vp);
  Transform(vp, 2, 1.5f, -2,
            out);  // the centre of the picture, inside the depth range
  CHECK(std::abs(out[0] / out[3]) < 1e-5f && std::abs(out[1] / out[3]) < 1e-5f);
  CHECK(out[2] / out[3] > -1.0f && out[2] / out[3] < 1.0f);
  Frustum planes;
  FrustumPlanes(vp, planes);
  const float in_min[3] = {1.9f, 1.4f, -2.1f}, in_max[3] = {2.1f, 1.6f, -1.9f};
  CHECK(BoxInFrustum(planes, in_min, in_max));
  const float behind_min[3] = {1.9f, 1.4f, 5.0f},
              behind_max[3] = {2.1f, 1.6f, 6.0f};
  CHECK(!BoxInFrustum(planes, behind_min, behind_max));
  CHECK(!BoxInFrustum(planes, in_min, in_max,
                      -100.0f));  // shifted far out of view
  const float far_min[3] = {1.9f, 1.4f, -90.0f},
              far_max[3] = {2.1f, 1.6f, -80.0f};
  CHECK(!BoxInFrustum(planes, far_min, far_max));  // beyond the far plane
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
  // GL's cube sampling: +X's right is -Z and its up is -Y; +Z's right is +X
  // (seen from the eye, looking out).
  float view[16], out[4];
  CubeFaceView(0, eye, view);
  Transform(view, eye[0] + 1, eye[1], eye[2] - 1,
            out);  // a point towards -Z sits on the right of +X's picture
  CHECK(out[0] > 0.5f);
  CubeFaceView(4, eye, view);
  Transform(view, eye[0] + 1, eye[1], eye[2] + 1,
            out);  // and towards +X on the right of +Z's
  CHECK(out[0] > 0.5f);
}

TEST(RuntimePositionalGainPansFadesAndHushes) {
  const Vec2 me{.x = 5, .y = 5};
  // Facing +x: a source ahead is centred; one to the right (+z, south) leans
  // right; to the left, left.
  const StereoGain ahead = PositionalGain(me, 0.0, {.x = 8, .y = 5}, 0);
  CHECK(std::abs(ahead.left - ahead.right) < 1e-4f);
  const StereoGain on_right = PositionalGain(me, 0.0, {.x = 5, .y = 8}, 0);
  const StereoGain on_left = PositionalGain(me, 0.0, {.x = 5, .y = 2}, 0);
  CHECK(on_right.right > on_right.left);
  CHECK(on_left.left > on_left.right);
  CHECK(on_right.left > 0 && on_left.right > 0);  // the pan is soft, never hard
  // Farther is quieter.
  const StereoGain near = PositionalGain(me, 0.0, {.x = 6, .y = 5}, 0);
  const StereoGain far = PositionalGain(me, 0.0, {.x = 15, .y = 5}, 0);
  CHECK(near.left > far.left);
  // A hushed source falls silent close up, and a muted one always.
  const StereoGain hushed = PositionalGain(me, 0.0, {.x = 6, .y = 5}, 2.0);
  CHECK(hushed.left == 0.0f && hushed.right == 0.0f);
  CHECK(PositionalGain(me, 0.0, {8, 5}, 2.0).left > 0.0f);
  CHECK(PositionalGain(me, 0.0, {6, 5}, 0, true).left == 0.0f);
  // A smaller falloff carries: the same distance is louder, yet near is still
  // louder than far.
  const StereoGain carries =
      PositionalGain(me, 0.0, {.x = 15, .y = 5}, 0, false, 1.0, 0.1);
  CHECK(carries.left > far.left * 5.0f);
  CHECK(PositionalGain(me, 0.0, {6, 5}, 0, false, 1.0, 0.1).left >
        carries.left);
  CHECK(PositionalGain(me, 0.0, {15, 5}, 0).left ==
        PositionalGain(me, 0.0, {15, 5}, 0, false, 0.75, 0.5)
            .left);  // the defaults
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
  CHECK_EQ(p1.pitch, 1.5f);  // no jitter: the cue's own pitch
  const float jitter_draw =
      r2.Next();  // one draw for a single clip, as the footsteps always did
  CHECK_EQ(r1.Next(), r2.Next());  // ...so the streams stay in step
  (void)jitter_draw;
  Rng r3(7);
  for (int i = 0; i < 200; ++i) {
    const CuePick p = PickCue(many, r3);
    CHECK(p.clip < 3);
    CHECK(p.pitch >= 0.92f && p.pitch <= 1.08f);
  }
}

TEST(RuntimeFlickerMatchesItsFormulaAndStaysInRange) {
  CHECK_EQ(LampGain(12.3, 0, 0.0), 1.0);  // steady
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
  CHECK(dark > 500 && dark < 1100);  // about 40% of slots at flicker 0.8
  int lit = 0;
  for (int i = 0; i < 2000; ++i) lit += TorchLit(i / 14.0 + 0.01, 0.5) ? 1 : 0;
  CHECK(lit > 1300 && lit < 1700);  // about 75% lit
  CHECK(TorchLit(3.0, 0.0));        // no restlessness: always lit
}

// ---- room doors need a legend that has doors: N closed, n open.

namespace {
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

Level DoorLevel() {
  Level l;
  l.SetTiles(DoorTiles());
  // A small room on the right, a corridor on the left, a door N between them.
  l.Parse({"#########", "#...#...#", "#...N...#", "#...#...#", "#########"});
  return l;
}
}  // namespace

TEST(RuntimeRoomDoorSwingsIntoTheSmallerSpaceAndCloses) {
  Level l = DoorLevel();
  RoomDoors doors;
  doors.Scan(l);
  std::vector<DoorState> st;
  doors.AppendStates(st);
  CHECK_EQ(st.size(), 1u);
  CHECK(st[0].passage_along_x);  // walls north and south of it: you walk
                                 // through along x
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
  doors.Use(4, 2);  // close it
  for (int i = 0; i < 20; ++i) doors.Update(l, 0.1, inside, 0.25, 0.5, 0.5);
  CHECK_EQ(l.At(4, 2), 'n');  // waits, ajar
  const Vec2 out{.x = 1.5, .y = 2.5};
  for (int i = 0; i < 20; ++i) doors.Update(l, 0.1, out, 0.25, 0.5, 0.5);
  CHECK_EQ(l.At(4, 2), 'N');
}

TEST(NormalMapIsFlatForAFlatTextureAndTiltsWithASlope) {
  const Surface flat(8, 8, Rgba(120, 120, 120));
  const Surface nf = MakeNormalMap(flat, 3.0f);
  CHECK_EQ(nf.w, 8);
  for (const uint32_t p : nf.px) CHECK_EQ(p, Rgba(128, 128, 255));

  // Brightness rising to the right (+u): the surface leans back towards -x.
  Surface ramp(8, 8);
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) ramp.At(x, y) = Rgba(x * 30, x * 30, x * 30);
  }
  Surface nr = MakeNormalMap(ramp, 3.0f);
  const uint32_t p = nr.At(4, 4);
  CHECK(((p >> 16) & 0xFF) < 120);  // x tilts negative
  CHECK(std::abs(static_cast<int>((p >> 8) & 0xFF) - 128) <=
        1);                 // no slope in y
  CHECK((p & 0xFF) > 128);  // still faces outwards
  // No strength, no tilt.
  CHECK_EQ(MakeNormalMap(ramp, 0.0f).At(4, 4), Rgba(128, 128, 255));

  // Brightness rising down the image (towards -v): y tilts positive (+v is up).
  Surface down(8, 8);
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) down.At(x, y) = Rgba(y * 30, y * 30, y * 30);
  }
  CHECK(((MakeNormalMap(down, 3.0f).At(4, 4) >> 8) & 0xFF) > 136);
  CHECK(MakeNormalMap(Surface(), 1.0f).Empty());
}

TEST(SurfaceMapUsesAuthoredNormalAndRoughnessOrFallsBack) {
  const Surface albedo(4, 4, Rgba(100, 100, 100));
  // Nothing authored: the brightness-made normal (flat here) and no gloss, as
  // before PBR.
  Surface plain = MakeSurfaceMap(albedo, nullptr, nullptr, 3.0f);
  CHECK_EQ(plain.At(1, 1), Rgba(128, 128, 255, 0));
  // An authored normal map is taken as it is; roughness 64 becomes gloss 191.
  const Surface nrm(4, 4, Rgba(200, 50, 220));
  const Surface rough(4, 4, Rgba(64, 64, 64));
  Surface both = MakeSurfaceMap(albedo, &nrm, &rough, 3.0f);
  CHECK_EQ(both.At(2, 3), Rgba(200, 50, 220, 191));
  // Roughness alone keeps the generated normal.
  CHECK_EQ(MakeSurfaceMap(albedo, nullptr, &rough, 3.0f).At(0, 0),
           Rgba(128, 128, 255, 191));
  // A companion of another size is resampled to the albedo's.
  const Surface small(2, 2, Rgba(0, 0, 0));
  Surface sized = MakeSurfaceMap(albedo, nullptr, &small, 3.0f);
  CHECK_EQ(sized.w, 4);
  CHECK_EQ(sized.At(3, 3),
           Rgba(128, 128, 255, 255));  // roughness 0: fully glossy
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
  // Neither companion: empty, so the renderer shares one default (metal 0,
  // occlusion 1).
  CHECK(MakeMaterialMap(albedo, nullptr, nullptr).Empty());
  CHECK(MakeMaterialMap(Surface(), &albedo, &albedo).Empty());
  const Surface metal(4, 4, Rgba(220, 220, 220));
  const Surface ao(4, 4, Rgba(90, 90, 90));
  CHECK_EQ(MakeMaterialMap(albedo, &metal, &ao).At(1, 2), Rgba(220, 90, 255));
  CHECK_EQ(MakeMaterialMap(albedo, &metal, nullptr).At(0, 0),
           Rgba(220, 255, 255));  // fully open
  CHECK_EQ(MakeMaterialMap(albedo, nullptr, &ao).At(3, 3),
           Rgba(0, 90, 255));  // not metal
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
  CHECK(!lib.Has("clockface"));  // story art belongs to a game, not the engine
}

TEST(VfsPackRoundTripAndMount) {
  const uint64_t key = vfs::KeyFromString("secret");
  std::string big(5000, 'x');
  for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>(i * 7);
  const std::string pack = vfs::BuildPack({{"data/a.json", "{\"a\": 1}"},
                                           {"models/b.obj", big},
                                           {"models/c.obj", ""},
                                           {"top.txt", "t"}},
                                          key);
  CHECK(pack.find("a.json") ==
        std::string::npos);  // names are not readable in the pack
  CHECK(pack.find("{\"a\"") == std::string::npos);  // nor the contents

  vfs::UnmountAll();
  std::string err;
  CHECK(!vfs::MountMemory(pack, "pak://assets", vfs::KeyFromString("wrong"),
                          &err));  // wrong key
  CHECK(!vfs::MountMemory("junk", "pak://assets", key, &err));
  CHECK(vfs::MountMemory(pack, "pak://assets", key, &err));
  CHECK(vfs::AnyMounted());

  std::string out;
  CHECK(vfs::ReadFile("pak://assets/data/a.json", out));
  CHECK(out == std::string("{\"a\": 1}"));
  CHECK(vfs::ReadFile("pak://assets//models/b.obj",
                      out));  // a doubled slash still resolves
  CHECK(out == big);
  CHECK(vfs::ReadFile("pak://assets/models/c.obj", out));
  CHECK(out.empty());
  CHECK(!vfs::ReadFile("pak://assets/data/missing.json", out));
  CHECK(vfs::Exists("pak://assets/top.txt"));
  CHECK(!vfs::Exists("pak://assets/nope"));
  CHECK(!vfs::Exists(
      "pak://assetsx/top.txt"));  // a sibling prefix is not under the root

  const auto models = vfs::List("pak://assets/models");
  CHECK_EQ(models.size(), size_t(2));
  CHECK(models[0] == std::string("b.obj"));
  CHECK_EQ(vfs::List("pak://assets").size(),
           size_t(1));  // only top.txt: no subfolders listed

  // Paths outside every mount still read from the disk.
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "mythos_vfs_test.txt";
  {
    std::ofstream(tmp, std::ios::binary) << "disk";
  }
  CHECK(vfs::ReadFile(tmp.string(), out));
  CHECK(out == std::string("disk"));
  std::filesystem::remove(tmp);
  vfs::UnmountAll();
  CHECK(!vfs::AnyMounted());
}

TEST(VfsDetectsTampering) {
  const uint64_t key = vfs::KeyFromString("k");
  std::string pack =
      vfs::BuildPack({{"a.txt", "hello world, a longer line"}}, key);
  pack.back() =
      static_cast<char>(pack.back() ^ 0x40);  // flip a bit in the file's data
  vfs::UnmountAll();
  CHECK(vfs::MountMemory(pack, "pak://t", key));  // the table is intact
  std::string out;
  CHECK(!vfs::ReadFile("pak://t/a.txt", out));  // but the file fails its check
  std::string cut = vfs::BuildPack({{"a.txt", "hello"}}, key);
  cut.resize(cut.size() - 2);
  CHECK(!vfs::MountMemory(cut, "pak://u", key));  // truncated
  vfs::UnmountAll();
}

}  // namespace mythos

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
