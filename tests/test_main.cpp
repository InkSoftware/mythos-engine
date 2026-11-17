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

#include "engine/data/achievements.h"
#include "engine/audio/audio_math.h"
#include "engine/data/credits.h"
#include "engine/core/fileio.h"
#include "engine/gfx/flicker.h"
#include "engine/gfx/font.h"
#include "engine/data/i18n.h"
#include "engine/world/interact.h"
#include "engine/core/json.h"
#include "engine/core/json_reader.h"
#include "engine/world/level.h"
#include "engine/core/vfs.h"
#include "engine/core/mat4.h"
#include "engine/gfx/materials.h"
#include "engine/ui/menu.h"
#include "engine/gfx/mesh.h"
#include "engine/gfx/model.h"
#include "engine/gfx/normalmap.h"
#include "engine/world/movement.h"
#include "engine/core/noise.h"
#include "engine/world/player.h"
#include "engine/world/room_doors.h"
#include "engine/ui/settings.h"
#include "engine/audio/sound_bank.h"
#include "engine/gfx/surface.h"
#include "engine/gfx/text.h"
#include "engine/gfx/textures.h"
#include "engine/world/tiles.h"
#include "engine/data/translation.h"
#include "engine/ui/ui.h"

using namespace mythos;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int g_failures = 0;
int g_checks = 0;

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

#define TEST(name)                                  \
    static void name();                             \
    static Registrar name##_registrar(#name, name); \
    static void name()

#define CHECK(cond)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_failures;                                                     \
            std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                     \
    } while (0)

#define CHECK_EQ(a, b)                                                                                  \
    do {                                                                                                \
        ++g_checks;                                                                                     \
        auto va_ = (a);                                                                                 \
        auto vb_ = (b);                                                                                 \
        if (!(va_ == vb_)) {                                                                            \
            ++g_failures;                                                                               \
            std::printf("    FAILED %s:%d: %s == %s (got %lld, want %lld)\n", __FILE__, __LINE__, #a, #b, \
                        static_cast<long long>(va_), static_cast<long long>(vb_));                      \
        }                                                                                               \
    } while (0)

}  // namespace

TEST(overlay_takes_the_window_shape_at_a_whole_scale) {
    int w = 0, h = 0;
    overlaySize(960, 600, 320, 200, w, h);
    CHECK(w == 320 && h == 200);
    overlaySize(1920, 1080, 320, 200, w, h);  // 16:9 at scale 5
    CHECK(w == 384 && h == 216);
    overlaySize(1280, 720, 320, 200, w, h);   // scale 3
    CHECK(w == 426 && h == 240);
    overlaySize(800, 1000, 320, 200, w, h);   // taller than wide: scale 2
    CHECK(w == 400 && h == 500);
    overlaySize(100, 100, 320, 200, w, h);    // never below the page size
    CHECK(w == 320 && h == 200);
}

TEST(obj_parser_reads_quads_and_glow_materials) {
    const std::string obj =
        "o test\n"
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
        "vn 0 0 1\n"
        "usemtl glow_computer\n"
        "f 1/1/1 2/2/1 3/3/1 4/4/1\n";
    std::vector<Batch> out;
    std::string err;
    CHECK(parseObj(obj, out, &err));
    CHECK_EQ(out.size(), 1u);
    if (out.size() == 1) {
        CHECK(out[0].emissive);
        CHECK(out[0].texture == "computer");
        CHECK_EQ(out[0].verts.size(), 6u);  // a quad becomes two triangles
    }
}

TEST(obj_parser_rejects_bad_faces) {
    std::vector<Batch> out;
    std::string err;
    CHECK(!parseObj("v 0 0 0\nf 1 2 3\n", out, &err));
    CHECK(!err.empty());
}

TEST(zone_parser_rejects_bad_furniture) {
    Level l;
    std::string err;
    CHECK(!l.parseText("[map]\n...\n...\n[furniture]\n1 1 bed x\n", &err));
    CHECK(!err.empty());
}

TEST(font_measures_and_wraps_text) {
    CHECK_EQ(textWidth("AB", 1), 11);
    CHECK_EQ(textWidth("AB", 2), 22);
    CHECK_EQ(textWidth("", 1), 0);
    auto lines = wrapText("alpha beta gamma", 10);
    CHECK_EQ(lines.size(), 2u);
    if (lines.size() == 2) {
        CHECK(lines[0] == "alpha beta");
        CHECK(lines[1] == "gamma");
    }
}

TEST(font_draws_pixels_and_lowercase_matches_uppercase) {
    Surface a(40, 10, 0), b(40, 10, 0);
    drawText(a, 0, 0, "Hi", 0xFFFFFFFF);
    drawText(b, 0, 0, "HI", 0xFFFFFFFF);
    int lit = 0;
    for (uint32_t p : a.px) lit += p != 0;
    CHECK(lit > 0);
    CHECK(a.px == b.px);
}

TEST(engine_json_parses_values_and_reports_errors) {
    json::Value v;
    std::string err;
    // (The text is kept out of the CHECK: MSVC cannot stringize a raw string with quotes in a macro argument.)
    const std::string text = R"({"a": [1, -2.5e1, true, null], "s": "\u0393\u03b5 \"q\" \ud83d\ude00", "o": {}})";
    CHECK(json::parse(text, v, &err));
    CHECK(v.isObject());
    const json::Value* a = v.find("a");
    CHECK(a && a->isArray() && a->items().size() == 4);
    if (a && a->items().size() == 4) {
        CHECK(a->items()[1].asNumber() == -25.0);
        CHECK(a->items()[2].asBool());
        CHECK(a->items()[3].isNull());
    }
    CHECK(v.find("s") && v.find("s")->asString() == "Γε \"q\" \xF0\x9F\x98\x80");
    CHECK(v.members().size() == 3 && v.members()[2].first == "o");  // key order is kept
    CHECK(!json::parse("{\"a\": 1,\n \"a\": 2}", v, &err));      // duplicate key
    CHECK(err.find("line 2") == 0);
    CHECK(!json::parse("[1, 2", v, &err));
    CHECK(!json::parse("{'a': 1}", v, &err));
    CHECK(!json::parse("01", v, &err));
    CHECK(!json::parse("\"\\ud800\"", v, &err));  // unpaired surrogate
}

