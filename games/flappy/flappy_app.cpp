// Flappy Bird, built on the OmniKillerNexus framework.
//
// Uses the engine's 2D GPU sprite path (GpuSpriteRenderer + SpriteBatch + Camera2D),
// okn-audio (WAV decode + playback) for the flap/score/hit beeps, and sokol_app for
// the window + keyboard/mouse input. The gameplay (gravity, scrolling pipes, AABB
// collision, scoring) is plain C++ — Flappy Bird needs no physics engine.

#include <okn/render/sprite2d/sprite_batch.hpp>
#include <okn/render/sprite2d/camera2d.hpp>
#include <okn/render/sprite2d/gpu_sprite_renderer.hpp>

#include <okn/audio/backend/audio_engine.hpp>
#include <okn/audio/mixer/playback.hpp>
#include <okn/audio/decode/wav_decoder.hpp>

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
using okn::render::sprite2d::Rgba8;
using okn::render::sprite2d::Sprite;
using okn::render::sprite2d::SpriteBatch;
using okn::render::sprite2d::rgba;
using okn::math::Vec2;

namespace {

// ── World constants (virtual units; +Y up, origin bottom-left) ────────────────
constexpr float kWorldW = 400.0f;
constexpr float kWorldH = 600.0f;
constexpr float kGroundH = 80.0f;
constexpr float kBirdX = 120.0f;
constexpr float kBirdW = 36.0f;
constexpr float kBirdH = 26.0f;
constexpr float kGravity = 1350.0f;
constexpr float kFlapV = 430.0f;
constexpr float kPipeW = 72.0f;
constexpr float kGap = 175.0f;
constexpr float kSpacing = 230.0f;
constexpr float kScroll = 135.0f;

struct Pipe {
    float x = 0.0f;
    float gap_center = 0.0f;
    bool scored = false;
};

struct Game {
    float bird_y = kWorldH * 0.5f;
    float bird_vy = 0.0f;
    std::vector<Pipe> pipes;
    int score = 0;
    bool started = false;
    bool game_over = false;
    std::uint32_t rng = 2463534242u;
};

Game g;
std::unique_ptr<GpuSpriteRenderer> g_renderer;
okn::audio::AudioEngine g_audio;
std::unique_ptr<okn::audio::AudioPlayback> g_pb;
okn::audio::AudioBuffer g_flap{}, g_score{}, g_hit{};

float frand() {
    g.rng ^= g.rng << 13;
    g.rng ^= g.rng >> 17;
    g.rng ^= g.rng << 5;
    return static_cast<float>(g.rng & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

// ── Audio ─────────────────────────────────────────────────────────────────────
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
        const int s = static_cast<int>(std::sin(t * freq * 6.2831853f) * 9000.0f * env);
        u16le(static_cast<unsigned>(static_cast<short>(s)) & 0xFFFF);
    }
    return w;
}

okn::audio::AudioBuffer decode_beep(float freq, float dur, float decay) {
    okn::audio::WavDecoder dec;
    const auto wav = make_beep(freq, dur, decay);
    return dec.decode(wav.data(), wav.size());
}

void play(const okn::audio::AudioBuffer& b, float vol) {
    if (g_pb && b.data) { g_pb->play(b, vol); }
}

// ── Game logic ──────────────────────────────────────────────────────────────
void reset() {
    g.bird_y = kWorldH * 0.5f;
    g.bird_vy = 0.0f;
    g.pipes.clear();
    g.score = 0;
    g.started = false;
    g.game_over = false;
}

void spawn_pipe(float x) {
    Pipe p;
    p.x = x;
    const float lo = kGroundH + kGap * 0.5f + 36.0f;
    const float hi = kWorldH - kGap * 0.5f - 36.0f;
    p.gap_center = lo + frand() * (hi - lo);
    g.pipes.push_back(p);
}

void flap() {
    if (g.game_over) { reset(); }
    g.started = true;
    g.bird_vy = kFlapV;
    play(g_flap, 0.5f);
}

bool bird_hits(const Pipe& p) {
    const float bl = kBirdX - kBirdW * 0.5f, br = kBirdX + kBirdW * 0.5f;
    const float pl = p.x - kPipeW * 0.5f, pr = p.x + kPipeW * 0.5f;
    if (br < pl || bl > pr) { return false; }
    const float bt = g.bird_y + kBirdH * 0.5f, bb = g.bird_y - kBirdH * 0.5f;
    return bt > p.gap_center + kGap * 0.5f || bb < p.gap_center - kGap * 0.5f;
}

void update(float dt) {
    if (!g.started) { return; }

    g.bird_vy -= kGravity * dt;
    g.bird_y += g.bird_vy * dt;

    // Soft ceiling.
    if (g.bird_y + kBirdH * 0.5f > kWorldH) {
        g.bird_y = kWorldH - kBirdH * 0.5f;
        g.bird_vy = 0.0f;
    }

    if (g.game_over) {
        if (g.bird_y - kBirdH * 0.5f < kGroundH) { g.bird_y = kGroundH + kBirdH * 0.5f; g.bird_vy = 0.0f; }
        return;
    }

    for (auto& p : g.pipes) { p.x -= kScroll * dt; }
    if (!g.pipes.empty() && g.pipes.front().x < -kPipeW) {
        g.pipes.erase(g.pipes.begin());
    }
    if (g.pipes.empty() || g.pipes.back().x < kWorldW - kSpacing) {
        spawn_pipe(g.pipes.empty() ? kWorldW + 80.0f : g.pipes.back().x + kSpacing);
    }

    for (auto& p : g.pipes) {
        if (!p.scored && p.x < kBirdX) { p.scored = true; ++g.score; play(g_score, 0.4f); }
        if (bird_hits(p)) { g.game_over = true; play(g_hit, 0.6f); }
    }
    if (g.bird_y - kBirdH * 0.5f < kGroundH) {
        g.bird_y = kGroundH + kBirdH * 0.5f;
        g.game_over = true;
        play(g_hit, 0.6f);
    }
}

// ── Rendering ─────────────────────────────────────────────────────────────────
void rect(SpriteBatch& b, float cx, float cy, float w, float h, Rgba8 color, float rot = 0.0f) {
    Sprite s;
    s.position = {cx, cy};
    s.size = {w, h};
    s.rotation = rot;
    s.color = color;
    b.add(s);
}

// 3x5 pixel font for digits 0-9 (rows top->bottom).
const char* kFont[10][5] = {
    {"###", "# #", "# #", "# #", "###"},  // 0
    {" # ", "## ", " # ", " # ", "###"},  // 1
    {"###", "  #", "###", "#  ", "###"},  // 2
    {"###", "  #", "###", "  #", "###"},  // 3
    {"# #", "# #", "###", "  #", "  #"},  // 4
    {"###", "#  ", "###", "  #", "###"},  // 5
    {"###", "#  ", "###", "# #", "###"},  // 6
    {"###", "  #", " # ", " # ", " # "},  // 7
    {"###", "# #", "###", "# #", "###"},  // 8
    {"###", "# #", "###", "  #", "###"},  // 9
};

void draw_digit(SpriteBatch& b, int d, float left, float top, float px, Rgba8 color) {
    if (d < 0 || d > 9) { return; }
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (kFont[d][row][col] == '#') {
                rect(b, left + (static_cast<float>(col) + 0.5f) * px,
                     top - (static_cast<float>(row) + 0.5f) * px, px, px, color);
            }
        }
    }
}

