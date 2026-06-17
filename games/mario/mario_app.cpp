#define _CRT_SECURE_NO_WARNINGS  // std::getenv

// Mario-like side-scroller — a broad framework test.
//
// Exercises a lot of the engine at once: a Jolt CHARACTER CONTROLLER, walking
// ENEMIES (goombas), COIN pickups, a SIDE-SCROLLING camera, lives/score, audio,
// multi-texture disk sprites — and, crucially, the engine's CONTACT-EVENTS API
// (IPhysicsWorld::drain_contacts) to decide stomp-kill vs. take-damage on a
// player↔enemy collision. Built on the 2D GPU sprite path like the other games.

#include <okn/physics/impl/physics_factory.hpp>
#include <okn/physics/api/physics_world.hpp>
#include <okn/physics/dynamics/body.hpp>
#include <okn/physics/shapes/box.hpp>
#include <okn/math/algebra/quat.hpp>

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
#include <cstdlib>
#include <fstream>
#include <memory>
#include <unordered_map>
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
using okn::math::u32;
using okn::physics::IPhysicsWorld;
using okn::physics::RigidBody;
using okn::physics::BodyType;

namespace {

constexpr float kGravity = -26.0f;
constexpr float kMoveSpeed = 7.0f;
constexpr float kJumpSpeed = 12.5f;
constexpr float kStompBounce = 10.0f;
constexpr float kGoombaSpeed = 1.8f;
constexpr float kPlayerHX = 0.4f;
constexpr float kPlayerHY = 0.55f;
constexpr float kLevelWidth = 64.0f;
constexpr float kFlagX = 61.0f;
constexpr unsigned kTexMario = 1;
constexpr unsigned kTexCoin = 2;
constexpr unsigned kTexGoomba = 3;

enum class Kind { Other, Player, Goomba };
enum class State { Playing, Win, GameOver };

// ── input action-map ────────────────────────────────────────────────────────────
enum class Action { Left, Right, Jump, Count };
struct InputMap {
    sapp_keycode keys[static_cast<int>(Action::Count)][2]{};
    bool down[static_cast<int>(Action::Count)]{};
    bool pressed[static_cast<int>(Action::Count)]{};
    void bind(Action a, sapp_keycode k0, sapp_keycode k1) {
        keys[static_cast<int>(a)][0] = k0; keys[static_cast<int>(a)][1] = k1;
    }
    void on_key(sapp_keycode k, bool is_down) {
        for (int a = 0; a < static_cast<int>(Action::Count); ++a) {
            if (keys[a][0] == k || keys[a][1] == k) {
                if (is_down && !down[a]) { pressed[a] = true; }
                down[a] = is_down;
            }
        }
    }
    void end_frame() { for (auto& p : pressed) { p = false; } }
    bool held(Action a) const { return down[static_cast<int>(a)]; }
    bool just(Action a) const { return pressed[static_cast<int>(a)]; }
};

struct Plat { float cx, cy, hx, hy; Rgba8 color; };
struct Coin { Vec2 pos; bool taken = false; };
struct Goomba { u32 body = 0; float minx = 0, maxx = 0; int dir = -1; bool dead = false; };

struct Game {
    std::unique_ptr<IPhysicsWorld> phys;
    std::vector<std::unique_ptr<okn::physics::Box>> shapes;
    std::vector<Plat> plats;
    std::vector<Coin> coins;
    std::vector<Goomba> goombas;
    std::unordered_map<u32, Kind> kind;
    u32 player = 0;
    Vec2 spawn{2.0f, 3.0f};
    int score = 0;
    int coins_got = 0;
    int lives = 3;
    float iframes = 0.0f;
    float cam_x = 8.0f;
    bool facing_right = true;
    State state = State::Playing;
    InputMap input;
    bool autodemo = false;
    float trace_t = 0.0f;
};

Game g;
std::unique_ptr<GpuSpriteRenderer> g_renderer;
okn::audio::AudioEngine g_audio;
std::unique_ptr<okn::audio::AudioPlayback> g_pb;
okn::audio::AudioBuffer g_jump{}, g_coin{}, g_stomp{}, g_hurt{}, g_win{};

// ── audio ─────────────────────────────────────────────────────────────────────
std::vector<okn::audio::u8> make_beep(float freq, float dur, float decay, bool rising = false) {
    using okn::audio::u8;
    const unsigned rate = 22050;
    const unsigned frames = static_cast<unsigned>(static_cast<float>(rate) * dur);
    const unsigned bytes = frames * 2;
    std::vector<u8> w;
    auto u16 = [&](unsigned v) { w.push_back(u8(v & 0xFF)); w.push_back(u8((v >> 8) & 0xFF)); };
    auto u32f = [&](unsigned v) { for (int i = 0; i < 4; ++i) { w.push_back(u8((v >> (8 * i)) & 0xFF)); } };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) { w.push_back(u8(t[i])); } };
    tag("RIFF"); u32f(36 + bytes); tag("WAVE"); tag("fmt "); u32f(16); u16(1); u16(1);
    u32f(rate); u32f(rate * 2); u16(2); u16(16); tag("data"); u32f(bytes);
    for (unsigned i = 0; i < frames; ++i) {
        const float frac = static_cast<float>(i) / static_cast<float>(frames);
        const float f = rising ? freq * (1.0f + frac) : freq;
        const float env = std::pow(1.0f - frac, decay);
        const int s = static_cast<int>(std::sin((static_cast<float>(i) / rate) * f * 6.2831853f) * 8000.0f * env);
        u16(static_cast<unsigned>(static_cast<short>(s)) & 0xFFFF);
    }
    return w;
}
okn::audio::AudioBuffer decode(float f, float d, float dec, bool rising = false) {
    okn::audio::WavDecoder wd; const auto w = make_beep(f, d, dec, rising);
    return wd.decode(w.data(), w.size());
}
void play(const okn::audio::AudioBuffer& b, float v) { if (g_pb && b.data) { g_pb->play(b, v); } }

