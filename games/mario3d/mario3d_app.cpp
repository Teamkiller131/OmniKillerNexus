#define _CRT_SECURE_NO_WARNINGS  // std::getenv

// Super OmniKiller 3D — a 3D Mario-like, the engine's first true-3D game.
//
// Runs on the NEW okn-render mesh3d renderer (perspective camera + depth buffer +
// directional lighting on sokol_gfx) and uses Jolt in FULL 3D (no z-flattening):
// the player moves in the X-Z plane and jumps in Y, climbs box platforms, collects
// coins, stomps goombas (engine CONTACT-EVENTS API), and reaches the goal pillar.
// A 3D chase camera follows the player.

#include <okn/physics/impl/physics_factory.hpp>
#include <okn/physics/api/physics_world.hpp>
#include <okn/physics/dynamics/body.hpp>
#include <okn/physics/shapes/box.hpp>
#include <okn/math/algebra/quat.hpp>

#include <okn/render/mesh3d/mesh_renderer.hpp>
#include <okn/render/mesh3d/camera3d.hpp>
#include <okn/render/sprite2d/image.hpp>   // Rgba8 / rgba

#include <okn/audio/backend/audio_engine.hpp>
#include <okn/audio/mixer/playback.hpp>
#include <okn/audio/decode/wav_decoder.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

using okn::math::Vec3;
using okn::math::u32;
using okn::physics::IPhysicsWorld;
using okn::physics::RigidBody;
using okn::physics::BodyType;
using okn::render::mesh3d::Camera3D;
using okn::render::mesh3d::MeshRenderer;
using okn::render::sprite2d::Rgba8;
using okn::render::sprite2d::rgba;

namespace {

constexpr float kGravity = -24.0f;
constexpr float kMove = 7.0f;
constexpr float kJump = 11.5f;
constexpr float kStompBounce = 9.0f;
constexpr float kPlayerH = 0.6f;   // half height
constexpr float kPlayerR = 0.4f;   // half x/z

enum class Kind { Other, Player, Goomba };
enum class State { Playing, Win, GameOver };

enum class Action { Left, Right, Fwd, Back, Jump, CamL, CamR, Count };
struct InputMap {
    sapp_keycode keys[static_cast<int>(Action::Count)][2]{};
    bool down[static_cast<int>(Action::Count)]{};
    bool pressed[static_cast<int>(Action::Count)]{};
    void bind(Action a, sapp_keycode k0, sapp_keycode k1) {
        keys[static_cast<int>(a)][0] = k0; keys[static_cast<int>(a)][1] = k1;
    }
    void on_key(sapp_keycode k, bool d) {
        for (int a = 0; a < static_cast<int>(Action::Count); ++a) {
            if (keys[a][0] == k || keys[a][1] == k) {
                if (d && !down[a]) { pressed[a] = true; }
                down[a] = d;
            }
        }
    }
    void end_frame() { for (auto& p : pressed) { p = false; } }
    bool held(Action a) const { return down[static_cast<int>(a)]; }
    bool just(Action a) const { return pressed[static_cast<int>(a)]; }
};

struct Box { Vec3 center; Vec3 half; Rgba8 color; };
struct Coin { Vec3 pos; bool taken = false; };
struct Goomba { u32 body = 0; Vec3 axis; float lo = 0, hi = 0; int dir = 1; bool dead = false; };

struct Game {
    std::unique_ptr<IPhysicsWorld> phys;
    std::vector<std::unique_ptr<okn::physics::Box>> shapes;
    std::vector<Box> blocks;            // static visuals (ground + platforms)
    std::vector<Coin> coins;
    std::vector<Goomba> goombas;
    std::unordered_map<u32, Kind> kind;
    u32 player = 0;
    Vec3 spawn{0.0f, 1.6f, 6.0f};
    Vec3 goal{0.0f, 7.0f, -20.0f};
    int score = 0, coins_got = 0, lives = 3;
    float iframes = 0.0f;
    State state = State::Playing;
    InputMap input;
    bool autodemo = false;
    float trace_t = 0.0f;
    Vec3 cam_pos{0, 10, 18};
    float cam_yaw = 0.0f;     // orbit azimuth around Y (0 = behind the player, +Z)
    float cam_pitch = 0.5f;   // orbit elevation
    float cam_dist = 13.0f;   // orbit distance (wheel zoom)
    bool cam_drag = false;    // mouse-drag orbiting
    float time = 0.0f;        // for coin spin/bob
    float player_yaw = 0.0f;  // model faces the move direction
};

Game g;
std::unique_ptr<MeshRenderer> g_mesh;
okn::audio::AudioEngine g_audio;
std::unique_ptr<okn::audio::AudioPlayback> g_pb;
okn::audio::AudioBuffer g_jump{}, g_coin{}, g_stomp{}, g_hurt{}, g_win{};

// ── audio ─────────────────────────────────────────────────────────────────────
std::vector<okn::audio::u8> beep(float freq, float dur, float decay, bool rising = false) {
    using okn::audio::u8;
    const unsigned rate = 22050, frames = static_cast<unsigned>(rate * dur), bytes = frames * 2;
    std::vector<u8> w;
    auto u16 = [&](unsigned v) { w.push_back(u8(v & 0xFF)); w.push_back(u8((v >> 8) & 0xFF)); };
    auto u32f = [&](unsigned v) { for (int i = 0; i < 4; ++i) { w.push_back(u8((v >> (8 * i)) & 0xFF)); } };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) { w.push_back(u8(t[i])); } };
    tag("RIFF"); u32f(36 + bytes); tag("WAVE"); tag("fmt "); u32f(16); u16(1); u16(1);
    u32f(rate); u32f(rate * 2); u16(2); u16(16); tag("data"); u32f(bytes);
    for (unsigned i = 0; i < frames; ++i) {
        const float fr = static_cast<float>(i) / frames, f = rising ? freq * (1.0f + fr) : freq;
        const float env = std::pow(1.0f - fr, decay);
        u16(static_cast<unsigned>(static_cast<short>(std::sin((static_cast<float>(i) / rate) * f * 6.2831853f) * 8000.0f * env)) & 0xFFFF);
    }
    return w;
}
okn::audio::AudioBuffer dec(float f, float d, float k, bool r = false) {
    okn::audio::WavDecoder wd; const auto w = beep(f, d, k, r); return wd.decode(w.data(), w.size());
}
void play(const okn::audio::AudioBuffer& b, float v) { if (g_pb && b.data) { g_pb->play(b, v); } }