TEST(engine_tile_legend_loads_from_json) {
    json::Value v;
    const std::string legend = R"({"defaultWall": "stone", "tiles": {"#": {"solid": true}, "D": {"door": "open"},
                           "*": {"sprite": "star", "spriteSize": 0.5}}})";
    CHECK(json::parse(legend, v));
    TileSet t;
    std::string err;
    CHECK(parseTileSet(v, t, &err));
    CHECK(t.solid('#') && t.isOpenDoor('D') && !t.solid('D'));
    CHECK(t.spriteTexture('*') == "star" && t['*'].spriteSize == 0.5f);
    CHECK(t.wallTexture('#') == "stone");
    const std::string typo = R"({"tiles": {"#": {"solidd": true}}})";
    CHECK(json::parse(typo, v));
    CHECK(!parseTileSet(v, t, &err));
    CHECK(err.find("solidd") != std::string::npos);
}

TEST(engine_default_legend_knows_only_the_hash_wall) {
    Level l;
    CHECK(l.parse({"#####", "#WLI#", "#####"}));
    CHECK(!l.passable(0, 0));
    CHECK(l.passable(1, 1));  // 'W' means nothing without the game's legend
    CHECK(l.passable(2, 1));
    auto tiles = std::make_shared<TileSet>();
    (*tiles)['W'].solid = true;
    l.setTiles(tiles);
    CHECK(!l.passable(1, 1));
    CHECK(l.passable(2, 1));
    Level copy = l;  // copies keep their legend
    CHECK(!copy.passable(1, 1));
}

TEST(engine_collision_slides_along_walls) {
    Level l;
    CHECK(l.parse({"#####", "#...#", "#...#", "#####"}));
    Vec2 p{1.5, 1.5};
    moveWithCollision(l, p, -1.0, 0.5, 0.22);  // into the west wall, and south
    CHECK(p.x == 1.5);                         // blocked on x
    CHECK(std::fabs(p.y - 2.0) < 1e-9);        // slid on y
}

TEST(engine_field_walker_reaches_the_goal) {
    Level l;
    CHECK(l.parse({"#######", "#.....#", "###.###", "#.....#", "#######"}));
    const std::vector<int> field = l.distanceField(5, 3);
    Vec2 p{1.5, 1.5};
    FieldWalker w;
    for (int i = 0; i < 100; ++i) walkField(l, field, p, w, 0.25);
    CHECK(std::fabs(p.x - 5.5) < 1e-9);
    CHECK(std::fabs(p.y - 3.5) < 1e-9);
}

TEST(engine_ray_hits_sphere) {
    const Vec3 eye{0, 0, 0}, ahead{1, 0, 0};
    CHECK(rayHitsSphere(eye, ahead, {3, 0, 0}, 0.5, 10) == 3.0);
    CHECK(rayHitsSphere(eye, ahead, {3, 0.4, 0}, 0.5, 10) > 0);   // grazes it
    CHECK(rayHitsSphere(eye, ahead, {3, 0.6, 0}, 0.5, 10) < 0);   // misses it
    CHECK(rayHitsSphere(eye, ahead, {-3, 0, 0}, 0.5, 10) < 0);    // behind
    CHECK(rayHitsSphere(eye, ahead, {30, 0, 0}, 0.5, 10) < 0);    // out of reach
}

TEST(engine_walk_step) {
    const Vec2 fwd = walkStep(0, 1, 0, 2.0);
    CHECK(std::abs(fwd.x - 2.0) < 1e-9 && std::abs(fwd.y) < 1e-9);
    const Vec2 right = walkStep(0, 0, 1, 1.0);  // strafing right at angle 0 goes +y
    CHECK(std::abs(right.x) < 1e-9 && std::abs(right.y - 1.0) < 1e-9);
    const Vec2 diag = walkStep(0, 1, 1, 1.0);
    CHECK(std::hypot(diag.x, diag.y) < 1.001);
}

TEST(engine_noise_is_deterministic) {
    CHECK(hnoise(3, 4, 7) == hnoise(3, 4, 7));
    CHECK(hnoise(3, 4, 7) >= 0.0 && hnoise(3, 4, 7) < 1.0);
    CHECK(grain(1, 2, 3, 10) >= -10 && grain(1, 2, 3, 10) <= 10);
}

TEST(engine_file_io_round_trip) {
    const std::string path = (std::filesystem::temp_directory_path() / "mythos_fileio_test.txt").string();
    CHECK(writeFileAtomic(path, "hello"));
    CHECK(readTextFile(path) == std::string("hello"));
    CHECK(writeFileAtomic(path, "", true));  // empty removes it
    CHECK(readTextFile(path) == std::string());
}