// ── disk sprites (generate PNGs, then load them back through stb) ─────────────────
void write_png(const char* path, const Image& img) {
    stbi_write_png(path, img.width(), img.height(), 4, img.pixels().data(), img.width() * 4);
}
Image make_mario() {
    Image im(16, 20, rgba(0, 0, 0, 0));
    auto fill = [&](int x0, int y0, int x1, int y1, Rgba8 c) {
        for (int y = y0; y <= y1; ++y) { for (int x = x0; x <= x1; ++x) { im.set(x, y, c); } } };
    const Rgba8 red = rgba(220, 60, 50), skin = rgba(245, 200, 150), brown = rgba(90, 50, 30), blue = rgba(60, 80, 200);
    fill(4, 0, 11, 3, red);                 // cap
    fill(3, 4, 12, 8, skin);                // face
    fill(3, 5, 5, 6, brown); fill(10, 5, 12, 6, brown);  // hair sides
    im.set(6, 6, rgba(20, 20, 30)); im.set(10, 6, rgba(20, 20, 30));  // eyes
    fill(5, 9, 10, 14, red);                // shirt
    fill(4, 12, 11, 18, blue);              // overalls
    fill(3, 18, 6, 19, brown); fill(9, 18, 12, 19, brown);  // shoes
    return im;
}
Image make_coin() {
    Image im(16, 16, rgba(0, 0, 0, 0));
    const float c = 7.5f, r = 7.0f;
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        const float d = std::sqrt((x - c) * (x - c) + (y - c) * (y - c));
        if (d <= r) { im.set(x, y, d > r - 1.5f ? rgba(200, 160, 30) : rgba(255, 215, 70)); }
    }
    for (int y = 5; y <= 10; ++y) { im.set(7, y, rgba(190, 150, 20)); im.set(8, y, rgba(190, 150, 20)); }
    return im;
}
Image make_goomba() {
    Image im(16, 16, rgba(0, 0, 0, 0));
    auto fill = [&](int x0, int y0, int x1, int y1, Rgba8 c) {
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) { im.set(x, y, c); } };
    const float c = 7.5f, r = 7.2f;
    for (int y = 0; y < 12; ++y) for (int x = 0; x < 16; ++x) {           // brown dome
        const float d = std::sqrt((x - c) * (x - c) + (y - 6) * (y - 6));
        if (d <= r && y <= 9) { im.set(x, y, rgba(140, 85, 45)); }
    }
    fill(3, 9, 12, 12, rgba(225, 200, 160));   // face band
    im.set(5, 8, rgba(20, 20, 20)); im.set(6, 8, rgba(20, 20, 20));      // eyes
    im.set(9, 8, rgba(20, 20, 20)); im.set(10, 8, rgba(20, 20, 20));
    fill(2, 13, 6, 15, rgba(60, 35, 20)); fill(9, 13, 13, 15, rgba(60, 35, 20));  // feet
    return im;
}
bool load_sprites() {
    write_png("mario_mario.png", make_mario());
    write_png("mario_coin.png", make_coin());
    write_png("mario_goomba.png", make_goomba());
    struct Pair { const char* path; unsigned tex; };
    for (const Pair p : {Pair{"mario_mario.png", kTexMario}, Pair{"mario_coin.png", kTexCoin},
                         Pair{"mario_goomba.png", kTexGoomba}}) {
        int w = 0, h = 0, ch = 0;
        stbi_set_flip_vertically_on_load(false);
        unsigned char* d = stbi_load(p.path, &w, &h, &ch, 4);
        if (!d) { return false; }
        Image im(w, h, rgba(0, 0, 0, 0));
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const unsigned char* px = d + (static_cast<size_t>(y) * w + x) * 4;
            im.set(x, y, rgba(px[0], px[1], px[2], px[3]));
        }
        stbi_image_free(d);
        g_renderer->upload_texture(p.tex, im);
    }
    return true;
}