// ── world ───────────────────────────────────────────────────────────────────────
u32 add_box(Vec3 c, Vec3 half, bool dynamic, Kind k) {
    auto shape = std::make_unique<okn::physics::Box>(half);
    RigidBody d;
    d.type = dynamic ? BodyType::kDynamic : BodyType::kStatic;
    d.position = c; d.mass = 1.0f;
    if (!dynamic) { d.inv_mass = 0.0f; }
    d.material.friction = dynamic ? 0.0f : 0.7f;
    d.ccd_enabled = dynamic;
    d.set_shape(shape.get());
    const u32 id = g.phys->create_body(d);
    g.shapes.push_back(std::move(shape));
    g.kind[id] = k;
    return id;
}
void add_static(Vec3 c, Vec3 half, Rgba8 color) {
    add_box(c, half, false, Kind::Other);
    g.blocks.push_back({c, half, color});
}

void build_world() {
    g.phys = okn::physics::make_jolt_physics_world();
    g.phys->set_gravity({0.0f, kGravity, 0.0f});
    g.shapes.clear(); g.blocks.clear(); g.coins.clear(); g.goombas.clear(); g.kind.clear();
    g.state = State::Playing; g.iframes = 0.0f;

    const Rgba8 grass = rgba(96, 170, 84), dirt = rgba(150, 110, 70), brick = rgba(186, 120, 76);

    add_static({0, -1, -8}, {16, 1, 18}, grass);                  // big ground (y top = 0)

    // A climbing path — every jump is <= ~5 units in X-Z (reach is ~6.7) and <= ~1.2
    // up; the final hop to the goal is now 4 units (was a 12-unit, impossible gap).
    add_static({0.0f, 0.75f, 1.0f}, {2.2f, 0.4f, 1.8f}, dirt);     // p1
    add_static({1.5f, 1.75f, -3.0f}, {2.0f, 0.4f, 1.8f}, brick);   // p2
    add_static({-1.5f, 2.9f, -7.0f}, {2.0f, 0.4f, 1.8f}, dirt);    // p3
    add_static({1.5f, 4.0f, -11.0f}, {2.0f, 0.4f, 1.8f}, brick);   // p4
    add_static({0.0f, 5.1f, -15.0f}, {2.2f, 0.4f, 1.8f}, dirt);    // p5
    add_static({0.0f, 6.0f, -20.0f}, {3.5f, 0.4f, 2.0f}, brick);   // goal platform (clean gap from p5)

    // Coins above each platform + a couple on the ground.
    const Vec3 cpos[] = {{0.0f, 1.7f, 1.0f}, {1.5f, 2.7f, -3.0f}, {-1.5f, 3.9f, -7.0f},
                         {1.5f, 5.0f, -11.0f}, {0.0f, 6.1f, -15.0f}, {0.0f, 7.3f, -20.0f},
                         {3.5f, 1.3f, 3.0f}, {-3.5f, 1.3f, 0.0f}};
    for (const Vec3 p : cpos) { g.coins.push_back({p, false}); }

    // Goombas patrolling on the ground (along X) and on a platform.
    auto add_goomba = [&](Vec3 c, Vec3 axis, float lo, float hi) {
        Goomba gb; gb.body = add_box(c, {0.4f, 0.4f, 0.4f}, true, Kind::Goomba);
        gb.axis = axis; gb.lo = lo; gb.hi = hi; gb.dir = 1; g.goombas.push_back(gb);
    };
    add_goomba({-4, 0.6f, -2}, {1, 0, 0}, -8, 6);
    add_goomba({4, 0.6f, -8}, {1, 0, 0}, -2, 9);
    add_goomba({1.5f, 4.5f, -11}, {1, 0, 0}, -0.4f, 3.4f);

    g.player = add_box(g.spawn, {kPlayerR, kPlayerH, kPlayerR}, true, Kind::Player);
}

