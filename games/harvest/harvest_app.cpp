#define _CRT_SECURE_NO_WARNINGS  // std::getenv

// OmniHarvest — a Stardew-Valley-like 2D TILE-GRID farming sim.
//
// A deliberately DIFFERENT genre from the engine's other demos (side-scrollers /
// 3D platformer): top-down, grid-based, and — on purpose — using NO physics at
// all. It proves the 2D sprite stack alone (SpriteBatch + GpuSpriteRenderer +
// Camera2D + Image + audio + an input action-map) is enough to carry a whole game.
//
// Systems exercised: a scrolling tilemap (a grid of hundreds of quads, view-culled),
// grid-based smooth-stepped movement with tile collision, a tool/interaction model
// (hoe → till, seed → plant, can → water), crops that grow per in-game day, a
// harvest→gold economy, and a HUD with a tool hotbar. Built on the same windowed
// sokol DX11 path + PerMonitorV2 DPI manifest as the other games.

#include <okn/render/sprite2d/sprite_batch.hpp>
#include <okn/render/sprite2d/camera2d.hpp>
#include <okn/render/sprite2d/gpu_sprite_renderer.hpp>
#include <okn/render/sprite2d/image.hpp>

#include <okn/audio/backend/audio_engine.hpp>
#include <okn/audio/mixer/playback.hpp>
#include <okn/audio/decode/wav_decoder.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
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
using okn::math::u32;

namespace {

// ── world dimensions / tuning ────────────────────────────────────────────────────
constexpr int kMapW = 40;
constexpr int kMapH = 30;
constexpr int kRipe = 3;          // crop stages: 0 (sprout) .. kRipe (harvestable)
constexpr int kSeedCost = 10;
constexpr int kHarvestValue = 35;
constexpr int kStartGold = 100;
constexpr int kGoldGoal = 200;    // human win: reach this much gold
constexpr float kViewTiles = 16.0f;   // vertical view height, in tiles
constexpr float kStepSpeed = 6.0f;    // tiles/sec the sprite slides between cells
constexpr unsigned kTexFarmer = 1;

enum class Tile : std::uint8_t { Grass, Water, Tree, Soil };
enum class Tool : int { Hoe, Seed, Can, Count };
enum class State { Playing, Win };

struct Crop { bool planted = false; int stage = 0; bool watered = false; };

// ── input action-map (same pattern as the other games) ────────────────────────────
enum class Action { Up, Down, Left, Right, Act, Sleep, Count };
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
    void end_frame() { for (bool& p : pressed) { p = false; } }
    bool held(Action a) const { return down[static_cast<int>(a)]; }
    bool just(Action a) const { return pressed[static_cast<int>(a)]; }
};

struct Game {
    std::array<Tile, kMapW * kMapH> tiles{};
    std::array<Crop, kMapW * kMapH> crops{};
    int ptx = 20, pty = 15;        // player tile
    int fx = 0, fy = 1;            // facing (default: down)
    float pox = 0.0f, poy = 0.0f;  // smooth render offset (world units), lerps to 0
    bool facing_right = true;
    Tool tool = Tool::Hoe;
    int gold = kStartGold;
    int day = 1;
    int harvested = 0;
    State state = State::Playing;
    InputMap input;

    bool autodemo = false;
    int demo_step = 0;
    float demo_t = 0.0f;
    float trace_t = 0.0f;

    Tile& at(int x, int y) { return tiles[static_cast<std::size_t>(y) * kMapW + x]; }
    Tile at(int x, int y) const { return tiles[static_cast<std::size_t>(y) * kMapW + x]; }
    Crop& crop(int x, int y) { return crops[static_cast<std::size_t>(y) * kMapW + x]; }
    const Crop& crop(int x, int y) const { return crops[static_cast<std::size_t>(y) * kMapW + x]; }
};

Game g;
std::unique_ptr<GpuSpriteRenderer> g_renderer;
okn::audio::AudioEngine g_audio;
std::unique_ptr<okn::audio::AudioPlayback> g_pb;
okn::audio::AudioBuffer g_till{}, g_plant{}, g_water{}, g_harvest{}, g_sleep{}, g_deny{};

