// Knockdown — an Angry-Birds-style physics game on the OmniKillerNexus engine.
//
// Showcases the engine's PHYSICS depth: a slingshot ball (Jolt dynamic body) you
// drag-launch into a stacked tower of boxes (Jolt's stacking, which the engine's
// golden tests verify), with the NEW physics contact events (IPhysicsWorld::
// drain_contacts) driving impact sounds + scoring. Rendered via the 2D GPU sprite
// path (with a procedural circle texture for the ball), audio via okn-audio, window
// + mouse input via sokol_app.

#include <okn/physics/impl/physics_factory.hpp>
#include <okn/physics/api/physics_world.hpp>
#include <okn/physics/dynamics/body.hpp>
#include <okn/physics/shapes/box.hpp>
#include <okn/physics/shapes/sphere.hpp>
#include <okn/math/algebra/quat.hpp>

#include <okn/render/sprite2d/sprite_batch.hpp>
#include <okn/render/sprite2d/bitmap_text.hpp>
#include <okn/render/sprite2d/camera2d.hpp>
#include <okn/render/sprite2d/gpu_sprite_renderer.hpp>
#include <okn/render/sprite2d/image.hpp>

#include <okn/audio/backend/audio_engine.hpp>
#include <okn/audio/mixer/playback.hpp>
#include <okn/audio/decode/wav_decoder.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
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

constexpr float kWorldW = 16.0f;
constexpr float kWorldH = 12.0f;
constexpr Vec2  kLaunch{4.0f, 3.4f};
constexpr float kBallR = 0.42f;
constexpr float kPower = 4.2f;
constexpr float kMaxSpeed = 24.0f;
constexpr unsigned kBallTex = 1;

struct Box2 {
    okn::math::u32 body = 0;
    float hx = 0.5f, hy = 0.5f;
    Rgba8 color{200, 120, 80, 255};
    float spawn_x = 0.0f;
};

struct Game {
    std::unique_ptr<IPhysicsWorld> phys;
    std::vector<std::unique_ptr<okn::physics::Box>> box_shapes;
    std::unique_ptr<okn::physics::Sphere> ball_shape;
    std::vector<Box2> boxes;
    okn::math::u32 ground = 0;
    okn::math::u32 ball = 0;
    bool ball_active = false;
    bool aiming = false;
    Vec2 mouse_px{0, 0};
    Vec2 mouse_world{0, 0};
    bool release_request = false;
    int score = 0;
    int shots = 0;
    float thump_cd = 0.0f;
    float settle = 0.0f;   // ignore impact sounds while the fresh tower settles
};

Game g;
std::unique_ptr<GpuSpriteRenderer> g_renderer;
okn::audio::AudioEngine g_audio;
std::unique_ptr<okn::audio::AudioPlayback> g_pb;
okn::audio::AudioBuffer g_thump{}, g_launch{};

// ── Audio helpers ─────────────────────────────────────────────────────────────
std::vector<okn::audio::u8> make_beep(float freq, float dur_s, float decay) {
    using okn::audio::u8;
    const unsigned rate = 22050;
    const unsigned frames = static_cast<unsigned>(static_cast<float>(rate) * dur_s);
    const unsigned data_bytes = frames * 2;
    std::vector<u8> w;
    auto u16le = [&](unsigned v) { w.push_back(u8(v & 0xFF)); w.push_back(u8((v >> 8) & 0xFF)); };
    auto u32le = [&](unsigned v) { for (int i = 0; i < 4; ++i) { w.push_back(u8((v >> (8 * i)) & 0xFF)); } };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) { w.push_back(u8(t[i])); } };
    tag("RIFF"); u32le(36 + data_bytes); tag("WAVE");
    tag("fmt "); u32le(16); u16le(1); u16le(1); u32le(rate); u32le(rate * 2); u16le(2); u16le(16);
    tag("data"); u32le(data_bytes);
    for (unsigned i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        const float env = std::pow(1.0f - static_cast<float>(i) / static_cast<float>(frames), decay);
        const int s = static_cast<int>(std::sin(t * freq * 6.2831853f) * 8500.0f * env);
        u16le(static_cast<unsigned>(static_cast<short>(s)) & 0xFFFF);
    }
    return w;
}
okn::audio::AudioBuffer decode(float f, float d, float dec) {
    okn::audio::WavDecoder wd;
    const auto wav = make_beep(f, d, dec);
    return wd.decode(wav.data(), wav.size());
}
void play(const okn::audio::AudioBuffer& b, float vol) { if (g_pb && b.data) { g_pb->play(b, vol); } }