void reset_game() { g.score = 0; g.coins_got = 0; g.lives = 3; build_world(); }

bool grounded() {
    auto* rb = g.phys->get_body(g.player);
    if (!rb) { return false; }
    const float y = rb->position.y - kPlayerH - 0.02f;
    const float o = kPlayerR * 0.7f;
    const Vec3 pts[5] = {{rb->position.x, y, rb->position.z}, {rb->position.x + o, y, rb->position.z},
                         {rb->position.x - o, y, rb->position.z}, {rb->position.x, y, rb->position.z + o},
                         {rb->position.x, y, rb->position.z - o}};
    for (const Vec3& p : pts) { if (g.phys->raycast(p, {0, -1, 0}, 0.2f).hit) { return true; } }
    return false;
}

void hurt() {
    if (g.iframes > 0.0f || g.autodemo) { return; }
    --g.lives; g.iframes = 1.5f; play(g_hurt, 0.5f);
    if (g.lives <= 0) { g.state = State::GameOver; }
}

void respawn() {
    g.phys->set_transform(g.player, g.spawn, okn::math::Quat{0, 0, 0, 1});
    g.phys->set_linear_velocity(g.player, {0, 0, 0});
    g.iframes = 1.0f;
}

void update(float dt) {
    if (g.state != State::Playing) { return; }
    auto* rb = g.phys->get_body(g.player);
    if (!rb) { return; }
    g.iframes = std::max(0.0f, g.iframes - dt);
    const bool gnd = grounded();

    // ── camera orbit via keyboard (mouse drag + wheel handled in on_event) ──
    if (g.input.held(Action::CamL)) { g.cam_yaw -= 1.8f * dt; }
    if (g.input.held(Action::CamR)) { g.cam_yaw += 1.8f * dt; }

    // ── player controller — CAMERA-RELATIVE move (X-Z), Y jump ──
    const float sy = std::sin(g.cam_yaw), cyaw = std::cos(g.cam_yaw);
    const Vec3 fwd{-sy, 0.0f, -cyaw};     // "into the screen" (away from the camera)
    const Vec3 rightv{cyaw, 0.0f, -sy};
    float fax = 0.0f, sax = 0.0f;
    if (g.input.held(Action::Fwd) || g.autodemo) { fax += 1.0f; }
    if (g.input.held(Action::Back)) { fax -= 1.0f; }
    if (g.input.held(Action::Right)) { sax += 1.0f; }
    if (g.input.held(Action::Left)) { sax -= 1.0f; }
    Vec3 mv = fwd * fax + rightv * sax;
    if (mv.length() > 0.001f) { mv = mv.normalized() * kMove; }
    if (mv.length() > 0.1f) {                       // turn the model toward the move dir
        const float target = std::atan2(mv.x, mv.z);
        float diff = target - g.player_yaw;
        while (diff > 3.14159f) { diff -= 6.28318f; }
        while (diff < -3.14159f) { diff += 6.28318f; }
        g.player_yaw += diff * std::min(1.0f, dt * 12.0f);
    }
    float vy = rb->linear_velocity.y;
    const bool jump = g.input.just(Action::Jump) || (g.autodemo && gnd && vy <= 0.1f);
    if (jump && gnd) { vy = kJump; play(g_jump, 0.35f); }
    g.phys->set_linear_velocity(g.player, {mv.x, vy, mv.z});

    // ── goombas patrol along their axis ──
    for (auto& gb : g.goombas) {
        if (gb.dead) { continue; }
        if (auto* b = g.phys->get_body(gb.body)) {
            const float along = gb.axis.x != 0 ? b->position.x : b->position.z;
            if (along <= gb.lo) { gb.dir = 1; }
            if (along >= gb.hi) { gb.dir = -1; }
            const Vec3 v = gb.axis * (gb.dir * 1.6f);
            g.phys->set_linear_velocity(gb.body, {v.x, b->linear_velocity.y, v.z});
            g.phys->set_angular_velocity(gb.body, {0, 0, 0});
            g.phys->set_transform(gb.body, b->position, okn::math::Quat{0, 0, 0, 1});
        }
    }

    g.phys->step(dt);
    rb = g.phys->get_body(g.player);
    g.phys->set_angular_velocity(g.player, {0, 0, 0});
    g.phys->set_transform(g.player, rb->position, okn::math::Quat{0, 0, 0, 1});

    // ── CONTACT EVENTS: stomp vs hurt ──
    for (const auto& c : g.phys->drain_contacts()) {
        const auto ka = g.kind.find(c.body_a), kb = g.kind.find(c.body_b);
        if (ka == g.kind.end() || kb == g.kind.end()) { continue; }
        u32 gbomb = 0;
        if (ka->second == Kind::Player && kb->second == Kind::Goomba) { gbomb = c.body_b; }
        else if (kb->second == Kind::Player && ka->second == Kind::Goomba) { gbomb = c.body_a; }
        else { continue; }
        auto* pb = g.phys->get_body(g.player);
        auto* eb = g.phys->get_body(gbomb);
        if (!pb || !eb) { continue; }
        if (g.autodemo || (pb->position.y > eb->position.y + 0.3f && pb->linear_velocity.y < 3.0f)) {
            for (auto& gb : g.goombas) {
                if (gb.body == gbomb && !gb.dead) {
                    gb.dead = true; g.kind.erase(gbomb); g.phys->destroy_body(gbomb);
                    g.score += 100; g.phys->set_linear_velocity(g.player, {pb->linear_velocity.x, kStompBounce, pb->linear_velocity.z});
                    play(g_stomp, 0.5f); break;
                }
            }
        } else { hurt(); }
    }

    // ── coins (3D overlap) ──
    rb = g.phys->get_body(g.player);
    for (auto& coin : g.coins) {
        if (coin.taken) { continue; }
        if ((coin.pos - rb->position).length() < 0.9f) { coin.taken = true; ++g.coins_got; g.score += 50; play(g_coin, 0.4f); }
    }

    // ── win / fall ──
    if ((g.goal - rb->position).length() < 2.8f) { g.state = State::Win; play(g_win, 0.6f); }   // flagpole touch
    if (rb->position.y < -6.0f) { --g.lives; if (g.lives <= 0) { g.state = State::GameOver; } else { respawn(); } }

    // ── orbit chase camera (yaw/pitch/dist are player-controllable) ──
    const float cp = std::cos(g.cam_pitch), sp = std::sin(g.cam_pitch);
    const Vec3 offset{g.cam_dist * cp * std::sin(g.cam_yaw),
                      g.cam_dist * sp,
                      g.cam_dist * cp * std::cos(g.cam_yaw)};
    const Vec3 want = rb->position + offset;
    g.cam_pos = g.cam_pos + (want - g.cam_pos) * std::min(1.0f, dt * 10.0f);   // smooth follow

    if (g.autodemo) {
        g.trace_t += dt;
        if (g.trace_t >= 0.5f) {
            g.trace_t = 0.0f;
            std::ofstream f("mario3d_trace.txt", std::ios::app);
            if (f) { f << "z=" << rb->position.z << " y=" << rb->position.y << " score=" << g.score
                       << " coins=" << g.coins_got << " lives=" << g.lives << " state=" << static_cast<int>(g.state) << "\n"; }
        }
    }
}