// ── world build ───────────────────────────────────────────────────────────────
u32 add_box(Vec2 c, Vec2 half, bool dynamic, Kind k) {
    auto shape = std::make_unique<okn::physics::Box>(Vec3{half.x, half.y, 0.8f});
    RigidBody d;
    d.type = dynamic ? BodyType::kDynamic : BodyType::kStatic;
    d.position = {c.x, c.y, 0.0f};
    d.mass = 1.0f;
    if (!dynamic) { d.inv_mass = 0.0f; }
    d.material.friction = dynamic ? 0.0f : 0.6f;
    d.ccd_enabled = dynamic;
    d.set_shape(shape.get());
    const u32 id = g.phys->create_body(d);
    g.shapes.push_back(std::move(shape));
    g.kind[id] = k;
    return id;
}

void add_ground(float x0, float x1, float top, Rgba8 col) {
    const float cx = (x0 + x1) * 0.5f, hx = (x1 - x0) * 0.5f, hy = 1.5f;
    add_box({cx, top - hy}, {hx, hy}, false, Kind::Other);
    g.plats.push_back({cx, top - hy, hx, hy, col});
}
void add_block(float cx, float cy, float hx, float hy, Rgba8 col) {
    add_box({cx, cy}, {hx, hy}, false, Kind::Other);
    g.plats.push_back({cx, cy, hx, hy, col});
}