Image make_circle_tex() {
    const int n = 64;
    Image img(n, n, rgba(0, 0, 0, 0));
    const float c = (n - 1) * 0.5f, rad = c - 1.0f;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float dx = x - c, dy = y - c;
            const float dd = std::sqrt(dx * dx + dy * dy);
            if (dd <= rad) {
                const std::uint8_t a = (dd > rad - 1.5f) ? 200 : 255;
                img.set(x, y, rgba(255, 255, 255, a));
            }
        }
    }
    return img;
}

// ── World setup ─────────────────────────────────────────────────────────────
okn::math::u32 add_box(Vec2 center, float hx, float hy, bool dynamic) {
    auto shape = std::make_unique<okn::physics::Box>(Vec3{hx, hy, 0.6f});
    RigidBody d;
    d.type = dynamic ? BodyType::kDynamic : BodyType::kStatic;
    d.position = {center.x, center.y, 0.0f};
    if (!dynamic) { d.inv_mass = 0.0f; }
    d.material.friction = 0.6f;
    d.material.restitution = 0.05f;
    d.ccd_enabled = dynamic;
    d.set_shape(shape.get());
    const okn::math::u32 id = g.phys->create_body(d);
    g.box_shapes.push_back(std::move(shape));
    return id;
}

void reset_level() {
    g.phys = okn::physics::make_jolt_physics_world();
    g.phys->set_gravity({0.0f, -13.0f, 0.0f});
    g.box_shapes.clear();
    g.boxes.clear();
    g.ball_active = false;
    g.aiming = false;
    g.score = 0;
    g.shots = 0;
    g.settle = 0.7f;

    g.ground = add_box({kWorldW * 0.5f, -1.0f}, kWorldW * 0.5f + 2.0f, 1.0f, false);

    // A pyramid of crates near the right.
    const float bx = 10.8f;
    const Rgba8 crate = rgba(206, 150, 92), crate2 = rgba(184, 128, 76);
    auto crate_at = [&](float x, float y, Rgba8 col) {
        Box2 b;
        b.body = add_box({x, y}, 0.5f, 0.5f, true);
        b.hx = 0.5f; b.hy = 0.5f; b.color = col; b.spawn_x = x;
        g.boxes.push_back(b);
    };
    crate_at(bx - 1.0f, 0.5f, crate);
    crate_at(bx,        0.5f, crate2);
    crate_at(bx + 1.0f, 0.5f, crate);
    crate_at(bx - 0.5f, 1.5f, crate2);
    crate_at(bx + 0.5f, 1.5f, crate);
    crate_at(bx,        2.5f, crate2);
}

void launch(Vec2 dir_world) {
    // Slingshot: pull back from the launch point; velocity is the pull vector.
    Vec2 pull{kLaunch.x - dir_world.x, kLaunch.y - dir_world.y};
    float speed = std::sqrt(pull.x * pull.x + pull.y * pull.y) * kPower;
    speed = std::clamp(speed, 0.0f, kMaxSpeed);
    const float len = std::sqrt(pull.x * pull.x + pull.y * pull.y);
    Vec2 vel{0, 0};
    if (len > 0.001f) { vel = {pull.x / len * speed, pull.y / len * speed}; }

    if (!g.ball_shape) { g.ball_shape = std::make_unique<okn::physics::Sphere>(kBallR); }
    RigidBody d;
    d.type = BodyType::kDynamic;
    d.position = {kLaunch.x, kLaunch.y, 0.0f};
    d.mass = 3.0f; d.inv_mass = 1.0f / 3.0f;
    d.material.restitution = 0.35f;
    d.material.friction = 0.5f;
    d.ccd_enabled = true;
    d.linear_velocity = {vel.x, vel.y, 0.0f};
    d.set_shape(g.ball_shape.get());
    g.ball = g.phys->create_body(d);
    g.ball_active = true;
    ++g.shots;
    play(g_launch, 0.5f);
}