TEST(engine_json_reader_reports_first_error) {
    json::Value root;
    CHECK(json::parse("{\"a\": {\"n\": 3, \"s\": \"x\", \"c\": [1, 2]}}", root));
    json::Reader r(root, "t.json");
    CHECK_EQ(r.integer("a.n"), 3);
    CHECK(r.string("a.s") == std::string("x"));
    int x = 0, y = 0;
    r.cell("a.c", x, y);
    CHECK(x == 1 && y == 2 && r.ok());
    r.number("a.missing");
    r.number("a.also");
    CHECK(!r.ok());
    CHECK(r.error().find("a.missing") != std::string::npos);
}

TEST(engine_ui_alpha_and_hit) {
    CHECK_EQ(withAlpha(0xFF123456, 0.5), 0x7F123456u);
    Surface s(4, 4);
    fillRect(s, -2, -2, 2, 2, 0xFFFFFFFF);
    CHECK_EQ(s.at(1, 1), 0xFFFFFFFFu);
    CHECK_EQ(s.at(2, 2), 0u);
}

TEST(runtime_walker_steps_once_per_stride) {
    Level l;
    l.parse({"#####", "#...#", "#####"});
    Player p;
    p.pos = {1.5, 1.5};
    p.angle = 0;
    PlayerParams params;
    params.walkSpeed = 1.0;
    params.runSpeed = 2.0;
    params.strideLength = 0.5;
    MoveInput in;
    in.forward = true;
    PlayerWalker w;
    int steps = 0;
    for (int i = 0; i < 6; ++i)
        if (w.update(l, p, 0.1, in, params)) ++steps;
    CHECK_EQ(steps, 1);  // 0.6 walked, one 0.5 stride
    CHECK(p.pos.x > 2.0 && p.pos.x < 2.2);
    MoveInput idle;
    CHECK(!w.update(l, p, 0.1, idle, params));
}

TEST(runtime_look_clamps_pitch) {
    Player p;
    PlayerParams params;
    params.mouseSensitivity = 0.01;
    params.maxPitch = 1.0;
    MoveInput in;
    in.mouseDy = -1000;
    applyLook(p, in, params);
    CHECK_EQ(p.pitch, 1.0);
}

TEST(runtime_findAimed_picks_nearest_visible_enabled) {
    Level l;
    l.parse({"#######", "#.....#", "#######"});
    Player p;
    p.pos = {1.5, 1.5};
    p.angle = 0;
    std::vector<AimCandidate> c = {{{4.5, 1.5}, 1.0, 0.3, true}, {{3.0, 1.5}, 1.0, 0.3, true}, {{2.5, 1.5}, 1.0, 0.3, false}};
    CHECK_EQ(findAimed(l, p, 1.0, 5.0, c), 1);   // nearest enabled
    CHECK_EQ(findAimed(l, p, 1.0, 1.0, c), -1);  // out of reach
    p.angle = kPi;                               // looking away
    CHECK_EQ(findAimed(l, p, 1.0, 5.0, c), -1);
}

TEST(runtime_verbs) {
    Verb v = Verb::Take;
    CHECK(parseVerb("read", v) && v == Verb::Read);
    CHECK(parseVerb("listen", v) && v == Verb::Listen);
    CHECK(!parseVerb("lick", v) && v == Verb::Listen);  // unchanged on failure
    Prop p;
    p.id = "thing";
    Interactor it;
    p.verb = Verb::Read;
    CHECK(it.use(p, false, "", 0.1, false, false, 1.0) == Use::None);
    CHECK(it.use(p, false, "", 0.1, true, true, 1.0) == Use::Read);
    p.verb = Verb::Inspect;
    CHECK(it.use(p, false, "", 0.1, true, true, 1.0) == Use::Inspect);
    // The empty place of the held prop puts it back, whatever the verb.
    CHECK(it.use(p, true, "thing", 0.1, true, true, 1.0) == Use::PutBack);
    CHECK(it.use(p, true, "thing", 0.1, false, false, 1.0) == Use::None);
}

TEST(runtime_take_needs_a_free_hand) {
    Prop p;
    p.id = "key";
    p.verb = Verb::Take;
    Interactor it;
    CHECK(it.use(p, false, "", 0.1, true, true, 1.0) == Use::Take);
    CHECK(it.use(p, false, "lamp", 0.1, true, true, 1.0) == Use::HandsFull);
    CHECK(it.use(p, false, "", 0.1, false, false, 1.0) == Use::None);
}

TEST(runtime_listen_holds_then_waits_for_release) {
    Prop p;
    p.id = "radio";
    p.verb = Verb::Listen;
    Interactor it;
    CHECK(it.use(p, false, "", 0.4, false, false, 1.0) == Use::None);  // not holding
    CHECK(it.use(p, false, "", 0.4, false, true, 1.0) == Use::Listening);
    CHECK(it.listenTime() > 0.39);
    CHECK(it.use(p, false, "", 0.4, false, true, 1.0) == Use::Listening);
    CHECK(it.use(p, false, "", 0.4, false, true, 1.0) == Use::Heard);
    CHECK(it.use(p, false, "", 0.4, false, true, 1.0) == Use::None);   // still holding: no repeat
    it.release(false);
    CHECK(it.use(p, false, "", 0.4, false, true, 1.0) == Use::Listening);
    it.idle();
    CHECK_EQ(it.listenTime(), 0.0);
}

TEST(runtime_effects_write_the_progress) {
    Progress pr;
    Effects e;
    e.notebook = "notebook.x";
    e.clue = "clue.a";
    e.flags["seen"] = 1;
    CHECK(applyEffects(pr, e));
    CHECK(!applyEffects(pr, e));  // the clue is no longer new
    CHECK_EQ(pr.notebook.size(), 1u);
    CHECK_EQ(pr.clues.size(), 1u);
    CHECK(pr.flag("seen") && !pr.flag("nope"));
}