void build_world() {
    g.phys = okn::physics::make_jolt_physics_world();
    g.phys->set_gravity({0.0f, kGravity, 0.0f});
    g.shapes.clear(); g.plats.clear(); g.coins.clear(); g.goombas.clear(); g.kind.clear();
    g.state = State::Playing;
    g.iframes = 0.0f;

    const Rgba8 dirt = rgba(150, 100, 60), grass = rgba(110, 180, 90), pipe = rgba(40, 170, 90), brick = rgba(180, 110, 70);

    // Ground with two gaps to jump over.
    add_ground(-2, 16, 0.0f, dirt);
    add_ground(20, 38, 0.0f, dirt);
    add_ground(42, 66, 0.0f, dirt);
    (void)grass;

    // Pipes + floating bricks.
    add_block(13, 1.0f, 0.9f, 1.0f, pipe);
    add_block(27, 1.2f, 0.9f, 1.2f, pipe);
    add_block(9, 3.2f, 1.5f, 0.4f, brick);
    add_block(24, 3.6f, 1.5f, 0.4f, brick);
    add_block(34, 4.2f, 1.5f, 0.4f, brick);
    add_block(50, 3.0f, 1.8f, 0.4f, brick);

    // Coins (floating arcs + over gaps).
    const float coin_xs[] = {6, 9, 9.0f, 17.5f, 18.5f, 24, 24, 34, 44, 50, 50, 55};
    const float coin_ys[] = {2.0f, 4.2f, 5.0f, 2.5f, 2.5f, 4.6f, 5.4f, 5.2f, 2.0f, 4.0f, 4.8f, 2.0f};
    for (int i = 0; i < 12; ++i) { g.coins.push_back({{coin_xs[i], coin_ys[i]}, false}); }

    // Goombas patrolling each ground segment.
    auto add_goomba = [&](float x, float minx, float maxx) {
        Goomba gb; gb.body = add_box({x, 0.6f}, {0.4f, 0.4f}, true, Kind::Goomba);
        gb.minx = minx; gb.maxx = maxx; gb.dir = -1; g.goombas.push_back(gb);
    };
    add_goomba(11, 2, 15);
    add_goomba(30, 21, 37);
    add_goomba(46, 43, 52);
    add_goomba(56, 53, 60);

    // Player.
    g.player = add_box(g.spawn, {kPlayerHX, kPlayerHY}, true, Kind::Player);
}

void reset_game() {
    g.score = 0; g.coins_got = 0; g.lives = 3; g.cam_x = 8.0f;
    build_world();
}

bool grounded() {
    if (auto* rb = g.phys->get_body(g.player)) {
        const Vec3 o{rb->position.x, rb->position.y - kPlayerHY - 0.02f, 0.0f};
        return g.phys->raycast(o, {0.0f, -1.0f, 0.0f}, 0.16f).hit;
    }
    return false;
}

void hurt_player() {
    if (g.iframes > 0.0f) { return; }
    --g.lives;
    g.iframes = 1.5f;
    play(g_hurt, 0.5f);
    if (auto* rb = g.phys->get_body(g.player)) {
        g.phys->set_linear_velocity(g.player, {g.facing_right ? -5.0f : 5.0f, 7.0f, 0.0f});
    }
    if (g.lives <= 0) { g.state = State::GameOver; }
}