// ── Update ────────────────────────────────────────────────────────────────────
void update(float dt) {
    g.phys->step(dt);

    // Impact sounds from contact events (NEW: IPhysicsWorld::drain_contacts). Drain
    // every frame to clear the buffer; while the fresh tower is still settling onto
    // the ground we swallow those contacts so the level starts quiet.
    g.settle = std::max(0.0f, g.settle - dt);
    g.thump_cd = std::max(0.0f, g.thump_cd - dt);
    const bool hit = !g.phys->drain_contacts().empty();
    if (hit && g.settle <= 0.0f && g.thump_cd <= 0.0f) {
        play(g_thump, 0.5f);
        g.thump_cd = 0.06f;
    }

    // Score = crates knocked far from their spawn x.
    int knocked = 0;
    for (const auto& b : g.boxes) {
        if (auto* rb = g.phys->get_body(b.body)) {
            if (std::fabs(rb->position.x - b.spawn_x) > 1.2f || rb->position.y < 0.15f) { ++knocked; }
        }
    }
    g.score = knocked;

    // Retire the ball when it has stopped or left the arena.
    if (g.ball_active) {
        if (auto* rb = g.phys->get_body(g.ball)) {
            const float sp = std::sqrt(rb->linear_velocity.x * rb->linear_velocity.x +
                                       rb->linear_velocity.y * rb->linear_velocity.y);
            if (rb->position.x > kWorldW + 3.0f || rb->position.y < -3.0f ||
                (sp < 0.4f && rb->position.y < 1.2f)) {
                g.phys->destroy_body(g.ball);
                g.ball_active = false;
            }
        }
    }
}

// ── Render ─────────────────────────────────────────────────────────────────────
void quad(SpriteBatch& b, Vec2 c, float w, float h, Rgba8 color, float rot = 0.0f, unsigned tex = 0) {
    Sprite s;
    s.position = c; s.size = {w, h}; s.rotation = rot; s.color = color; s.texture_id = tex;
    b.add(s);
}

// Score digits come from the engine's bitmap font (okn-render bitmap_text.hpp);
// this wrapper just centers the number on cx (width minus the trailing gap).
void draw_num(SpriteBatch& b, int n, float cx, float top, float px) {
    const float total = okn::render::sprite2d::num_width(n, px) - px;
    okn::render::sprite2d::draw_num(b, n, cx - total * 0.5f, top, px, rgba(255, 255, 255));
}

void render() {
    SpriteBatch batch;

    // Ground.
    if (auto* rb = g.phys->get_body(g.ground)) {
        quad(batch, {rb->position.x, rb->position.y}, kWorldW + 4.0f, 2.0f, rgba(120, 90, 60));
    }
    quad(batch, {kWorldW * 0.5f, 0.06f}, kWorldW + 4.0f, 0.12f, rgba(96, 156, 78));

    // Crates (rotate as they tumble).
    for (const auto& b : g.boxes) {
        if (auto* rb = g.phys->get_body(b.body)) {
            quad(batch, {rb->position.x, rb->position.y}, b.hx * 2.0f, b.hy * 2.0f, b.color,
                 rb->rotation.angle_z());
            quad(batch, {rb->position.x, rb->position.y}, b.hx * 1.2f, b.hy * 0.18f,
                 rgba(150, 96, 56), rb->rotation.angle_z());  // plank line
        }
    }

    // Slingshot posts.
    quad(batch, {kLaunch.x - 0.35f, kLaunch.y - 1.4f}, 0.22f, 2.8f, rgba(110, 78, 48));
    quad(batch, {kLaunch.x + 0.35f, kLaunch.y - 1.4f}, 0.22f, 2.8f, rgba(110, 78, 48));

    // Ball (live) or aiming ghost + aim guide.
    if (g.ball_active) {
        if (auto* rb = g.phys->get_body(g.ball)) {
            quad(batch, {rb->position.x, rb->position.y}, kBallR * 2.0f, kBallR * 2.0f,
                 rgba(228, 92, 72), 0.0f, kBallTex);
        }
    } else {
        Vec2 ball_pos = kLaunch;
        if (g.aiming) {
            Vec2 pull{kLaunch.x - g.mouse_world.x, kLaunch.y - g.mouse_world.y};
            const float len = std::sqrt(pull.x * pull.x + pull.y * pull.y);
            ball_pos = {kLaunch.x - pull.x * 0.25f, kLaunch.y - pull.y * 0.25f};  // pulled back a bit
            // Aim guide: dots along the launch direction.
            if (len > 0.1f) {
                const Vec2 dir{pull.x / len, pull.y / len};
                const float power = std::min(len * kPower, kMaxSpeed) / kMaxSpeed;
                for (int i = 1; i <= 6; ++i) {
                    const float t = static_cast<float>(i) * 0.55f * (0.4f + power);
                    quad(batch, {kLaunch.x + dir.x * t, kLaunch.y + dir.y * t}, 0.16f, 0.16f,
                         rgba(255, 255, 255, 180), 0.0f, kBallTex);
                }
            }
        }
        quad(batch, ball_pos, kBallR * 2.0f, kBallR * 2.0f, rgba(228, 92, 72), 0.0f, kBallTex);
    }

    // HUD: score + shots.
    draw_num(batch, g.score, 1.4f, kWorldH - 0.6f, 0.32f);
    quad(batch, {0.9f, kWorldH - 1.1f}, 0.5f, 0.5f, rgba(206, 150, 92));  // crate icon for score

    const float w = sapp_widthf(), h = sapp_heightf();
    Camera2D cam;
    cam.center = {kWorldW * 0.5f, kWorldH * 0.5f};
    cam.viewport_h = kWorldH;
    cam.viewport_w = kWorldH * (w / h);

    sg_pass_action pa{};
    pa.colors[0].load_action = SG_LOADACTION_CLEAR;
    pa.colors[0].clear_value = {0.55f, 0.78f, 0.92f, 1.0f};
    sg_begin_default_pass(&pa, sapp_width(), sapp_height());
    g_renderer->draw(cam, batch.build());
    sg_end_pass();
    sg_commit();
}