void draw_number(SpriteBatch& b, int n, float center_x, float top, float px) {
    if (n < 0) { n = 0; }
    int digits[10];
    int count = 0;
    do { digits[count++] = n % 10; n /= 10; } while (n > 0 && count < 10);
    const float dw = 3.0f * px + px;  // digit width + spacing
    const float total = static_cast<float>(count) * dw - px;
    float x = center_x - total * 0.5f;
    for (int i = count - 1; i >= 0; --i) {
        draw_digit(b, digits[i], x, top, px, rgba(255, 255, 255));
        // small shadow under each digit for readability
        x += dw;
    }
}

void build_scene(SpriteBatch& b) {
    // Pipes (each = top + bottom green column with a darker cap).
    for (const auto& p : g.pipes) {
        const float gap_top = p.gap_center + kGap * 0.5f;
        const float gap_bot = p.gap_center - kGap * 0.5f;
        const float top_h = kWorldH - gap_top;
        const float bot_h = gap_bot - kGroundH;
        const Rgba8 body = rgba(80, 184, 72);
        const Rgba8 cap = rgba(96, 208, 84);
        if (top_h > 0.0f) {
            rect(b, p.x, gap_top + top_h * 0.5f, kPipeW, top_h, body);
            rect(b, p.x, gap_top + 12.0f, kPipeW + 10.0f, 24.0f, cap);
        }
        if (bot_h > 0.0f) {
            rect(b, p.x, kGroundH + bot_h * 0.5f, kPipeW, bot_h, body);
            rect(b, p.x, gap_bot - 12.0f, kPipeW + 10.0f, 24.0f, cap);
        }
    }
    // Ground.
    rect(b, kWorldW * 0.5f, kGroundH * 0.5f, kWorldW, kGroundH, rgba(222, 196, 120));
    rect(b, kWorldW * 0.5f, kGroundH - 6.0f, kWorldW, 12.0f, rgba(120, 176, 88));

    // Bird (tilts with vertical speed).
    const float tilt = std::clamp(g.bird_vy / 600.0f, -0.5f, 0.9f);
    rect(b, kBirdX, g.bird_y, kBirdW, kBirdH, rgba(248, 216, 72), tilt);
    rect(b, kBirdX + 10.0f, g.bird_y + 5.0f, 7.0f, 7.0f, rgba(40, 40, 40), tilt);   // eye
    rect(b, kBirdX + 20.0f, g.bird_y, 12.0f, 8.0f, rgba(236, 132, 52), tilt);       // beak

    // Score.
    draw_number(b, g.score, kWorldW * 0.5f, kWorldH - 40.0f, 9.0f);

    // Prompt squares (a small blinking marker) before start / on game over.
    if (!g.started || g.game_over) {
        const Rgba8 c = g.game_over ? rgba(220, 80, 80) : rgba(255, 255, 255);
        rect(b, kWorldW * 0.5f, kWorldH * 0.5f - 60.0f, 60.0f, 8.0f, c);
        rect(b, kWorldW * 0.5f, kWorldH * 0.5f - 76.0f, 90.0f, 8.0f, c);
    }
}

