#define _CRT_SECURE_NO_WARNINGS  // std::getenv

// Platformer — the roadmap-v2 "forcing-function" game (P5+P6+P7).
//
// Pulls together: the ENGINE'S okn-physics CharacterController (a kinematic capsule
// with collide-and-slide + auto-step + grounded query — no hand-rolled raycast) and
// an INPUT ACTION-MAP (P6); a sprite LOADED FROM DISK via stb + a binary PROGRESS
// SAVE (P5); and a COMPLETE small game loop — several levels, a goal, fall-respawn,
// win, and persistence (P7). Rendered on the engine's 2D GPU sprite path.

#include <okn/physics/impl/physics_factory.hpp>
#include <okn/physics/api/physics_world.hpp>
#include <okn/physics/dynamics/body.hpp>
#include <okn/physics/shapes/box.hpp>
#include <okn/math/algebra/quat.hpp>
#include <okn/input/action_map.hpp>

#include <okn/render/sprite2d/sprite_batch.hpp>
#include <okn/render/sprite2d/camera2d.hpp>
#include <okn/render/sprite2d/gpu_sprite_renderer.hpp>
#include <okn/render/sprite2d/image.hpp>

#include <okn/audio/backend/audio_engine.hpp>
#include <okn/audio/mixer/playback.hpp>
#include <okn/audio/decode/wav_decoder.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

using okn::render::sprite2d::Camera2D;
using okn::render::sprite2d::GpuSpriteRenderer;
using okn::render::sprite2d::Image;
using okn::render::sprite2d::Rgba8;
using okn::render::sprite2d::Sprite;
using okn::render::sprite2d::SpriteBatch;
using okn::render::sprite2d::rgba;
using okn::math::Vec2;
using okn::math::Vec3;
using okn::physics::IPhysicsWorld;
using okn::physics::RigidBody;
using okn::physics::BodyType;

namespace {

constexpr float kGravity = -22.0f;
constexpr float kMoveSpeed = 6.5f;
constexpr float kJumpSpeed = 11.5f;
constexpr float kPlayerHX = 0.4f;
constexpr float kPlayerHY = 0.5f;
constexpr unsigned kPlayerTex = 1;
const char* kSavePath = "platformer_save.dat";
const char* kSpritePath = "platformer_player.png";

// ── P6: input action-map (engine module okn-input; was a per-game struct) ────────
enum class Action { Left, Right, Jump, Count };
using InputMap = okn::input::ActionMap<Action>;

// ── Level data ──────────────────────────────────────────────────────────────────
struct Plat { float cx, cy, hx, hy; };
struct Level { std::vector<Plat> plats; Vec2 spawn; Vec2 goal; };

struct Game {
    std::unique_ptr<IPhysicsWorld> phys;
    std::vector<std::unique_ptr<okn::physics::Box>> shapes;
    std::vector<Plat> plats;
    okn::math::u32 player = 0;
    float vy = 0.0f;       // player vertical velocity (we integrate gravity; the controller is kinematic)
    Vec2 spawn{0, 0};
    Vec2 goal{0, 0};
    int level = 0;
    int best = 0;          // highest level index completed (persisted)
    bool won = false;
    bool facing_right = true;
    InputMap input;
    bool autodemo = false; // env OKN_PLAT_AUTODEMO: auto-hop right + trace (verification)
    float trace_t = 0.0f;
};

Game g;
std::vector<Level> g_levels;
std::unique_ptr<GpuSpriteRenderer> g_renderer;
okn::audio::AudioEngine g_audio;
std::unique_ptr<okn::audio::AudioPlayback> g_pb;
okn::audio::AudioBuffer g_jump_sfx{}, g_win_sfx{};

// ── audio (procedural, same pattern as the other games) ──────────────────────────
std::vector<okn::audio::u8> make_beep(float freq, float dur, float decay) {
    using okn::audio::u8;
    const unsigned rate = 22050;
    const unsigned frames = static_cast<unsigned>(static_cast<float>(rate) * dur);
    const unsigned bytes = frames * 2;
    std::vector<u8> w;
    auto u16 = [&](unsigned v) { w.push_back(u8(v & 0xFF)); w.push_back(u8((v >> 8) & 0xFF)); };
    auto u32 = [&](unsigned v) { for (int i = 0; i < 4; ++i) { w.push_back(u8((v >> (8 * i)) & 0xFF)); } };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) { w.push_back(u8(t[i])); } };
    tag("RIFF"); u32(36 + bytes); tag("WAVE"); tag("fmt "); u32(16); u16(1); u16(1);
    u32(rate); u32(rate * 2); u16(2); u16(16); tag("data"); u32(bytes);
    for (unsigned i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        const float env = std::pow(1.0f - static_cast<float>(i) / static_cast<float>(frames), decay);
        const int s = static_cast<int>(std::sin(t * freq * 6.2831853f) * 8000.0f * env);
        u16(static_cast<unsigned>(static_cast<short>(s)) & 0xFFFF);
    }
    return w;
}
okn::audio::AudioBuffer decode(float f, float d, float dec) {
    okn::audio::WavDecoder wd; const auto w = make_beep(f, d, dec);
    return wd.decode(w.data(), w.size());
}
void play(const okn::audio::AudioBuffer& b, float v) { if (g_pb && b.data) { g_pb->play(b, v); } }