// ── audio (tiny synthesized beeps, like the other games) ──────────────────────────
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

// ── farmer sprite (the one texture; everything else is colored quads) ─────────────
void write_png(const char* path, const Image& img) {
    stbi_write_png(path, img.width(), img.height(), 4, img.pixels().data(), img.width() * 4);
}
Image make_farmer() {
    Image im(16, 20, rgba(0, 0, 0, 0));
    auto fill = [&](int x0, int y0, int x1, int y1, Rgba8 c) {
        for (int y = y0; y <= y1; ++y) { for (int x = x0; x <= x1; ++x) { im.set(x, y, c); } } };
    const Rgba8 straw = rgba(225, 200, 110), skin = rgba(245, 200, 150),
                green = rgba(70, 150, 80), denim = rgba(70, 100, 170), boot = rgba(90, 55, 30);
    fill(2, 2, 13, 4, straw);               // straw hat brim
    fill(4, 0, 11, 2, straw);               // hat crown
    fill(4, 5, 11, 9, skin);                // face
    im.set(6, 7, rgba(30, 30, 40)); im.set(9, 7, rgba(30, 30, 40));   // eyes
    fill(4, 10, 11, 14, green);             // shirt
    fill(4, 14, 11, 18, denim);             // overalls
    fill(4, 18, 7, 19, boot); fill(8, 18, 11, 19, boot);              // boots
    return im;
}
void load_sprites() {
    write_png("harvest_farmer.png", make_farmer());
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* d = stbi_load("harvest_farmer.png", &w, &h, &ch, 4);
    if (!d) { return; }
    Image im(w, h, rgba(0, 0, 0, 0));
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const unsigned char* px = d + (static_cast<std::size_t>(y) * w + x) * 4;
        im.set(x, y, rgba(px[0], px[1], px[2], px[3]));
    }
    stbi_image_free(d);
    g_renderer->upload_texture(kTexFarmer, im);
}

// ── world generation (fully deterministic so the autodemo's plot is always clear) ──
unsigned hash2(int x, int y) {
    unsigned h = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(y) * 19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
void build_world() {
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            Tile t = Tile::Grass;
            // A border ring of trees frames the map.
            if (x == 0 || y == 0 || x == kMapW - 1 || y == kMapH - 1) { t = Tile::Tree; }
            // A pond in the top-left quadrant.
            else if (x >= 5 && x <= 11 && y >= 4 && y <= 8) { t = Tile::Water; }
            // Scattered tree clumps — but NEVER inside the central farm plot.
            else {
                const bool in_plot = (x >= 14 && x <= 30 && y >= 10 && y <= 22);
                if (!in_plot && (hash2(x, y) % 100u) < 7u) { t = Tile::Tree; }
            }
            g.at(x, y) = t;
            g.crop(x, y) = Crop{};
        }
    }
    g.ptx = 20; g.pty = 15; g.pox = 0.0f; g.poy = 0.0f; g.fx = 0; g.fy = 1;
}

void reset_game() {
    g.gold = kStartGold; g.day = 1; g.harvested = 0; g.state = State::Playing;
    g.tool = Tool::Hoe; g.facing_right = true;
    build_world();
}

// ── grid helpers + per-tile actions (shared by player input AND the autodemo) ──────
Vec2 tile_world(int tx, int ty) { return {static_cast<float>(tx) + 0.5f, -static_cast<float>(ty) - 0.5f}; }
Vec2 player_world() { return {static_cast<float>(g.ptx) + 0.5f + g.pox, -static_cast<float>(g.pty) - 0.5f + g.poy}; }
bool in_bounds(int x, int y) { return x >= 0 && y >= 0 && x < kMapW && y < kMapH; }
bool walkable(int x, int y) {
    if (!in_bounds(x, y)) { return false; }
    const Tile t = g.at(x, y);
    return t == Tile::Grass || t == Tile::Soil;   // water + trees block
}