// ── voxel-style models, composed from lit boxes ──────────────────────────────────
Vec3 ry(const Vec3& o, float c, float s) { return {o.x * c + o.z * s, o.y, -o.x * s + o.z * c}; }
void part(const Vec3& base, float yaw, float c, float s, const Vec3& off, const Vec3& half, Rgba8 col) {
    g_mesh->draw_box(base + ry(off, c, s), half, col, yaw);
}
void draw_mario(const Vec3& center, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    const Rgba8 red = rgba(220, 50, 45), blue = rgba(50, 80, 200), skin = rgba(245, 200, 150),
                dark = rgba(28, 24, 24), brown = rgba(90, 55, 35);
    part(center, yaw, c, s, {0, -0.30f, 0}, {0.27f, 0.30f, 0.23f}, blue);            // overalls
    part(center, yaw, c, s, {-0.15f, -0.56f, 0.02f}, {0.10f, 0.10f, 0.13f}, brown);  // shoe L
    part(center, yaw, c, s, {0.15f, -0.56f, 0.02f}, {0.10f, 0.10f, 0.13f}, brown);   // shoe R
    part(center, yaw, c, s, {0, 0.10f, 0}, {0.29f, 0.22f, 0.25f}, red);              // shirt
    part(center, yaw, c, s, {-0.35f, 0.08f, 0}, {0.08f, 0.17f, 0.12f}, red);         // arm L
    part(center, yaw, c, s, {0.35f, 0.08f, 0}, {0.08f, 0.17f, 0.12f}, red);          // arm R
    part(center, yaw, c, s, {0, 0.45f, 0}, {0.23f, 0.20f, 0.21f}, skin);             // head
    part(center, yaw, c, s, {0, 0.66f, 0.02f}, {0.26f, 0.10f, 0.23f}, red);          // cap
    part(center, yaw, c, s, {0, 0.60f, 0.21f}, {0.20f, 0.05f, 0.10f}, red);          // brim
    part(center, yaw, c, s, {-0.10f, 0.45f, 0.20f}, {0.04f, 0.05f, 0.03f}, dark);    // eye L
    part(center, yaw, c, s, {0.10f, 0.45f, 0.20f}, {0.04f, 0.05f, 0.03f}, dark);     // eye R
}
void draw_goomba(const Vec3& center) {
    const Rgba8 body = rgba(142, 86, 46), top = rgba(112, 66, 34), band = rgba(232, 206, 166),
                dark = rgba(24, 20, 20), foot = rgba(58, 36, 22);
    g_mesh->draw_box(center + Vec3{0, 0.02f, 0}, {0.42f, 0.30f, 0.40f}, body);
    g_mesh->draw_box(center + Vec3{0, 0.30f, 0}, {0.32f, 0.12f, 0.30f}, top);
    g_mesh->draw_box(center + Vec3{0, -0.06f, 0.36f}, {0.30f, 0.15f, 0.05f}, band);
    g_mesh->draw_box(center + Vec3{-0.13f, 0.04f, 0.40f}, {0.05f, 0.09f, 0.03f}, dark);
    g_mesh->draw_box(center + Vec3{0.13f, 0.04f, 0.40f}, {0.05f, 0.09f, 0.03f}, dark);
    g_mesh->draw_box(center + Vec3{-0.22f, -0.34f, 0.08f}, {0.12f, 0.07f, 0.12f}, foot);
    g_mesh->draw_box(center + Vec3{0.22f, -0.34f, 0.08f}, {0.12f, 0.07f, 0.12f}, foot);
}
void draw_coin(const Vec3& pos, float spin) {
    g_mesh->draw_box(pos, {0.30f, 0.30f, 0.05f}, rgba(255, 205, 50), spin);   // spinning disc
    g_mesh->draw_box(pos, {0.15f, 0.15f, 0.07f}, rgba(210, 160, 25), spin);   // inset face
}