TEST(runtime_prop_override_and_placement) {
    Prop p;
    p.id = "mug";
    p.at = {4.75, 2.25};
    p.lift = 0.8;
    PropOverride o;
    o.model = "mug2";
    o.yawDeg = 90;
    o.applyTo(p);
    CHECK(p.model == "mug2");
    CHECK_EQ(p.yawDeg, 90.0);
    CHECK_EQ(p.lift, 0.8);  // not given: unchanged
    const Placement pl = placementOf(p);
    CHECK(pl.x == 4 && pl.y == 2 && !pl.solid && pl.id == "mug");
    CHECK(pl.offX > 0.24 && pl.offX < 0.26 && pl.offZ < -0.24 && pl.offZ > -0.26);
}

TEST(runtime_menu_step_wraps_skips_and_activates) {
    int index = 0;
    UiInput in;
    auto usable = [](int i) { return i != 1; };
    in.downPressed = true;
    CHECK_EQ(menuStep(index, 4, in, false, usable), -1);
    CHECK_EQ(index, 2);  // item 1 is skipped
    index = 3;
    CHECK_EQ(menuStep(index, 4, in, false, usable), -1);
    CHECK_EQ(index, 0);  // wraps
    in = UiInput{};
    in.upPressed = true;
    menuStep(index, 4, in, false, usable);
    CHECK_EQ(index, 3);  // 0 -> back round to 3
    in = UiInput{};
    CHECK_EQ(menuStep(index, 4, in, true, usable), 3);  // activate chooses the selection
    in.hoverItem = 1;
    menuStep(index, 4, in, false, usable);
    CHECK_EQ(index, 3);  // hovering an unusable item does nothing
    in.hoverItem = 2;
    menuStep(index, 4, in, false, usable);
    CHECK_EQ(index, 2);
    in = UiInput{};
    in.clickItem = 0;
    CHECK_EQ(menuStep(index, 4, in, false, usable), 0);
    in.clickItem = 1;
    CHECK_EQ(menuStep(index, 4, in, false, usable), -1);  // an unusable item cannot be clicked
}

TEST(runtime_settings_page_keys_clicks_and_back) {
    GameSettings s;
    SettingsPage page = SettingsPage::Main;
    int index = kSetGraphics;
    UiInput in;
    in.confirmPressed = true;
    CHECK(!updateSettingsPage(s, page, index, in, false, 2));  // GRAPHICS opens its page
    CHECK(page == SettingsPage::Graphics);
    CHECK_EQ(index, kSetResolution);
    in = UiInput{};
    in.rightPressed = true;
    for (int i = 0; i < 6; ++i) updateSettingsPage(s, page, index, in, false, 2);
    CHECK_EQ(s.resolution, 2);  // keys stop at the display's maximum
    in = UiInput{};
    in.clickItem = kSetResolution;
    in.clickStep = 1;
    updateSettingsPage(s, page, index, in, false, 2);
    CHECK_EQ(s.resolution, 0);  // clicks cycle round
    in = UiInput{};
    in.clickItem = kSetMaster;  // not a row of this page: ignored
    updateSettingsPage(s, page, index, in, false, 2);
    CHECK(page == SettingsPage::Graphics && index == kSetResolution);
    index = kSetFullscreen;
    in = UiInput{};
    in.confirmPressed = true;
    updateSettingsPage(s, page, index, in, false, 2);
    CHECK(s.fullscreen);
    in = UiInput{};
    in.pausePressed = true;  // Esc on a sub-page goes back to the main page, on its link
    CHECK(!updateSettingsPage(s, page, index, in, false, 2));
    CHECK(page == SettingsPage::Main && index == kSetGraphics);

    in = UiInput{};
    in.clickItem = kSetAudio;
    updateSettingsPage(s, page, index, in, false, 2);
    CHECK(page == SettingsPage::Audio);
    in = UiInput{};
    in.clickItem = kSetMaster;
    in.clickValue = 4;
    updateSettingsPage(s, page, index, in, false, 2);
    CHECK_EQ(s.masterVolume, 4);
    updateSettingsPage(s, page, index, in, false, 2);
    CHECK_EQ(s.masterVolume, 3);  // clicking the lit last pip again turns it down
    in = UiInput{};
    in.clickItem = kSetBack;
    CHECK(!updateSettingsPage(s, page, index, in, false, 2));  // Back: to the main page
    CHECK(page == SettingsPage::Main && index == kSetAudio);
    index = kSetLanguage;
    in = UiInput{};
    in.confirmPressed = true;
    updateSettingsPage(s, page, index, in, false, 2);
    CHECK(s.language == Lang::Greek);
    index = kSetBack;
    CHECK(updateSettingsPage(s, page, index, in, false, 2));  // Back on the main page closes
    in = UiInput{};
    in.pausePressed = true;
    CHECK(updateSettingsPage(s, page, index, in, false, 2));  // so does Esc
    setLanguage(Lang::English);
}