// ── sokol callbacks ───────────────────────────────────────────────────────────
void on_init() {
    sg_desc d{};
    d.context = sapp_sgcontext();
    d.logger.func = slog_func;
    sg_setup(&d);

    g_renderer = std::make_unique<GpuSpriteRenderer>();
    g_renderer->init();

    if (g_audio.initialize()) {
        g_pb = std::make_unique<okn::audio::AudioPlayback>(g_audio);
        g_flap = decode_beep(720.0f, 0.09f, 1.5f);
        g_score = decode_beep(960.0f, 0.10f, 1.2f);
        g_hit = decode_beep(180.0f, 0.20f, 0.8f);
    }
    reset();
}

void on_event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
        switch (ev->key_code) {
            case SAPP_KEYCODE_SPACE:
            case SAPP_KEYCODE_UP:
            case SAPP_KEYCODE_W: flap(); break;
            case SAPP_KEYCODE_ESCAPE: sapp_request_quit(); break;
            default: break;
        }
    } else if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN) {
        flap();
    }
}

void on_frame() {
    float dt = static_cast<float>(sapp_frame_duration());
    dt = std::clamp(dt, 0.0f, 1.0f / 30.0f);
    update(dt);

    const float w = sapp_widthf();
    const float h = sapp_heightf();
    Camera2D cam;
    cam.center = {kWorldW * 0.5f, kWorldH * 0.5f};
    cam.viewport_h = kWorldH;
    cam.viewport_w = kWorldH * (w / h);

    SpriteBatch batch;
    build_scene(batch);

    sg_pass_action pa{};
    pa.colors[0].load_action = SG_LOADACTION_CLEAR;
    pa.colors[0].clear_value = {0.49f, 0.75f, 0.93f, 1.0f};   // sky
    sg_begin_default_pass(&pa, sapp_width(), sapp_height());
    g_renderer->draw(cam, batch.build());
    sg_end_pass();
    sg_commit();
}

void on_cleanup() {
    for (auto* buf : {&g_flap, &g_score, &g_hit}) {
        if (buf->data) { delete[] buf->data; buf->data = nullptr; }
    }
    g_pb.reset();
    if (g_audio.is_initialized()) { g_audio.shutdown(); }
    if (g_renderer) { g_renderer->shutdown(); g_renderer.reset(); }
    sg_shutdown();
}

}  // namespace

sapp_desc sokol_main(int /*argc*/, char* /*argv*/[]) {
    sapp_desc d{};
    d.init_cb = on_init;
    d.frame_cb = on_frame;
    d.cleanup_cb = on_cleanup;
    d.event_cb = on_event;
    d.width = 460;
    d.height = 690;   // ~ 400:600 world aspect
    d.window_title = "Flappy Bird — OmniKillerNexus";
    d.logger.func = slog_func;
    return d;
}