// ── render ─────────────────────────────────────────────────────────────────────
void render() {
    const float w = sapp_widthf(), h = sapp_heightf();
    auto* rb = g.phys->get_body(g.player);
    const Vec3 ppos = rb ? rb->position : g.spawn;

    Camera3D cam;
    cam.position = g.cam_pos;
    cam.target = ppos + Vec3{0.0f, 0.6f, 0.0f};
    cam.aspect = w / h;

    sg_pass_action pa{};
    pa.colors[0].load_action = SG_LOADACTION_CLEAR;
    pa.colors[0].clear_value = {0.45f, 0.68f, 0.96f, 1.0f};
    pa.depth.load_action = SG_LOADACTION_CLEAR;
    pa.depth.clear_value = 1.0f;
    sg_begin_default_pass(&pa, sapp_width(), sapp_height());

    g_mesh->begin(cam);
    for (const auto& b : g.blocks) { g_mesh->draw_box(b.center, b.half, b.color); }
    for (std::size_t i = 0; i < g.coins.size(); ++i) {
        if (g.coins[i].taken) { continue; }
        const float bob = std::sin(g.time * 2.5f + static_cast<float>(i)) * 0.12f;
        draw_coin(g.coins[i].pos + Vec3{0, bob, 0}, g.time * 3.0f);
    }
    for (const auto& gb : g.goombas) {
        if (gb.dead) { continue; }
        if (auto* b = g.phys->get_body(gb.body)) { draw_goomba(b->position); }
    }
    // goal: flagpole + flag
    g_mesh->draw_box(g.goal + Vec3{0, 1.5f, 0}, {0.10f, 2.2f, 0.10f}, rgba(235, 235, 235));
    g_mesh->draw_box(g.goal + Vec3{0.55f, 2.9f, 0}, {0.6f, 0.42f, 0.06f}, rgba(70, 205, 95));
    // player (Mario model; flicker on i-frames)
    if (rb) {
        const bool blink = g.iframes > 0.0f && (static_cast<int>(g.iframes * 14.0f) % 2 == 0);
        if (!blink) { draw_mario(rb->position, g.player_yaw); }
    }

    sg_end_pass();
    sg_commit();
}