TEST(runtime_settings_round_trip_through_key_values) {
    GameSettings a;
    a.resolution = resolutionIndex(1920, 1080);
    a.renderScale = 5;
    a.filmGrain = false;
    a.antiAlias = 4;
    a.shadows = 5;
    a.motionBlur = 4;  // 100%
    a.cameraEffect = false;
    a.vsync = false;
    a.fpsCap = 4;  // 120
    a.fullscreen = true;
    a.masterVolume = 3;
    a.language = Lang::Greek;
    GameSettings b;
    applyKeyValues(b, toKeyValues(a));
    CHECK_EQ(b.resolution, a.resolution);
    CHECK_EQ(b.renderScale, 5);
    CHECK(!b.filmGrain && b.fullscreen);
    CHECK_EQ(b.antiAlias, 4);
    CHECK_EQ(b.shadows, 5);
    CHECK(b.motionBlur == 4 && !b.cameraEffect && !b.vsync);
    CHECK_EQ(b.fpsCap, 4);
    CHECK_EQ(b.masterVolume, 3);
    CHECK(b.language == Lang::Greek);
    GameSettings c;
    applyKeyValues(c, {{"masterVolume", "99"}, {"renderScale", "-4"}, {"nonsense", "1"}, {"resolution", "bad"}});
    CHECK_EQ(c.masterVolume, 10);  // clamped
    CHECK_EQ(c.renderScale, 0);
    CHECK_EQ(c.resolution, 0);     // unparsable: unchanged
    GameSettings d;
    applyKeyValues(d, {{"antiAliasing", "99"}, {"fpsCap", "77"}, {"shadowMaps", "9"}, {"motionBlurScale", "33"}});
    CHECK_EQ(d.antiAlias, kAntiAliasCount - 1);
    CHECK_EQ(d.fpsCap, 0);  // not an offered limit: none
    CHECK_EQ(d.shadows, kShadowLevelCount - 1);
    CHECK_EQ(d.motionBlur, 0);  // not an offered strength: off
}

TEST(runtime_settings_page_new_rows) {
    GameSettings s;
    SettingsPage page = SettingsPage::Graphics;
    int index = kSetVsync;
    UiInput in;
    in.confirmPressed = true;
    updateSettingsPage(s, page, index, in, false, 2);
    CHECK(!s.vsync);
    index = kSetFpsCap;
    in = UiInput{};
    in.rightPressed = true;
    for (int i = 0; i < 20; ++i) updateSettingsPage(s, page, index, in, false, 2);
    CHECK_EQ(s.fpsCap, kFpsCapCount - 1);  // keys stop at the end
    index = kSetShadows;
    in = UiInput{};
    in.leftPressed = true;
    for (int i = 0; i < 9; ++i) updateSettingsPage(s, page, index, in, false, 2);
    CHECK_EQ(s.shadows, 0);
    index = kSetMotionBlur;
    in = UiInput{};
    in.rightPressed = true;
    for (int i = 0; i < 3; ++i) updateSettingsPage(s, page, index, in, false, 2);
    CHECK_EQ(s.motionBlur, 3);
    index = kSetRenderScale;
    for (int i = 0; i < 20; ++i) updateSettingsPage(s, page, index, in, false, 2);
    CHECK_EQ(s.renderScale, kRenderScaleCount - 1);
    index = kSetAntiAlias;
    for (int i = 0; i < 9; ++i) updateSettingsPage(s, page, index, in, false, 2);
    CHECK_EQ(s.antiAlias, kAntiAliasCount - 1);
    index = kSetCameraEffect;
    in = UiInput{};
    in.confirmPressed = true;
    updateSettingsPage(s, page, index, in, false, 2);
    CHECK(!s.cameraEffect);
    // Every page ends with Back, and up / down wrap round its rows.
    for (SettingsPage p : {SettingsPage::Main, SettingsPage::Graphics, SettingsPage::Audio})
        CHECK_EQ(settingsPageItems(p).back(), kSetBack);
    page = SettingsPage::Audio;
    index = kSetMaster;
    in = UiInput{};
    in.upPressed = true;
    updateSettingsPage(s, page, index, in, false, 2);
    CHECK_EQ(index, kSetBack);
}

TEST(runtime_engine_strings_cover_both_languages_and_yield_to_the_game) {
    const TranslationTable& en = engineStrings(Lang::English);
    const TranslationTable& el = engineStrings(Lang::Greek);
    CHECK_EQ(en.size(), el.size());
    for (const auto& [key, text] : en) CHECK(el.count(key) == 1);
    setLanguage(Lang::English);
    setTranslations(Lang::English, nullptr);
    CHECK(tr("settings.vsync") == "VSYNC");  // no game table: the engine's own
    CHECK(tr("common.on") == "ON");
    TranslationTable game = {{"settings.vsync", "SYNC"}};
    setTranslations(Lang::English, &game);
    CHECK(tr("settings.vsync") == "SYNC");  // the game's wins
    CHECK(tr("settings.title") == "SETTINGS");
    setTranslations(Lang::English, nullptr);
}

namespace {
json::Value parseJson(const std::string& text) {
    json::Value v;
    std::string err;
    CHECK(json::parse(text, v, &err));
    return v;
}
}  // namespace