void update(float dt) {
    if (g.state != State::Playing) { return; }
    auto* rb = g.phys->get_body(g.player);
    if (rb == nullptr) { return; }
    g.iframes = std::max(0.0f, g.iframes - dt);

    // ── player controller ──
    const bool gnd = grounded();
    float vx = 0.0f;
    if (g.input.held(Action::Left)) { vx -= kMoveSpeed; g.facing_right = false; }
    if (g.input.held(Action::Right) || g.autodemo) { vx += kMoveSpeed; g.facing_right = true; }
    float vy = rb->linear_velocity.y;
    if ((g.input.just(Action::Jump) || (g.autodemo && gnd && vy <= 0.1f)) && gnd) {
        vy = kJumpSpeed; play(g_jump, 0.35f);
    }
    g.phys->set_linear_velocity(g.player, {vx, vy, 0.0f});

    // ── goomba AI: patrol between bounds ──
    for (auto& gb : g.goombas) {
        if (gb.dead) { continue; }
        if (auto* b = g.phys->get_body(gb.body)) {
            if (b->position.x <= gb.minx) { gb.dir = 1; }
            if (b->position.x >= gb.maxx) { gb.dir = -1; }
            g.phys->set_linear_velocity(gb.body, {gb.dir * kGoombaSpeed, b->linear_velocity.y, 0.0f});
            g.phys->set_angular_velocity(gb.body, {0, 0, 0});
            g.phys->set_transform(gb.body, b->position, okn::math::Quat{0, 0, 0, 1});
        }
    }

    g.phys->step(dt);

    // Keep the player upright.
    rb = g.phys->get_body(g.player);
    g.phys->set_angular_velocity(g.player, {0, 0, 0});
    g.phys->set_transform(g.player, rb->position, okn::math::Quat{0, 0, 0, 1});

    // ── CONTACT EVENTS: stomp vs. hurt on player↔goomba ──
    for (const auto& c : g.phys->drain_contacts()) {
        const auto ka = g.kind.find(c.body_a), kb = g.kind.find(c.body_b);
        if (ka == g.kind.end() || kb == g.kind.end()) { continue; }
        u32 goomba_body = 0;
        if (ka->second == Kind::Player && kb->second == Kind::Goomba) { goomba_body = c.body_b; }
        else if (kb->second == Kind::Player && ka->second == Kind::Goomba) { goomba_body = c.body_a; }
        else { continue; }

        auto* pb = g.phys->get_body(g.player);
        auto* eb = g.phys->get_body(goomba_body);
        if (!pb || !eb) { continue; }
        // Stomp if the player is clearly above the goomba and descending.
        if (pb->position.y > eb->position.y + 0.35f && pb->linear_velocity.y < 3.0f) {
            for (auto& gb : g.goombas) {
                if (gb.body == goomba_body && !gb.dead) {
                    gb.dead = true;
                    g.kind.erase(goomba_body);
                    g.phys->destroy_body(goomba_body);
                    g.score += 100;
                    g.phys->set_linear_velocity(g.player, {pb->linear_velocity.x, kStompBounce, 0.0f});
                    play(g_stomp, 0.5f);
                    break;
                }
            }
        } else {
            hurt_player();
        }
    }

    // ── coin pickup (AABB overlap) ──
    rb = g.phys->get_body(g.player);
    for (auto& coin : g.coins) {
        if (coin.taken) { continue; }
        if (std::fabs(rb->position.x - coin.pos.x) < kPlayerHX + 0.4f &&
            std::fabs(rb->position.y - coin.pos.y) < kPlayerHY + 0.4f) {
            coin.taken = true; ++g.coins_got; g.score += 50; play(g_coin, 0.4f);
        }
    }

    // ── win / fall ──
    if (rb->position.x >= kFlagX) { g.state = State::Win; play(g_win, 0.6f); }
    if (rb->position.y < -4.0f) {
        --g.lives;
        if (g.lives <= 0) { g.state = State::GameOver; }
        else {
            g.phys->set_transform(g.player, {g.spawn.x, g.spawn.y, 0.0f}, okn::math::Quat{0, 0, 0, 1});
            g.phys->set_linear_velocity(g.player, {0, 0, 0});
            g.iframes = 1.0f;
        }
    }

    // ── camera follows the player, clamped to the level ──
    const float w = sapp_widthf(), h = sapp_heightf();
    const float half_vw = 0.5f * (12.0f * (w / h));
    g.cam_x = std::clamp(rb->position.x, half_vw - 2.0f, kLevelWidth - half_vw);

    if (g.autodemo) {
        g.trace_t += dt;
        if (g.trace_t >= 0.5f) {
            g.trace_t = 0.0f;
            std::ofstream f("mario_trace.txt", std::ios::app);
            if (f) {
                f << "x=" << rb->position.x << " score=" << g.score << " coins=" << g.coins_got
                  << " lives=" << g.lives << " state=" << static_cast<int>(g.state) << "\n";
            }
        }
    }
}