// ── sokol callbacks ───────────────────────────────────────────────────────────────
void on_init() {
    sg_desc d{}; d.context = sapp_sgcontext(); d.logger.func = slog_func; sg_setup(&d);
    g_mesh = std::make_unique<MeshRenderer>();
    g_mesh->init();
    g_mesh->set_light({-0.5f, -1.0f, -0.3f});
    if (g_audio.initialize()) {
        g_pb = std::make_unique<okn::audio::AudioPlayback>(g_audio);
        g_jump = dec(680, 0.10f, 1.4f); g_coin = dec(1050, 0.10f, 1.2f);
        g_stomp = dec(240, 0.12f, 1.0f); g_hurt = dec(180, 0.30f, 0.8f); g_win = dec(700, 0.45f, 0.7f, true);
    }
    g.input.bind(Action::Left, SAPP_KEYCODE_A, SAPP_KEYCODE_LEFT);
    g.input.bind(Action::Right, SAPP_KEYCODE_D, SAPP_KEYCODE_RIGHT);
    g.input.bind(Action::Fwd, SAPP_KEYCODE_W, SAPP_KEYCODE_UP);
    g.input.bind(Action::Back, SAPP_KEYCODE_S, SAPP_KEYCODE_DOWN);
    g.input.bind(Action::Jump, SAPP_KEYCODE_SPACE, SAPP_KEYCODE_SPACE);
    g.input.bind(Action::CamL, SAPP_KEYCODE_Q, SAPP_KEYCODE_Q);
    g.input.bind(Action::CamR, SAPP_KEYCODE_E, SAPP_KEYCODE_E);
    g.autodemo = (std::getenv("OKN_MARIO3D_AUTODEMO") != nullptr);
    if (const char* v = std::getenv("OKN_MARIO3D_CAMVIEW")) {   // deterministic view for verification
        if (std::strcmp(v, "top") == 0) { g.cam_pitch = 1.40f; g.cam_dist = 18.0f; }
        else if (std::strcmp(v, "side") == 0) { g.cam_yaw = 1.5708f; g.cam_pitch = 0.45f; }
        else if (std::strcmp(v, "front") == 0) { g.cam_yaw = 3.14159f; g.cam_pitch = 0.45f; }
    }
    g.cam_pos = g.spawn + Vec3{0, 7, 12};
    reset_game();
}