TEST(runtime_data_loaders_read_the_documented_formats) {
    std::string err;
    {  // achievements
        const json::Value v = parseJson(R"({"achievements":{"_c":"x","a":{"event":"loops","count":3}}})");
        std::vector<AchievementDef> defs;
        CHECK(loadAchievementDefs(v, "f", defs, &err));
        CHECK_EQ(defs.size(), 1u);
        CHECK(defs[0].id == "a" && defs[0].count == 3 && defs[0].name == "achievements.a.name");
        const json::Value bad = parseJson(R"({"achievements":{"a":{"event":"loops","count":0}}})");
        std::vector<AchievementDef> none;
        CHECK(!loadAchievementDefs(bad, "f", none, &err));
    }
    {  // player numbers
        const json::Value v = parseJson(R"({"player":{"radius":0.2,"walkSpeed":2,"runSpeed":3,"turnSpeed":2.5,
            "strideLength":0.8,"mouseSensitivity":0.002,"maxPitch":1.1}})");
        json::Reader r(v, "f");
        PlayerParams p;
        CHECK(loadPlayerParams(r, "player", p));
        CHECK(p.radius == 0.2 && p.runSpeed == 3 && p.maxPitch == 1.1);
        const json::Value missing = parseJson(R"({"player":{"radius":0.2}})");
        json::Reader r2(missing, "f");
        PlayerParams q;
        CHECK(!loadPlayerParams(r2, "player", q));
        CHECK(r2.error().find("player.walkSpeed") != std::string::npos);
    }
    {  // sounds
        const json::Value v = parseJson(R"({"events":{"Step":{"clips":["a","b"],"gain":0.5,"jitter":true}},
            "ambience":{"house":{"clip":"h","gain":0.3}},
            "voice":{"dir":"voices","gain":1,"duck":0.4,"radioFalloff":0.1}})");
        SoundBank bank;
        CHECK(loadSoundBank(v, "f", bank, {"Step"}, {"house"}, &err));
        CHECK(bank.events.at("Step").jitter && bank.events.at("Step").clips.size() == 2);
        CHECK_EQ(bank.allClips().size(), 3u);
        SoundBank other;
        CHECK(!loadSoundBank(v, "f", other, {"Missing"}, {"house"}, &err));
        CHECK(!loadSoundBank(v, "f", other, {"Step"}, {"car"}, &err));
    }
    {  // translations
        const json::Value v = parseJson(R"({"language":"en","name":"ENGLISH","strings":{"hud":{"a":"A"},"b":"B"}})");
        Translation t;
        CHECK(loadTranslation(v, "f", t, &err));
        CHECK(t.strings.at("hud.a") == "A" && t.strings.at("b") == "B");
    }
    {  // credits
        const json::Value v = parseJson(R"({"sections":[{"title":"t","entries":[
            {"name":"n","by":"b","license":"CC0","what":""}]}]})");
        std::vector<CreditSection> secs;
        CHECK(loadCredits(v, "f", secs, &err));
        CHECK(secs.size() == 1 && secs[0].entries[0].license == "CC0");
    }
}

namespace {
// m * (x, y, z, 1), column-major.
void transform(const float* m, float x, float y, float z, float out[4]) {
    for (int r = 0; r < 4; ++r) out[r] = m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r];
}
}  // namespace

TEST(runtime_mat4_view_projection_and_frustum) {
    const float eye[3] = {2, 1.5f, 3}, center[3] = {2, 1.5f, -2}, up[3] = {0, 1, 0};
    float view[16], proj[16], vp[16], out[4];
    mat4LookAt(eye, center, up, view);
    transform(view, 2, 1.5f, -2, out);  // five metres straight ahead: (0, 0, -5)
    CHECK(std::abs(out[0]) < 1e-5f && std::abs(out[1]) < 1e-5f && std::abs(out[2] + 5.0f) < 1e-5f);
    mat4Perspective(1.2f, 1.6f, 0.05f, 40.0f, proj);
    mat4Multiply(proj, view, vp);
    transform(vp, 2, 1.5f, -2, out);  // the centre of the picture, inside the depth range
    CHECK(std::abs(out[0] / out[3]) < 1e-5f && std::abs(out[1] / out[3]) < 1e-5f);
    CHECK(out[2] / out[3] > -1.0f && out[2] / out[3] < 1.0f);
    Frustum planes;
    frustumPlanes(vp, planes);
    const float inMin[3] = {1.9f, 1.4f, -2.1f}, inMax[3] = {2.1f, 1.6f, -1.9f};
    CHECK(boxInFrustum(planes, inMin, inMax));
    const float behindMin[3] = {1.9f, 1.4f, 5.0f}, behindMax[3] = {2.1f, 1.6f, 6.0f};
    CHECK(!boxInFrustum(planes, behindMin, behindMax));
    CHECK(!boxInFrustum(planes, inMin, inMax, -100.0f));  // shifted far out of view
    const float farMin[3] = {1.9f, 1.4f, -90.0f}, farMax[3] = {2.1f, 1.6f, -80.0f};
    CHECK(!boxInFrustum(planes, farMin, farMax));  // beyond the far plane
}

TEST(runtime_cube_faces_look_down_their_axis) {
    const float eye[3] = {4, 1.2f, -3};
    const float dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        float view[16], out[4];
        cubeFaceView(f, eye, view);
        transform(view, eye[0] + 3 * dirs[f][0], eye[1] + 3 * dirs[f][1], eye[2] + 3 * dirs[f][2], out);
        CHECK(std::abs(out[0]) < 1e-5f && std::abs(out[1]) < 1e-5f && std::abs(out[2] + 3.0f) < 1e-5f);
    }
    // GL's cube sampling: +X's right is -Z and its up is -Y; +Z's right is +X (seen from the eye, looking out).
    float view[16], out[4];
    cubeFaceView(0, eye, view);
    transform(view, eye[0] + 1, eye[1], eye[2] - 1, out);  // a point towards -Z sits on the right of +X's picture
    CHECK(out[0] > 0.5f);
    cubeFaceView(4, eye, view);
    transform(view, eye[0] + 1, eye[1], eye[2] + 1, out);  // and towards +X on the right of +Z's
    CHECK(out[0] > 0.5f);
}