// ── P5: a sprite that lives ON DISK ──────────────────────────────────────────────
// Generate a little character BMP if it is missing, then LOAD it back through stb
// (the same path a real PNG asset would take) and upload it to the GPU.
void ensure_player_sprite_on_disk() {
    std::ifstream probe(kSpritePath, std::ios::binary);
    if (probe.good()) { return; }
    const int n = 32;
    Image img(n, n, rgba(0, 0, 0, 0));
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float dx = (x - 15.5f) / 14.0f, dy = (y - 15.5f) / 14.0f;
            if (dx * dx + dy * dy <= 1.0f) { img.set(x, y, rgba(90, 170, 240)); }   // round body
        }
    }
    for (int y = 9; y < 14; ++y) {                                                    // eyes
        for (int x = 9; x < 13; ++x) { img.set(x, y, rgba(20, 20, 30)); }
        for (int x = 19; x < 23; ++x) { img.set(x, y, rgba(20, 20, 30)); }
    }
    for (int x = 11; x < 21; ++x) { img.set(x, 21, rgba(20, 20, 30)); }              // smile
    img.set(10, 20, rgba(20, 20, 30)); img.set(21, 20, rgba(20, 20, 30));
    // Write a real RGBA PNG (alpha preserved, unlike 24-bit BMP). Rgba8 packs as
    // r,g,b,a bytes, so pixels().data() is a valid tightly-packed RGBA8 buffer.
    stbi_write_png(kSpritePath, n, n, 4, img.pixels().data(), n * 4);
}

bool load_player_sprite_from_disk() {
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(kSpritePath, &w, &h, &ch, 4);
    if (data == nullptr) { return false; }
    Image img(w, h, rgba(0, 0, 0, 0));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = data + (static_cast<size_t>(y) * w + x) * 4;
            img.set(x, y, rgba(p[0], p[1], p[2], p[3]));
        }
    }
    stbi_image_free(data);
    g_renderer->upload_texture(kPlayerTex, img);
    return true;
}

// ── progress persistence ──────────────────────────────────────────────────────────
void save_progress() {
    std::ofstream f(kSavePath, std::ios::binary);
    if (!f) { return; }
    const std::uint32_t magic = 0x314D4C50u;  // 'P','L','M','1'
    const std::uint32_t best = static_cast<std::uint32_t>(g.best);
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&best), 4);
}
void load_progress() {
    std::ifstream f(kSavePath, std::ios::binary);
    if (!f) { return; }
    std::uint32_t magic = 0, best = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&best), 4);
    if (magic == 0x314D4C50u) { g.best = static_cast<int>(best); }
}