void till(int x, int y) {
    if (in_bounds(x, y) && g.at(x, y) == Tile::Grass) { g.at(x, y) = Tile::Soil; play(g_till, 0.5f); }
}
bool plant(int x, int y) {
    if (in_bounds(x, y) && g.at(x, y) == Tile::Soil && !g.crop(x, y).planted && g.gold >= kSeedCost) {
        g.crop(x, y) = Crop{true, 0, false}; g.gold -= kSeedCost; play(g_plant, 0.5f); return true;
    }
    play(g_deny, 0.3f); return false;
}
void water(int x, int y) {
    Crop& c = g.crop(x, y);
    if (in_bounds(x, y) && c.planted && !c.watered) { c.watered = true; play(g_water, 0.4f); }
}
bool harvest(int x, int y) {
    Crop& c = g.crop(x, y);
    if (in_bounds(x, y) && c.planted && c.stage >= kRipe) {
        c = Crop{}; g.gold += kHarvestValue; ++g.harvested; play(g_harvest, 0.6f);
        if (g.gold >= kGoldGoal) { g.state = State::Win; }
        return true;
    }
    return false;
}

// Use the held tool on the faced tile — but a ripe crop is always harvested first.
void do_action() {
    const int tx = g.ptx + g.fx, ty = g.pty + g.fy;
    if (harvest(tx, ty)) { return; }
    switch (g.tool) {
        case Tool::Hoe:  till(tx, ty); break;
        case Tool::Seed: plant(tx, ty); break;
        case Tool::Can:  water(tx, ty); break;
        default: break;
    }
}

void do_sleep() {
    ++g.day;
    for (auto& c : g.crops) {
        if (c.planted && c.watered) { c.stage = std::min(kRipe, c.stage + 1); }
        c.watered = false;   // soil dries overnight — must re-water each day
    }
    play(g_sleep, 0.5f);
}

void try_step(int dx, int dy) {
    g.fx = dx; g.fy = dy;                                   // face the way we tried, even if blocked
    if (dx > 0) { g.facing_right = true; } else if (dx < 0) { g.facing_right = false; }
    const int nx = g.ptx + dx, ny = g.pty + dy;
    if (walkable(nx, ny)) {
        g.ptx = nx; g.pty = ny;
        g.pox = -static_cast<float>(dx); g.poy = static_cast<float>(dy);   // start at the old cell, slide to new
    }
}

// ── scripted autodemo: drives the real action functions (no synthetic keys) ───────
// Tills a 5-wide plot, plants+waters it, then runs the daily water→sleep cycle until
// the crops ripen, harvests them, and writes a result marker. Also steps the player a
// few tiles to exercise grid movement + collision.
void run_autodemo() {
    constexpr float kStepDt = 0.18f;
    g.demo_t += static_cast<float>(sapp_frame_duration());
    if (g.demo_t < kStepDt) { return; }
    g.demo_t = 0.0f;
    const int plot_x0 = 18, plot_y = 15;   // the row the player starts on
    const int plot_n = 5;
    const int s = g.demo_step++;

    if (s < 3) {                                  // walk right 3 tiles (movement + collision)
        try_step(1, 0);
    } else if (s < 3 + plot_n) {                  // till the plot
        g.tool = Tool::Hoe; till(plot_x0 + (s - 3), plot_y);
    } else if (s < 3 + 2 * plot_n) {              // plant seeds
        g.tool = Tool::Seed; plant(plot_x0 + (s - 3 - plot_n), plot_y);
    } else {
        // Daily cycle: water the whole plot, then sleep — repeat until ripe, then harvest.
        const int cyc = s - (3 + 2 * plot_n);
        if (cyc < kRipe * (plot_n + 1)) {
            const int within = cyc % (plot_n + 1);
            if (within < plot_n) { g.tool = Tool::Can; water(plot_x0 + within, plot_y); }
            else { do_sleep(); }
        } else if (cyc < kRipe * (plot_n + 1) + plot_n) {
            harvest(plot_x0 + (cyc - kRipe * (plot_n + 1)), plot_y);
        } else {
            std::ofstream rf("harvest_result.txt");
            rf << (g.harvested > 0 && g.gold > kStartGold ? "OKHARVEST WIN" : "OKHARVEST FAIL")
               << " gold=" << g.gold << " day=" << g.day << " harvested=" << g.harvested;
            g.autodemo = false;   // stop driving; leave the final state on screen
        }
    }
}