// ── sokol callbacks ───────────────────────────────────────────────────────────
Vec2 mouse_to_world(float px, float py) {
    const float w = sapp_widthf(), h = sapp_heightf();
    const float vw = kWorldH * (w / h), vh = kWorldH;
    const float ndc_x = px / w * 2.0f - 1.0f;
    const float ndc_y = py / h * 2.0f - 1.0f;
    return Vec2{kWorldW * 0.5f + ndc_x * vw * 0.5f, kWorldH * 0.5f - ndc_y * vh * 0.5f};
}

void on_init() {
    sg_desc d{};
    d.context = sapp_sgcontext();
    d.logger.func = slog_func;
    sg_setup(&d);

    g_renderer = std::make_unique<GpuSpriteRenderer>();
    g_renderer->init();
    g_renderer->upload_texture(kBallTex, make_circle_tex());

    if (g_audio.initialize()) {
        g_pb = std::make_unique<okn::audio::AudioPlayback>(g_audio);
        g_thump = decode(150.0f, 0.13f, 1.0f);
        g_launch = decode(520.0f, 0.12f, 1.4f);
    }
    reset_level();
}

void on_event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        g.mouse_px = {ev->mouse_x, ev->mouse_y};
    } else if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN && ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
        g.mouse_px = {ev->mouse_x, ev->mouse_y};
        if (!g.ball_active) { g.aiming = true; }
    } else if (ev->type == SAPP_EVENTTYPE_MOUSE_UP && ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
        if (g.aiming) { g.aiming = false; g.release_request = true; }
    } else if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
        if (ev->key_code == SAPP_KEYCODE_R) { reset_level(); }
        else if (ev->key_code == SAPP_KEYCODE_ESCAPE) { sapp_request_quit(); }
    }
}

void on_frame() {
    float dt = static_cast<float>(sapp_frame_duration());
    dt = std::clamp(dt, 0.0f, 1.0f / 30.0f);

    g.mouse_world = mouse_to_world(g.mouse_px.x, g.mouse_px.y);
    if (g.release_request) {
        g.release_request = false;
        if (!g.ball_active) { launch(g.mouse_world); }
    }
    update(dt);
    render();
}

void on_cleanup() {
    for (auto* b : {&g_thump, &g_launch}) {
        if (b->data) { delete[] b->data; b->data = nullptr; }
    }
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
    d.width = 880;
    d.height = 660;   // 16:12 = 4:3
    d.high_dpi = true;   // native-res framebuffer; the PerMonitorV2 manifest keeps viewport==window
    d.window_title = "Knockdown — OmniKillerNexus (physics)";
    d.logger.func = slog_func;
    return d;
}