// ── level building ────────────────────────────────────────────────────────────────
okn::math::u32 add_static_box(float cx, float cy, float hx, float hy) {
    auto shape = std::make_unique<okn::physics::Box>(Vec3{hx, hy, 1.0f});
    RigidBody d;
    d.type = BodyType::kStatic;
    d.position = {cx, cy, 0.0f};
    d.inv_mass = 0.0f;
    d.material.friction = 0.8f;
    d.set_shape(shape.get());
    const okn::math::u32 id = g.phys->create_body(d);
    g.shapes.push_back(std::move(shape));
    return id;
}

void build_levels() {
    g_levels.clear();
    // L0 — gentle intro
    g_levels.push_back({{{0, 0, 6, 0.5f}, {9, 1.5f, 1.5f, 0.5f}, {14, 3.0f, 2.0f, 0.5f}},
                        {-3.0f, 1.5f}, {14.0f, 4.0f}});
    // L1 — gaps + a step up
    g_levels.push_back({{{-2, 0, 3, 0.5f}, {4, 0.5f, 1.5f, 0.5f}, {9, 2.0f, 1.5f, 0.5f},
                         {13, 3.5f, 1.5f, 0.5f}, {17, 5.0f, 2.0f, 0.5f}},
                        {-3.5f, 1.5f}, {17.0f, 6.0f}});
    // L2 — a climb to the top
    g_levels.push_back({{{-3, 0, 2.5f, 0.5f}, {2, 1.5f, 1.2f, 0.5f}, {6, 3.0f, 1.2f, 0.5f},
                         {2, 4.5f, 1.2f, 0.5f}, {6, 6.0f, 1.2f, 0.5f}, {10, 7.5f, 2.0f, 0.5f}},
                        {-4.0f, 1.5f}, {10.0f, 8.6f}});
}


void load_level(int idx) {
    g.phys = okn::physics::make_jolt_physics_world();
    g.phys->set_gravity({0.0f, kGravity, 0.0f});
    g.shapes.clear();
    g.won = false;
    g.level = std::clamp(idx, 0, static_cast<int>(g_levels.size()) - 1);

    const Level& lv = g_levels[g.level];
    g.plats = lv.plats;
    g.spawn = lv.spawn;
    g.goal = lv.goal;
    for (const auto& p : lv.plats) { add_static_box(p.cx, p.cy, p.hx, p.hy); }

    // The player IS the engine character controller — a kinematic capsule. radius =
    // player half-width; cylinder half-height makes the capsule total half-height the
    // player half-height, so its center (character_position) rests where the old box
    // center did. plane_2d locks it to Z=0; step_height auto-climbs the platform lips.
    okn::physics::CharacterDesc cd;
    cd.position = {lv.spawn.x, lv.spawn.y, 0.0f};
    cd.radius = kPlayerHX;
    cd.half_height = kPlayerHY - kPlayerHX;   // total half-height = kPlayerHY
    cd.max_slope = 0.873f;                     // ~50 deg
    cd.step_height = 0.3f;
    cd.plane_2d = true;
    cd.shape = okn::physics::CharacterShape::Box;   // flat bottom: stable on ledges
    g.player = g.phys->create_character(cd);
    g.vy = 0.0f;
}

bool grounded() {
    return g.phys->character_is_grounded(g.player);
}