// ── render ─────────────────────────────────────────────────────────────────────
void quad(SpriteBatch& b, Vec2 c, float w, float h, Rgba8 col, unsigned tex = 0, bool flip = false) {
    Sprite s; s.position = c; s.size = {flip ? -w : w, h}; s.color = col; s.texture_id = tex; b.add(s);
}
const char* kFont[10][5] = {
    {"###","# #","# #","# #","###"},{" # ","## "," # "," # ","###"},{"###","  #","###","#  ","###"},
    {"###","  #","###","  #","###"},{"# #","# #","###","  #","  #"},{"###","#  ","###","  #","###"},
    {"###","#  ","###","# #","###"},{"###","  #"," # "," # "," # "},{"###","# #","###","# #","###"},
    {"###","# #","###","  #","###"}};
void draw_num(SpriteBatch& b, int n, float left, float top, float px, Rgba8 col) {
    if (n < 0) { n = 0; }
    int ds[8], cnt = 0;
    do { ds[cnt++] = n % 10; n /= 10; } while (n > 0 && cnt < 8);
    for (int i = cnt - 1; i >= 0; --i) {
        for (int r = 0; r < 5; ++r) for (int c = 0; c < 3; ++c)
            if (kFont[ds[i]][r][c] == '#') quad(b, {left + (c + 0.5f) * px, top - (r + 0.5f) * px}, px, px, col);
        left += 4.0f * px;
    }
}

void render() {
    SpriteBatch world, hud;

    for (const auto& p : g.plats) { quad(world, {p.cx, p.cy}, p.hx * 2, p.hy * 2, p.color); }
    for (const auto& c : g.coins) { if (!c.taken) { quad(world, c.pos, 0.7f, 0.7f, rgba(255, 255, 255), kTexCoin); } }
    for (const auto& gb : g.goombas) {
        if (gb.dead) { continue; }
        if (auto* b = g.phys->get_body(gb.body)) { quad(world, {b->position.x, b->position.y}, 1.0f, 1.0f, rgba(255, 255, 255), kTexGoomba); }
    }
    // flag
    quad(world, {kFlagX, 3.0f}, 0.15f, 6.0f, rgba(220, 220, 220));
    quad(world, {kFlagX + 0.5f, 5.4f}, 0.9f, 0.6f, rgba(70, 200, 90));

    if (auto* rb = g.phys->get_body(g.player)) {
        const bool blink = g.iframes > 0.0f && (static_cast<int>(g.iframes * 12.0f) % 2 == 0);
        quad(world, {rb->position.x, rb->position.y}, kPlayerHX * 2.6f, kPlayerHY * 2.2f,
             rgba(255, 255, 255, blink ? 90 : 255), kTexMario, !g.facing_right);
    }

    const float w = sapp_widthf(), h = sapp_heightf();
    const float vh = 12.0f, vw = vh * (w / h);

    // HUD: coins (top-left) + score + lives (hearts).
    quad(hud, {0.7f, 11.3f}, 0.5f, 0.5f, rgba(255, 255, 255), kTexCoin);
    draw_num(hud, g.coins_got, 1.1f, 11.5f, 0.22f, rgba(255, 255, 255));
    draw_num(hud, g.score, vw * 0.5f - 1.0f, 11.5f, 0.24f, rgba(255, 245, 120));
    for (int i = 0; i < g.lives; ++i) { quad(hud, {vw - 0.6f - i * 0.7f, 11.3f}, 0.5f, 0.5f, rgba(230, 70, 70)); }

    if (g.state == State::Win) {
        for (int i = 0; i < 5; ++i) quad(hud, {vw * 0.5f, 7.0f - i * 0.55f}, 7.0f, 0.4f, rgba(120, 230, 120));
    } else if (g.state == State::GameOver) {
        for (int i = 0; i < 5; ++i) quad(hud, {vw * 0.5f, 7.0f - i * 0.55f}, 7.0f, 0.4f, rgba(220, 80, 80));
    }

    Camera2D wc; wc.center = {g.cam_x, 4.5f}; wc.viewport_h = vh; wc.viewport_w = vw;
    Camera2D hc; hc.center = {vw * 0.5f, 6.0f}; hc.viewport_h = vh; hc.viewport_w = vw;

    sg_pass_action pa{};
    pa.colors[0].load_action = SG_LOADACTION_CLEAR;
    pa.colors[0].clear_value = {0.42f, 0.66f, 0.95f, 1.0f};
    sg_begin_default_pass(&pa, sapp_width(), sapp_height());
    g_renderer->begin_frame();
    g_renderer->add(wc, world.build());
    g_renderer->add(hc, hud.build());
    g_renderer->end_frame();
    sg_end_pass();
    sg_commit();
}