void on_event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
        switch (ev->key_code) {
            case SAPP_KEYCODE_ESCAPE: sapp_request_quit(); return;
            case SAPP_KEYCODE_R: reset_game(); return;
            case SAPP_KEYCODE_1: g.cam_yaw = 0.0f; g.cam_pitch = 0.50f; g.cam_dist = 13.0f; return;       // chase
            case SAPP_KEYCODE_2: g.cam_pitch = 1.40f; g.cam_dist = 17.0f; return;                          // top-down
            case SAPP_KEYCODE_3: g.cam_yaw = 3.14159f; g.cam_pitch = 0.45f; g.cam_dist = 13.0f; return;    // front
            case SAPP_KEYCODE_4: g.cam_yaw = 1.5708f; g.cam_pitch = 0.50f; g.cam_dist = 13.0f; return;     // side
            default: break;
        }
        g.input.on_key(ev->key_code, true);
    } else if (ev->type == SAPP_EVENTTYPE_KEY_UP) {
        g.input.on_key(ev->key_code, false);
    } else if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN) {
        g.cam_drag = true;
    } else if (ev->type == SAPP_EVENTTYPE_MOUSE_UP) {
        g.cam_drag = false;
    } else if (ev->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        if (g.cam_drag) {                                   // drag to orbit
            g.cam_yaw -= ev->mouse_dx * 0.006f;
            g.cam_pitch = std::clamp(g.cam_pitch + ev->mouse_dy * 0.006f, 0.12f, 1.45f);
        }
    } else if (ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL) {   // wheel to zoom
        g.cam_dist = std::clamp(g.cam_dist - ev->scroll_y * 1.2f, 4.0f, 30.0f);
    }
}

void on_frame() {
    float dt = static_cast<float>(sapp_frame_duration());
    dt = std::clamp(dt, 0.0f, 1.0f / 30.0f);
    g.time += dt;   // coin spin/bob advances regardless of game state
    update(dt);
    render();
    g.input.end_frame();
}

void on_cleanup() {
    for (auto* b : {&g_jump, &g_coin, &g_stomp, &g_hurt, &g_win}) { if (b->data) { delete[] b->data; b->data = nullptr; } }
    g_pb.reset();
    if (g_audio.is_initialized()) { g_audio.shutdown(); }
    if (g_mesh) { g_mesh->shutdown(); g_mesh.reset(); }
    g.phys.reset();
    sg_shutdown();
}

}  // namespace

sapp_desc sokol_main(int /*argc*/, char* /*argv*/[]) {
    sapp_desc d{};
    d.init_cb = on_init; d.frame_cb = on_frame; d.cleanup_cb = on_cleanup; d.event_cb = on_event;
    d.width = 1024; d.height = 640; d.high_dpi = true;
    d.window_title = "Super OmniKiller 3D — a 3D Mario-like (mesh3d + Jolt 3D)";
    d.logger.func = slog_func;
    return d;
}