void update(float dt) {
    const bool gnd = grounded();
    float vx = 0.0f;
    const bool want_left = g.input.held(Action::Left);
    const bool want_right = g.input.held(Action::Right) || g.autodemo;
    if (want_left) { vx -= kMoveSpeed; g.facing_right = false; }
    if (want_right) { vx += kMoveSpeed; g.facing_right = true; }

    // We integrate gravity ourselves (the controller is kinematic): rest the fall when
    // grounded, jump off the ground, then apply gravity. The controller collide-and-
    // slides this velocity, auto-steps platform lips, and keeps us out of the geometry.
    if (gnd && g.vy <= 0.0f) { g.vy = 0.0f; }
    const bool want_jump = g.input.just(Action::Jump) || (g.autodemo && gnd && g.vy <= 0.1f);
    if (want_jump && gnd) { g.vy = kJumpSpeed; play(g_jump_sfx, 0.4f); }
    g.vy += kGravity * dt;
    g.phys->character_move(g.player, {vx, g.vy, 0.0f}, dt);

    const Vec3 pos = g.phys->character_position(g.player);

    // Win on a flagpole touch: horizontally at the goal and at/above the pole base
    // (forgiving for a jumping player).
    const float gdx = std::fabs(pos.x - g.goal.x);
    if (!g.won && gdx < 1.0f && pos.y > g.goal.y - 1.8f) {
        g.won = true;
        play(g_win_sfx, 0.5f);
        if (g.level >= g.best) { g.best = g.level + 1; save_progress(); }
        const bool last = g.level + 1 >= static_cast<int>(g_levels.size());
        if (!last) { load_level(g.level + 1); }
    }

    // Fall off -> respawn at the level spawn.
    if (pos.y < -4.0f) {
        g.phys->character_set_position(g.player, {g.spawn.x, g.spawn.y, 0.0f});
        g.vy = 0.0f;
    }

    // Verification trace (auto-demo only): record the player's trajectory so the
    // controller/win/save chain can be checked headlessly of window focus.
    if (g.autodemo) {
        g.trace_t += dt;
        if (g.trace_t >= 0.4f) {
            g.trace_t = 0.0f;
            std::ofstream f("platformer_trace.txt", std::ios::app);
            if (f) {
                f << "lvl=" << (g.level + 1) << " x=" << pos.x
                  << " y=" << pos.y << " grounded=" << (gnd ? 1 : 0)
                  << " best=" << g.best << "\n";
            }
        }
    }
}

// ── render ─────────────────────────────────────────────────────────────────────
void quad(SpriteBatch& b, Vec2 c, float w, float h, Rgba8 col, float rot = 0.0f, unsigned tex = 0,
          bool flip = false) {
    Sprite s;
    s.position = c; s.size = {flip ? -w : w, h}; s.rotation = rot; s.color = col; s.texture_id = tex;
    b.add(s);
}

const char* kFont[10][5] = {
    {"###","# #","# #","# #","###"},{" # ","## "," # "," # ","###"},{"###","  #","###","#  ","###"},
    {"###","  #","###","  #","###"},{"# #","# #","###","  #","  #"},{"###","#  ","###","  #","###"},
    {"###","#  ","###","# #","###"},{"###","  #"," # "," # "," # "},{"###","# #","###","# #","###"},
    {"###","# #","###","  #","###"}};
void draw_digit(SpriteBatch& b, int dgt, float left, float top, float px) {
    if (dgt < 0 || dgt > 9) { return; }
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (kFont[dgt][r][c] == '#') {
                quad(b, {left + (c + 0.5f) * px, top - (r + 0.5f) * px}, px, px, rgba(255, 255, 255));
            }
        }
    }
}

void render() {
    SpriteBatch world;
    SpriteBatch hud;

    for (const auto& p : g.plats) {
        quad(world, {p.cx, p.cy}, p.hx * 2.0f, p.hy * 2.0f, rgba(96, 132, 96));
        quad(world, {p.cx, p.cy + p.hy - 0.08f}, p.hx * 2.0f, 0.16f, rgba(132, 196, 120));  // grass top
    }
    // goal flag
    quad(world, {g.goal.x, g.goal.y - 0.3f}, 0.12f, 1.4f, rgba(180, 150, 90));
    quad(world, {g.goal.x + 0.4f, g.goal.y + 0.25f}, 0.7f, 0.45f, rgba(230, 80, 80));

    {
        const Vec3 pp = g.phys->character_position(g.player);
        quad(world, {pp.x, pp.y}, kPlayerHX * 2.4f, kPlayerHY * 2.2f,
             rgba(255, 255, 255), 0.0f, kPlayerTex, !g.facing_right);
    }

    // HUD: level number (top-left) + a win banner.
    quad(hud, {0.9f, 11.2f}, 1.0f, 0.55f, rgba(40, 40, 60, 200));
    draw_digit(hud, g.level + 1, 0.55f, 11.45f, 0.22f);
    const bool last_done = g.won && g.level + 1 >= static_cast<int>(g_levels.size());
    if (last_done) {
        for (int i = 0; i < 3; ++i) { quad(hud, {8.0f, 7.0f - i * 0.6f}, 6.0f, 0.4f, rgba(250, 220, 90)); }
    }

    const float w = sapp_widthf(), h = sapp_heightf();
    const float vh = 12.0f, vw = vh * (w / h);

    Camera2D world_cam;       // follows the player
    float cx = g.spawn.x, cy = 4.0f;
    { const Vec3 pp = g.phys->character_position(g.player); cx = pp.x; cy = std::max(3.5f, pp.y); }
    world_cam.center = {cx, cy};
    world_cam.viewport_h = vh;
    world_cam.viewport_w = vw;

    Camera2D hud_cam;         // fixed screen space
    hud_cam.center = {vw * 0.5f, 6.0f};
    hud_cam.viewport_h = vh;
    hud_cam.viewport_w = vw;

    sg_pass_action pa{};
    pa.colors[0].load_action = SG_LOADACTION_CLEAR;
    pa.colors[0].clear_value = {0.36f, 0.62f, 0.80f, 1.0f};
    sg_begin_default_pass(&pa, sapp_width(), sapp_height());
    g_renderer->begin_frame();
    g_renderer->add(world_cam, world.build());
    g_renderer->add(hud_cam, hud.build());
    g_renderer->end_frame();
    sg_end_pass();
    sg_commit();
}