// ── sokol callbacks ───────────────────────────────────────────────────────────────
void on_init() {
    sg_desc d{}; d.context = sapp_sgcontext(); d.logger.func = slog_func; sg_setup(&d);
    g_renderer = std::make_unique<GpuSpriteRenderer>();
    g_renderer->init();
    load_sprites();
    if (g_audio.initialize()) {
        g_pb = std::make_unique<okn::audio::AudioPlayback>(g_audio);
        g_jump = decode(680.0f, 0.10f, 1.4f);
        g_coin = decode(1050.0f, 0.10f, 1.2f);
        g_stomp = decode(240.0f, 0.12f, 1.0f);
        g_hurt = decode(180.0f, 0.30f, 0.8f);
        g_win = decode(700.0f, 0.45f, 0.7f, /*rising=*/true);
    }
    g.input.bind(Action::Left, SAPP_KEYCODE_A, SAPP_KEYCODE_LEFT);
    g.input.bind(Action::Right, SAPP_KEYCODE_D, SAPP_KEYCODE_RIGHT);
    g.input.bind(Action::Jump, SAPP_KEYCODE_SPACE, SAPP_KEYCODE_W);
    g.autodemo = (std::getenv("OKN_MARIO_AUTODEMO") != nullptr);
    reset_game();
}

void on_event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
        if (ev->key_code == SAPP_KEYCODE_ESCAPE) { sapp_request_quit(); return; }
        if (ev->key_code == SAPP_KEYCODE_R) { reset_game(); return; }
        g.input.on_key(ev->key_code, true);
    } else if (ev->type == SAPP_EVENTTYPE_KEY_UP) {
        g.input.on_key(ev->key_code, false);
    }
}

void on_frame() {
    float dt = static_cast<float>(sapp_frame_duration());
    dt = std::clamp(dt, 0.0f, 1.0f / 30.0f);
    update(dt);
    render();
    g.input.end_frame();
}

void on_cleanup() {
    for (auto* b : {&g_jump, &g_coin, &g_stomp, &g_hurt, &g_win}) { if (b->data) { delete[] b->data; b->data = nullptr; } }
    g_pb.reset();
    if (g_audio.is_initialized()) { g_audio.shutdown(); }
    if (g_renderer) { g_renderer->shutdown(); g_renderer.reset(); }
    g.phys.reset();
    sg_shutdown();
}

}  // namespace

sapp_desc sokol_main(int /*argc*/, char* /*argv*/[]) {
    sapp_desc d{};
    d.init_cb = on_init; d.frame_cb = on_frame; d.cleanup_cb = on_cleanup; d.event_cb = on_event;
    d.width = 1024; d.height = 600; d.high_dpi = true;
    d.window_title = "Super OmniKiller — a Mario-like (contact events + enemies + coins)";
    d.logger.func = slog_func;
    return d;
}