TEST(runtime_positional_gain_pans_fades_and_hushes) {
    const Vec2 me{5, 5};
    // Facing +x: a source ahead is centred; one to the right (+z, south) leans right; to the left, left.
    const StereoGain ahead = positionalGain(me, 0.0, {8, 5}, 0);
    CHECK(std::abs(ahead.left - ahead.right) < 1e-4f);
    const StereoGain onRight = positionalGain(me, 0.0, {5, 8}, 0);
    const StereoGain onLeft = positionalGain(me, 0.0, {5, 2}, 0);
    CHECK(onRight.right > onRight.left);
    CHECK(onLeft.left > onLeft.right);
    CHECK(onRight.left > 0 && onLeft.right > 0);  // the pan is soft, never hard
    // Farther is quieter.
    const StereoGain near = positionalGain(me, 0.0, {6, 5}, 0);
    const StereoGain far = positionalGain(me, 0.0, {15, 5}, 0);
    CHECK(near.left > far.left);
    // A hushed source falls silent close up, and a muted one always.
    const StereoGain hushed = positionalGain(me, 0.0, {6, 5}, 2.0);
    CHECK(hushed.left == 0.0f && hushed.right == 0.0f);
    CHECK(positionalGain(me, 0.0, {8, 5}, 2.0).left > 0.0f);
    CHECK(positionalGain(me, 0.0, {6, 5}, 0, true).left == 0.0f);
    // A smaller falloff carries: the same distance is louder, yet near is still louder than far.
    const StereoGain carries = positionalGain(me, 0.0, {15, 5}, 0, false, 1.0, 0.1);
    CHECK(carries.left > far.left * 5.0f);
    CHECK(positionalGain(me, 0.0, {6, 5}, 0, false, 1.0, 0.1).left > carries.left);
    CHECK(positionalGain(me, 0.0, {15, 5}, 0).left == positionalGain(me, 0.0, {15, 5}, 0, false, 0.75, 0.5).left);  // the defaults
}

TEST(runtime_cue_pick_keeps_the_draw_order) {
    SoundCue one;
    one.clips = {"a"};
    one.pitch = 1.5f;
    SoundCue many;
    many.clips = {"a", "b", "c"};
    many.jitter = true;
    Rng r1(7), r2(7);
    const CuePick p1 = pickCue(one, r1);
    CHECK_EQ(p1.clip, 0u);
    CHECK_EQ(p1.pitch, 1.5f);                // no jitter: the cue's own pitch
    const float jitterDraw = r2.next();       // one draw for a single clip, as the footsteps always did
    CHECK_EQ(r1.next(), r2.next());           // ...so the streams stay in step
    (void)jitterDraw;
    Rng r3(7);
    for (int i = 0; i < 200; ++i) {
        const CuePick p = pickCue(many, r3);
        CHECK(p.clip < 3);
        CHECK(p.pitch >= 0.92f && p.pitch <= 1.08f);
    }
}

TEST(runtime_flicker_matches_its_formula_and_stays_in_range) {
    CHECK_EQ(lampGain(12.3, 0, 0.0), 1.0);  // steady
    int dark = 0;
    for (int slot = 0; slot < 2000; ++slot) {
        const double t = slot / 16.0 + 0.01;
        const uint32_t h = mixSlot(static_cast<uint32_t>(t * 16.0) * 2654435761u + 2u * 97u);
        const bool drop = (h & 0xFFFF) / 65535.0 < 0.8 * 0.5;
        const double g = lampGain(t, 2, 0.8);
        CHECK(g > 0.0 && g < 1.01);
        if (drop) {
            CHECK(g < 0.14);
            ++dark;
        }
    }
    CHECK(dark > 500 && dark < 1100);  // about 40% of slots at flicker 0.8
    int lit = 0;
    for (int i = 0; i < 2000; ++i) lit += torchLit(i / 14.0 + 0.01, 0.5) ? 1 : 0;
    CHECK(lit > 1300 && lit < 1700);  // about 75% lit
    CHECK(torchLit(3.0, 0.0));        // no restlessness: always lit
}

// ---- room doors need a legend that has doors: N closed, n open.

namespace {
TileSetPtr doorTiles() {
    json::Value root;
    std::string err;
    const std::string legend = R"({"defaultWall": "brick", "defaultFloor": "wood", "tiles": {
        "#": {"solid": true}, "N": {"solid": true, "door": "closed"}, "n": {"door": "open"}}})";
    CHECK(json::parse(legend, root, &err));
    auto set = std::make_shared<TileSet>();
    CHECK(parseTileSet(root, *set, &err));
    return set;
}

Level doorLevel() {
    Level l;
    l.setTiles(doorTiles());
    // A small room on the right, a corridor on the left, a door N between them.
    l.parse({"#########", "#...#...#", "#...N...#", "#...#...#", "#########"});
    return l;
}
}  // namespace

TEST(runtime_room_door_swings_into_the_smaller_space_and_closes) {
    Level l = doorLevel();
    RoomDoors doors;
    doors.scan(l);
    std::vector<DoorState> st;
    doors.appendStates(st);
    CHECK_EQ(st.size(), 1u);
    CHECK(st[0].passageAlongX);  // walls north and south of it: you walk through along x
    CHECK_EQ(doors.use(4, 2), 1);
    CHECK_EQ(doors.use(9, 9), 0);
    Vec2 far{1.5, 1.5};
    doors.update(l, 1.0, far, 0.25, 0.5, 0.5);
    CHECK_EQ(l.at(4, 2), 'n');
    CHECK_EQ(doors.use(4, 2), -1);
    CHECK_EQ(doors.update(l, 1.0, far, 0.25, 0.5, 0.5), 1);
    CHECK_EQ(l.at(4, 2), 'N');
}