void update(float dt) {
    // ── movement: slide the render offset toward 0; step again when settled ──
    const float decay = kStepSpeed * dt;
    auto relax = [&](float& o) { if (o > 0) { o = std::max(0.0f, o - decay); } else if (o < 0) { o = std::min(0.0f, o + decay); } };
    relax(g.pox); relax(g.poy);
    const bool settled = std::fabs(g.pox) < 0.001f && std::fabs(g.poy) < 0.001f;

    if (g.autodemo) { run_autodemo(); }
    else if (g.state == State::Playing && settled) {
        if (g.input.held(Action::Up))    { try_step(0, -1); }
        else if (g.input.held(Action::Down))  { try_step(0, 1); }
        else if (g.input.held(Action::Left))  { try_step(-1, 0); }
        else if (g.input.held(Action::Right)) { try_step(1, 0); }
    }

    if (!g.autodemo && g.state == State::Playing) {
        if (g.input.just(Action::Act)) { do_action(); }
        if (g.input.just(Action::Sleep)) { do_sleep(); }
    }

    if (g.autodemo) {
        g.trace_t += dt;
        if (g.trace_t >= 0.5f) {
            g.trace_t = 0.0f;
            std::ofstream f("harvest_trace.txt", std::ios::app);
            if (f) {
                f << "ptx=" << g.ptx << " pty=" << g.pty << " gold=" << g.gold << " day=" << g.day
                  << " tool=" << static_cast<int>(g.tool) << " harvested=" << g.harvested
                  << " state=" << static_cast<int>(g.state) << "\n";
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

void draw_tool_icon(SpriteBatch& b, Tool t, Vec2 c, float sz) {
    switch (t) {
        case Tool::Hoe:                                                    // brown handle + gray head
            quad(b, {c.x - sz * 0.1f, c.y}, sz * 0.12f, sz * 0.7f, rgba(140, 90, 50));
            quad(b, {c.x + sz * 0.12f, c.y + sz * 0.28f}, sz * 0.42f, sz * 0.14f, rgba(180, 180, 190));
            break;
        case Tool::Seed:                                                   // a little seed pouch
            quad(b, c, sz * 0.5f, sz * 0.5f, rgba(150, 110, 60));
            quad(b, {c.x, c.y + sz * 0.3f}, sz * 0.28f, sz * 0.16f, rgba(110, 80, 40));
            break;
        case Tool::Can:                                                    // watering can body + spout
            quad(b, c, sz * 0.5f, sz * 0.45f, rgba(120, 150, 180));
            quad(b, {c.x + sz * 0.36f, c.y + sz * 0.08f}, sz * 0.3f, sz * 0.12f, rgba(120, 150, 180));
            break;
        default: break;
    }
}

void render() {
    SpriteBatch world, hud;

    const float w = sapp_widthf(), h = sapp_heightf();
    const float vh = kViewTiles, vw = vh * (w / h);

    // Camera centered on the player, clamped so the view stays inside the map.
    const Vec2 pw = player_world();
    const float hw = vw * 0.5f, hhh = vh * 0.5f;
    Camera2D wc;
    wc.center = {std::clamp(pw.x, hw, static_cast<float>(kMapW) - hw),
                 std::clamp(pw.y, -static_cast<float>(kMapH) + hhh, -hhh)};
    wc.viewport_h = vh; wc.viewport_w = vw;

    // View-cull: only emit tiles inside the camera window (+1 margin).
    const int x0 = std::max(0, static_cast<int>(wc.center.x - hw) - 1);
    const int x1 = std::min(kMapW - 1, static_cast<int>(wc.center.x + hw) + 1);
    const int y0 = std::max(0, static_cast<int>(-wc.center.y - hhh) - 1);
    const int y1 = std::min(kMapH - 1, static_cast<int>(-wc.center.y + hhh) + 1);

    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            const Vec2 c = tile_world(tx, ty);
            const bool chk = ((tx + ty) & 1) != 0;
            switch (g.at(tx, ty)) {
                case Tile::Grass:
                    quad(world, c, 1.0f, 1.0f, chk ? rgba(122, 192, 98) : rgba(112, 180, 90));
                    break;
                case Tile::Water:
                    quad(world, c, 1.0f, 1.0f, chk ? rgba(74, 134, 204) : rgba(66, 122, 192));
                    break;
                case Tile::Soil: {
                    const Crop& cr = g.crop(tx, ty);
                    const bool wet = cr.planted && cr.watered;
                    quad(world, c, 1.0f, 1.0f, wet ? rgba(92, 60, 38) : rgba(146, 102, 64));
                    quad(world, c, 0.92f, 0.92f, wet ? rgba(104, 68, 42) : rgba(160, 114, 72));  // inset furrow
                    break;
                }
                case Tile::Tree:
                    quad(world, c, 1.0f, 1.0f, chk ? rgba(122, 192, 98) : rgba(112, 180, 90));   // grass under
                    quad(world, {c.x, c.y - 0.18f}, 0.22f, 0.5f, rgba(96, 62, 36));               // trunk
                    quad(world, {c.x, c.y + 0.18f}, 0.9f, 0.78f, rgba(54, 132, 70));              // canopy
                    quad(world, {c.x, c.y + 0.30f}, 0.62f, 0.5f, rgba(66, 152, 84));
                    break;
            }
            // crops on soil (size + color by stage; ripe sprouts a fruit)
            const Crop& cr = g.crop(tx, ty);
            if (cr.planted) {
                const float gscale = 0.18f + 0.20f * static_cast<float>(cr.stage);
                quad(world, {c.x, c.y - 0.18f}, 0.12f, gscale, rgba(60, 140, 70));       // stalk
                quad(world, {c.x, c.y - 0.18f + gscale * 0.5f}, gscale * 0.9f, gscale * 0.7f,
                     cr.stage >= kRipe ? rgba(70, 165, 80) : rgba(86, 178, 96));          // leaves
                if (cr.stage >= kRipe) {
                    quad(world, {c.x, c.y + 0.06f}, 0.28f, 0.28f, rgba(225, 90, 70));     // ripe fruit
                }
            }
        }
    }

    // Tile cursor: outline the faced tile (the action target).
    {
        const Vec2 fc = tile_world(g.ptx + g.fx, g.pty + g.fy);
        const Rgba8 cur = rgba(255, 255, 255, 110);
        quad(world, {fc.x, fc.y + 0.46f}, 1.0f, 0.08f, cur);
        quad(world, {fc.x, fc.y - 0.46f}, 1.0f, 0.08f, cur);
        quad(world, {fc.x - 0.46f, fc.y}, 0.08f, 1.0f, cur);
        quad(world, {fc.x + 0.46f, fc.y}, 0.08f, 1.0f, cur);
    }

    // Player (the one textured sprite), feet roughly on the tile.
    quad(world, {pw.x, pw.y + 0.18f}, 0.8f, 1.0f, rgba(255, 255, 255), kTexFarmer, !g.facing_right);

    // ── HUD (screen-space camera) ──
    Camera2D hc; hc.center = {vw * 0.5f, vh * 0.5f}; hc.viewport_h = vh; hc.viewport_w = vw;
    const float top = vh - 0.5f;
    quad(hud, {vw * 0.5f, top}, vw, 1.0f, rgba(40, 32, 24, 200));            // top bar
    // gold (coin + number)
    quad(hud, {0.8f, top}, 0.5f, 0.5f, rgba(255, 215, 70));
    draw_num(hud, g.gold, 1.2f, top + 0.25f, 0.18f, rgba(255, 245, 150));
    // day (sun + number)
    quad(hud, {vw * 0.5f - 0.5f, top}, 0.45f, 0.45f, rgba(255, 220, 120));
    draw_num(hud, g.day, vw * 0.5f, top + 0.25f, 0.18f, rgba(255, 255, 255));
    // goal progress hint
    draw_num(hud, kGoldGoal, vw - 2.0f, top + 0.25f, 0.16f, rgba(180, 230, 180));

    // tool hotbar (3 slots, bottom-center; selected slot highlighted)
    const float slot = 1.1f, bx = vw * 0.5f - slot, by = 0.85f;
    for (int i = 0; i < static_cast<int>(Tool::Count); ++i) {
        const Vec2 sc{bx + slot * static_cast<float>(i), by};
        const bool sel = static_cast<int>(g.tool) == i;
        quad(hud, sc, slot * 0.92f, slot * 0.92f, sel ? rgba(250, 230, 120) : rgba(60, 50, 40, 220));
        quad(hud, sc, slot * 0.74f, slot * 0.74f, rgba(35, 28, 22, 230));
        draw_tool_icon(hud, static_cast<Tool>(i), sc, slot * 0.7f);
    }

    if (g.state == State::Win) {
        for (int i = 0; i < 5; ++i) { quad(hud, {vw * 0.5f, vh * 0.5f - i * 0.5f}, 8.0f, 0.4f, rgba(120, 230, 120)); }
    }

    sg_pass_action pa{};
    pa.colors[0].load_action = SG_LOADACTION_CLEAR;
    pa.colors[0].clear_value = {0.30f, 0.52f, 0.40f, 1.0f};
    sg_begin_default_pass(&pa, sapp_width(), sapp_height());
    g_renderer->begin_frame();
    g_renderer->add(wc, world.build());
    g_renderer->add(hc, hud.build());
    g_renderer->end_frame();
    sg_end_pass();
    sg_commit();
}

// ── sokol callbacks ─────────────────────────────────────────────────────────────
void on_init() {
    sg_desc d{}; d.context = sapp_sgcontext(); d.logger.func = slog_func; sg_setup(&d);
    g_renderer = std::make_unique<GpuSpriteRenderer>();
    g_renderer->init();
    load_sprites();
    if (g_audio.initialize()) {
        g_pb = std::make_unique<okn::audio::AudioPlayback>(g_audio);
        g_till = decode(300.0f, 0.10f, 1.2f);
        g_plant = decode(600.0f, 0.10f, 1.2f);
        g_water = decode(820.0f, 0.14f, 1.0f);
        g_harvest = decode(720.0f, 0.30f, 0.7f, /*rising=*/true);
        g_sleep = decode(180.0f, 0.40f, 0.8f);
        g_deny = decode(140.0f, 0.10f, 1.4f);
    }
    g.input.bind(Action::Up, SAPP_KEYCODE_W, SAPP_KEYCODE_UP);
    g.input.bind(Action::Down, SAPP_KEYCODE_S, SAPP_KEYCODE_DOWN);
    g.input.bind(Action::Left, SAPP_KEYCODE_A, SAPP_KEYCODE_LEFT);
    g.input.bind(Action::Right, SAPP_KEYCODE_D, SAPP_KEYCODE_RIGHT);
    g.input.bind(Action::Act, SAPP_KEYCODE_SPACE, SAPP_KEYCODE_E);
    g.input.bind(Action::Sleep, SAPP_KEYCODE_T, SAPP_KEYCODE_ENTER);
    g.autodemo = (std::getenv("OKN_HARVEST_AUTODEMO") != nullptr);
    reset_game();
}

void on_event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
        switch (ev->key_code) {
            case SAPP_KEYCODE_ESCAPE: sapp_request_quit(); return;
            case SAPP_KEYCODE_R: reset_game(); return;
            case SAPP_KEYCODE_1: g.tool = Tool::Hoe; return;
            case SAPP_KEYCODE_2: g.tool = Tool::Seed; return;
            case SAPP_KEYCODE_3: g.tool = Tool::Can; return;
            default: break;
        }
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
    for (auto* b : {&g_till, &g_plant, &g_water, &g_harvest, &g_sleep, &g_deny}) {
        if (b->data) { delete[] b->data; b->data = nullptr; }
    }
    g_pb.reset();
    if (g_audio.is_initialized()) { g_audio.shutdown(); }
    if (g_renderer) { g_renderer->shutdown(); g_renderer.reset(); }
    sg_shutdown();
}

}  // namespace

sapp_desc sokol_main(int /*argc*/, char* /*argv*/[]) {
    sapp_desc d{};
    d.init_cb = on_init; d.frame_cb = on_frame; d.cleanup_cb = on_cleanup; d.event_cb = on_event;
    d.width = 1024; d.height = 640; d.high_dpi = true;
    d.window_title = "OmniHarvest — a Stardew-like grid farming sim (no physics, pure 2D)";
    d.logger.func = slog_func;
    return d;
}