// ── sokol callbacks ───────────────────────────────────────────────────────────────
void on_init() {
    sg_desc d{};
    d.context = sapp_sgcontext();
    d.logger.func = slog_func;
    sg_setup(&d);

    g_renderer = std::make_unique<GpuSpriteRenderer>();
    g_renderer->init();
    ensure_player_sprite_on_disk();
    load_player_sprite_from_disk();

    if (g_audio.initialize()) {
        g_pb = std::make_unique<okn::audio::AudioPlayback>(g_audio);
        g_jump_sfx = decode(680.0f, 0.10f, 1.4f);
        g_win_sfx = decode(900.0f, 0.30f, 1.0f);
    }

    g.input.bind(Action::Left, SAPP_KEYCODE_A, SAPP_KEYCODE_LEFT);
    g.input.bind(Action::Right, SAPP_KEYCODE_D, SAPP_KEYCODE_RIGHT);
    g.input.bind(Action::Jump, SAPP_KEYCODE_SPACE, SAPP_KEYCODE_W);

    g.autodemo = (std::getenv("OKN_PLAT_AUTODEMO") != nullptr);

    load_progress();
    build_levels();
    load_level(0);
}

void on_event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
        if (ev->key_code == SAPP_KEYCODE_ESCAPE) { sapp_request_quit(); return; }
        if (ev->key_code == SAPP_KEYCODE_R) { load_level(g.won && g.level + 1 >= (int)g_levels.size() ? 0 : g.level); return; }
        g.input.on_key(ev->key_code, true);
    } else if (ev->type == SAPP_EVENTTYPE_KEY_UP) {
        g.input.on_key(ev->key_code, false);
    }
}

void on_frame() {
    float dt = static_cast<float>(sapp_frame_duration());
    dt = std::clamp(dt, 0.0f, 1.0f / 30.0f);
    if (!(g.won && g.level + 1 >= static_cast<int>(g_levels.size()))) { update(dt); }
    render();
    g.input.end_frame();
}

void on_cleanup() {
    for (auto* b : {&g_jump_sfx, &g_win_sfx}) { if (b->data) { delete[] b->data; b->data = nullptr; } }
    g_pb.reset();
    if (g_audio.is_initialized()) { g_audio.shutdown(); }
    if (g_renderer) { g_renderer->shutdown(); g_renderer.reset(); }
    g.phys.reset();
    sg_shutdown();
}

}  // namespace

sapp_desc sokol_main(int /*argc*/, char* /*argv*/[]) {
    sapp_desc d{};
    d.init_cb = on_init;
    d.frame_cb = on_frame;
    d.cleanup_cb = on_cleanup;
    d.event_cb = on_event;
    d.width = 960;
    d.height = 720;
    d.high_dpi = true;
    d.window_title = "Platformer — OmniKillerNexus (P5+P6+P7)";
    d.logger.func = slog_func;
    return d;
}