TEST(runtime_room_door_never_shuts_on_the_player) {
    Level l = doorLevel();
    RoomDoors doors;
    doors.scan(l);
    doors.use(4, 2);
    Vec2 inside{4.5, 2.5};
    doors.update(l, 1.0, inside, 0.25, 0.5, 0.5);
    doors.use(4, 2);  // close it
    for (int i = 0; i < 20; ++i) doors.update(l, 0.1, inside, 0.25, 0.5, 0.5);
    CHECK_EQ(l.at(4, 2), 'n');  // waits, ajar
    Vec2 out{1.5, 2.5};
    for (int i = 0; i < 20; ++i) doors.update(l, 0.1, out, 0.25, 0.5, 0.5);
    CHECK_EQ(l.at(4, 2), 'N');
}

TEST(normal_map_is_flat_for_a_flat_texture_and_tilts_with_a_slope) {
    Surface flat(8, 8, rgba(120, 120, 120));
    Surface nf = makeNormalMap(flat, 3.0f);
    CHECK_EQ(nf.w, 8);
    for (uint32_t p : nf.px) CHECK_EQ(p, rgba(128, 128, 255));

    // Brightness rising to the right (+u): the surface leans back towards -x.
    Surface ramp(8, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) ramp.at(x, y) = rgba(x * 30, x * 30, x * 30);
    Surface nr = makeNormalMap(ramp, 3.0f);
    const uint32_t p = nr.at(4, 4);
    CHECK(((p >> 16) & 0xFF) < 120);                // x tilts negative
    CHECK(std::abs(static_cast<int>((p >> 8) & 0xFF) - 128) <= 1);  // no slope in y
    CHECK((p & 0xFF) > 128);                        // still faces outwards
    // No strength, no tilt.
    CHECK_EQ(makeNormalMap(ramp, 0.0f).at(4, 4), rgba(128, 128, 255));

    // Brightness rising down the image (towards -v): y tilts positive (+v is up).
    Surface down(8, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) down.at(x, y) = rgba(y * 30, y * 30, y * 30);
    CHECK(((makeNormalMap(down, 3.0f).at(4, 4) >> 8) & 0xFF) > 136);
    CHECK(makeNormalMap(Surface(), 1.0f).empty());
}

TEST(engine_common_materials_stand_alone) {
    TextureLibrary lib;
    addCommonMaterials(lib);
    for (const char* n : {"brick", "wood", "tile", "carpet", "stone", "grass", "door", "ceiling", "void", "poster"})
        CHECK(lib.has(n));
    CHECK(lib.isPlaceholder("poster"));
    CHECK(!lib.has("clockface"));  // story art belongs to a game, not the engine
}

TEST(vfs_pack_round_trip_and_mount) {
    using namespace mythos;
    const uint64_t key = vfs::keyFromString("secret");
    std::string big(5000, 'x');
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>(i * 7);
    const std::string pack = vfs::buildPack({{"data/a.json", "{\"a\": 1}"}, {"models/b.obj", big}, {"models/c.obj", ""}, {"top.txt", "t"}}, key);
    CHECK(pack.find("a.json") == std::string::npos);  // names are not readable in the pack
    CHECK(pack.find("{\"a\"") == std::string::npos);  // nor the contents

    vfs::unmountAll();
    std::string err;
    CHECK(!vfs::mountMemory(pack, "pak://assets", vfs::keyFromString("wrong"), &err));  // wrong key
    CHECK(!vfs::mountMemory("junk", "pak://assets", key, &err));
    CHECK(vfs::mountMemory(pack, "pak://assets", key, &err));
    CHECK(vfs::anyMounted());

    std::string out;
    CHECK(vfs::readFile("pak://assets/data/a.json", out));
    CHECK(out == std::string("{\"a\": 1}"));
    CHECK(vfs::readFile("pak://assets//models/b.obj", out));  // a doubled slash still resolves
    CHECK(out == big);
    CHECK(vfs::readFile("pak://assets/models/c.obj", out));
    CHECK(out.empty());
    CHECK(!vfs::readFile("pak://assets/data/missing.json", out));
    CHECK(vfs::exists("pak://assets/top.txt"));
    CHECK(!vfs::exists("pak://assets/nope"));
    CHECK(!vfs::exists("pak://assetsx/top.txt"));  // a sibling prefix is not under the root

    const auto models = vfs::list("pak://assets/models");
    CHECK_EQ(models.size(), size_t(2));
    CHECK(models[0] == std::string("b.obj"));
    CHECK_EQ(vfs::list("pak://assets").size(), size_t(1));  // only top.txt: no subfolders listed

    // Paths outside every mount still read from the disk.
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "mythos_vfs_test.txt";
    {
        std::ofstream(tmp, std::ios::binary) << "disk";
    }
    CHECK(vfs::readFile(tmp.string(), out));
    CHECK(out == std::string("disk"));
    std::filesystem::remove(tmp);
    vfs::unmountAll();
    CHECK(!vfs::anyMounted());
}

TEST(vfs_detects_tampering) {
    using namespace mythos;
    const uint64_t key = vfs::keyFromString("k");
    std::string pack = vfs::buildPack({{"a.txt", "hello world, a longer line"}}, key);
    pack.back() = static_cast<char>(pack.back() ^ 0x40);  // flip a bit in the file's data
    vfs::unmountAll();
    CHECK(vfs::mountMemory(pack, "pak://t", key));  // the table is intact
    std::string out;
    CHECK(!vfs::readFile("pak://t/a.txt", out));  // but the file fails its check
    std::string cut = vfs::buildPack({{"a.txt", "hello"}}, key);
    cut.resize(cut.size() - 2);
    CHECK(!vfs::mountMemory(cut, "pak://u", key));  // truncated
    vfs::unmountAll();
}

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (const auto& t : registry()) {
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) continue;
        const int before = g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
        ++ran;
    }
    std::printf("\n%d test(s), %d check(s), %d failure(s)\n", ran, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
