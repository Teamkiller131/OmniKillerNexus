#define _CRT_SECURE_NO_WARNINGS  // std::getenv / snprintf

// OmniHarvest — a Stardew-Valley-like 2D TILE-GRID farming sim, now with a full
// RPG/sim layer on top of the base grid game:
//   • INVENTORY  — a 24-slot bag of stackable items (seeds, produce, resources,
//                  forage); harvesting deposits produce, planting consumes a seed.
//   • ECONOMY    — a town SHOP (buy seeds, sell produce) with a deterministic daily
//                  market; gold now flows through selling, not harvesting.
//   • SKILLS     — Farming / Foraging / Mining, XP-driven 0..10 with real perks
//                  (sell-price %, extra wood/stone) and level-up toasts.
//   • ACHIEVEMENTS — 12 milestones across every system, with unlock toasts.
//   • RELATIONSHIPS — 3 townsfolk with friendship hearts; talk + gift (preferences).
//   • SUPPORTING — energy/stamina, a day clock, foraging (chop trees) + mining
//                  (break rocks), a shared toast queue, and a one-panel-at-a-time UI.
//
// Still NO physics — the whole thing rides the 2D sprite stack (SpriteBatch +
// GpuSpriteRenderer + Camera2D + Image) + okn-audio. Data model: harvest_systems.hpp.
// Every player verb is a standalone function so the headless OKN_HARVEST_AUTODEMO
// can drive the entire loop (buy/plant/water/sleep/harvest/sell/chop/mine/talk/gift)
// without synthetic keys, asserting each system in harvest_result.txt.

#include "harvest_systems.hpp"

#include <okn/render/sprite2d/sprite_batch.hpp>
#include <okn/render/sprite2d/bitmap_text.hpp>
#include <okn/render/sprite2d/camera2d.hpp>
#include <okn/render/sprite2d/gpu_sprite_renderer.hpp>
#include <okn/render/sprite2d/image.hpp>

#include <okn/audio/backend/audio_engine.hpp>
#include <okn/audio/mixer/playback.hpp>
#include <okn/audio/decode/wav_decoder.hpp>

#include <okn/input/action_map.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

using namespace okh;
using okn::render::sprite2d::Camera2D;
using okn::render::sprite2d::GpuSpriteRenderer;
using okn::render::sprite2d::Image;
using okn::render::sprite2d::Sprite;
using okn::render::sprite2d::SpriteBatch;
using okn::math::Vec2;
using okn::math::u32;

namespace {

constexpr int kMapW = 40;
constexpr int kMapH = 30;
constexpr float kViewTiles = 16.0f;
constexpr float kMoveSpeed = 4.6f;     // tiles/sec — free (non-grid) walking
constexpr float kPlayerR = 0.34f;      // player collision half-extent (fits 1-wide gaps)
constexpr unsigned kTexFarmer = 1;
// fixed demo gather targets (left of the farm plot) so the autodemo is deterministic:
// a 3-tree grove at x=13 and a 3-rock cluster at x=12, on rows 12/14/16.
constexpr int kDemoTreeX = 13, kDemoRockX = 12, kDemoGather = 3;
constexpr int kDemoRows[kDemoGather] = {12, 14, 16};

enum class State { Playing, Win };

// ── input action-map (okn-input; held movement + discrete act/sleep) ──────────────
enum class Action { Up, Down, Left, Right, Act, Sleep, Count };
using InputMap = okn::input::ActionMap<Action>;

struct Game {
    std::array<Tile, kMapW * kMapH> tiles{};
    std::array<Crop, kMapW * kMapH> crops{};
    float pcx = 20.5f, pcy = 15.5f;   // continuous grid position (col, row); row increases downward
    int fx = 0, fy = 1;               // facing (cardinal) — the action/cursor direction
    bool facing_right = true;
    Tool tool = Tool::Hoe;
    int gold = kStartGold;
    int day = 1;
    int harvested = 0;
    State state = State::Playing;
    bool goal_reached = false;     // gold goal hit — a celebratory badge, NOT a freeze
    InputMap input;

    // systems
    Inventory inv;
    Skills skills;
    std::array<NpcState, static_cast<int>(NpcId::Count)> npcs{};
    std::vector<Toast> toasts;
    Panel panel = Panel::None;
    ShopTab shopTab = ShopTab::Buy;
    int shopCursor = 0;
    int shopTileX = -1, shopTileY = -1;
    int energy = kMaxEnergy;
    float clock_min = kDayStartMin;
    float clock_acc = 0.0f;
    int hit_tx = -1, hit_ty = -1, hits = 0;   // in-progress chop/mine on the faced tile

    // achievements + the counters their predicates read
    bool unlocked[static_cast<int>(AchId::Count)]{};
    int achCount = 0;
    int totalGoldEarned = 0;
    int treesChopped = 0;
    int rocksMined = 0;
    int maxSkillLevel = 0;
    int maxNpcHearts = 0;
    int itemsBought = 0;
    int nightsSlept = 0;
    bool everOwnedProduce[kProduceTypes]{};

    bool autodemo = false;
    int demo_step = 0;
    float demo_t = 0.0f;
    float trace_t = 0.0f;
    int sold = 0;
    float demo_freeX = 0.0f;          // autodemo: continuous X reached by free walking
    bool demo_collide_ok = false;     // autodemo: wall blocks the AABB, open ground doesn't

    Tile& at(int x, int y) { return tiles[static_cast<std::size_t>(y) * kMapW + x]; }
    Tile at(int x, int y) const { return tiles[static_cast<std::size_t>(y) * kMapW + x]; }
    Crop& crop(int x, int y) { return crops[static_cast<std::size_t>(y) * kMapW + x]; }
    const Crop& crop(int x, int y) const { return crops[static_cast<std::size_t>(y) * kMapW + x]; }
    NpcState& npc(NpcId id) { return npcs[static_cast<int>(id)]; }
    const NpcState& npc(NpcId id) const { return npcs[static_cast<int>(id)]; }
    NpcId npc_at(int x, int y) const {
        for (int i = 0; i < static_cast<int>(NpcId::Count); ++i) {
            if (kNpc[i].hx == x && kNpc[i].hy == y) { return static_cast<NpcId>(i); }
        }
        return NpcId::Count;
    }
    int ever_owned_count() const { int n = 0; for (bool b : everOwnedProduce) { if (b) { ++n; } } return n; }
};

Game g;
std::unique_ptr<GpuSpriteRenderer> g_renderer;
okn::audio::AudioEngine g_audio;
std::unique_ptr<okn::audio::AudioPlayback> g_pb;
okn::audio::AudioBuffer g_till{}, g_plant{}, g_water{}, g_harvest{}, g_sleep{}, g_deny{},
                        g_levelup{}, g_ach{}, g_talk{}, g_gift{};

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

// ── shared toast queue ─────────────────────────────────────────────────────────
void push_toast(const char* s, Rgba8 col = rgba(255, 244, 210)) {
    Toast t; t.life = kToastLife; t.col = col;
    int i = 0; for (; s[i] && i < 39; ++i) { t.text[i] = s[i]; } t.text[i] = '\0';
    if (g.toasts.size() >= kMaxToasts) { g.toasts.erase(g.toasts.begin()); }
    g.toasts.push_back(t);
}
void tick_toasts(float dt) {
    for (auto& t : g.toasts) { t.life -= dt; }
    g.toasts.erase(std::remove_if(g.toasts.begin(), g.toasts.end(),
                   [](const Toast& t) { return t.life <= 0.0f; }), g.toasts.end());
}

// ── skills ──────────────────────────────────────────────────────────────────────
void add_xp(Skill k, int n) {
    if (n <= 0) { return; }
    SkillState& s = g.skills[k];
    const int old = s.level;
    s.xp += n;
    s.level = skill_level_for_xp(s.xp);
    if (s.level > old) {
        g.maxSkillLevel = std::max(g.maxSkillLevel, s.level);
        char buf[40]; std::snprintf(buf, sizeof(buf), "%s LV %d", skill_name(k), s.level);
        push_toast(buf, rgba(120, 230, 140));
        play(g_levelup, 0.6f);
    }
}

// ── energy gate ───────────────────────────────────────────────────────────────
bool spend_energy(EAct a) {
    const int c = kEnergyCost[static_cast<int>(a)];
    if (g.energy < c) { push_toast("TOO TIRED SLEEP T", rgba(255, 150, 120)); play(g_deny, 0.3f); return false; }
    g.energy -= c;
    return true;
}

// ── grid + free-movement helpers ──────────────────────────────────────────────────
Vec2 tile_world(int tx, int ty) { return {static_cast<float>(tx) + 0.5f, -static_cast<float>(ty) - 0.5f}; }
Vec2 player_world() { return {g.pcx, -g.pcy}; }                 // continuous world position of the player
int cur_tx() { return static_cast<int>(std::floor(g.pcx)); }   // tile the player stands on
int cur_ty() { return static_cast<int>(std::floor(g.pcy)); }
int faced_tx() { return cur_tx() + g.fx; }                     // tile the player faces (action/cursor target)
int faced_ty() { return cur_ty() + g.fy; }
bool in_bounds(int x, int y) { return x >= 0 && y >= 0 && x < kMapW && y < kMapH; }
bool walkable(int x, int y) {
    if (!in_bounds(x, y)) { return false; }
    if (g.npc_at(x, y) != NpcId::Count) { return false; }      // NPCs are solid
    const Tile t = g.at(x, y);
    return t == Tile::Grass || t == Tile::Soil;                // water/tree/shop/rock block
}
// AABB-vs-tilemap: is the player box (half-extent kPlayerR) at (cx,cy) over any solid tile?
bool blocked_box(float cx, float cy) {
    const int x0 = static_cast<int>(std::floor(cx - kPlayerR)), x1 = static_cast<int>(std::floor(cx + kPlayerR));
    const int y0 = static_cast<int>(std::floor(cy - kPlayerR)), y1 = static_cast<int>(std::floor(cy + kPlayerR));
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) { if (!walkable(tx, ty)) { return true; } }
    }
    return false;
}
// Free movement: integrate a (col,row) direction with axis-separated collision (wall-slide).
void move_player(float dcol, float drow, float dt) {
    const float len = std::sqrt(dcol * dcol + drow * drow);
    if (len < 0.0001f) { return; }
    dcol /= len; drow /= len;
    if (std::fabs(dcol) >= std::fabs(drow)) { g.fx = dcol > 0 ? 1 : -1; g.fy = 0; }   // face the dominant axis
    else { g.fx = 0; g.fy = drow > 0 ? 1 : -1; }
    if (dcol > 0.01f) { g.facing_right = true; } else if (dcol < -0.01f) { g.facing_right = false; }
    const float step = kMoveSpeed * dt;
    const float nx = g.pcx + dcol * step;
    if (!blocked_box(nx, g.pcy)) { g.pcx = nx; }
    const float ny = g.pcy + drow * step;
    if (!blocked_box(g.pcx, ny)) { g.pcy = ny; }
    g.hit_tx = -1; g.hit_ty = -1; g.hits = 0;   // walking cancels an in-progress chop/mine
}

// ── per-tile farming verbs (callable by input AND the autodemo) ───────────────────
void till(int x, int y) {
    if (in_bounds(x, y) && g.at(x, y) == Tile::Grass) {
        if (!spend_energy(EAct::Till)) { return; }
        g.at(x, y) = Tile::Soil; play(g_till, 0.5f);
    }
}
bool plant(int x, int y) {                                     // plants the SELECTED held seed
    const Slot& h = g.inv.held();
    if (!in_bounds(x, y) || g.at(x, y) != Tile::Soil || g.crop(x, y).planted ||
        h.id == ItemId::None || item_def(h.id).cat != ItemCat::Seed) { play(g_deny, 0.3f); return false; }
    const ItemDef& sd = item_def(h.id);
    if (!g.inv.remove(h.id, 1)) { play(g_deny, 0.3f); return false; }
    Crop c; c.planted = true; c.produce = sd.produces; c.growDays = std::max(1, sd.growDays);
    g.crop(x, y) = c;
    play(g_plant, 0.5f);
    return true;
}
void water(int x, int y) {
    Crop& c = g.crop(x, y);
    if (in_bounds(x, y) && c.planted && !c.watered) {
        if (!spend_energy(EAct::Water)) { return; }
        c.watered = true; play(g_water, 0.4f);
    }
}
bool harvest(int x, int y) {
    Crop& c = g.crop(x, y);
    if (in_bounds(x, y) && c.planted && c.stage >= c.growDays) {
        if (!g.inv.has_space(c.produce)) { push_toast("BAG FULL", rgba(255, 150, 120)); play(g_deny, 0.3f); return false; }
        if (!spend_energy(EAct::Harvest)) { return false; }
        const ItemId out = c.produce;
        c = Crop{};
        g.inv.add(out, 1);                                      // produce -> bag (sell for gold)
        const int pi = produce_index(out);
        if (pi >= 0) { g.everOwnedProduce[pi] = true; }
        ++g.harvested;
        add_xp(Skill::Farming, kXpHarvest);
        play(g_harvest, 0.6f);
        return true;
    }
    return false;
}

// ── foraging / mining verbs ───────────────────────────────────────────────────
bool chop(int x, int y) {
    if (!in_bounds(x, y) || g.at(x, y) != Tile::Tree) { return false; }
    if (!g.inv.has_space(ItemId::Wood)) { push_toast("BAG FULL", rgba(255, 150, 120)); play(g_deny, 0.3f); return false; }
    if (!spend_energy(EAct::Chop)) { return false; }
    if (g.hit_tx != x || g.hit_ty != y) { g.hit_tx = x; g.hit_ty = y; g.hits = 0; }
    play(g_till, 0.5f);
    if (++g.hits >= kChopHits) {
        g.at(x, y) = Tile::Grass; g.crop(x, y) = Crop{};
        const int n = 1 + forage_extra_wood(g.skills[Skill::Foraging].level);
        g.inv.add(ItemId::Wood, n); ++g.treesChopped;
        g.hits = 0; g.hit_tx = -1; g.hit_ty = -1;
        char buf[24]; std::snprintf(buf, sizeof(buf), "+%d WOOD", n); push_toast(buf, rgba(180, 140, 90));
        add_xp(Skill::Foraging, kXpChop);
        return true;
    }
    return false;
}
bool mine(int x, int y) {
    if (!in_bounds(x, y) || g.at(x, y) != Tile::Rock) { return false; }
    if (!g.inv.has_space(ItemId::Stone)) { push_toast("BAG FULL", rgba(255, 150, 120)); play(g_deny, 0.3f); return false; }
    if (!spend_energy(EAct::Mine)) { return false; }
    if (g.hit_tx != x || g.hit_ty != y) { g.hit_tx = x; g.hit_ty = y; g.hits = 0; }
    play(g_water, 0.4f);
    if (++g.hits >= kMineHits) {
        g.at(x, y) = Tile::Grass;
        const int n = 1 + mine_extra_stone(g.skills[Skill::Mining].level);
        g.inv.add(ItemId::Stone, n); ++g.rocksMined;
        g.hits = 0; g.hit_tx = -1; g.hit_ty = -1;
        char buf[24]; std::snprintf(buf, sizeof(buf), "+%d STONE", n); push_toast(buf, rgba(170, 170, 185));
        add_xp(Skill::Mining, kXpMine);
        return true;
    }
    return false;
}

// ── economy / shop ──────────────────────────────────────────────────────────────
bool shop_adjacent() {
    return std::abs(cur_tx() - g.shopTileX) + std::abs(cur_ty() - g.shopTileY) <= 1;
}
void open_shop() { g.panel = Panel::Shop; g.shopTab = ShopTab::Buy; g.shopCursor = 0; }
bool buy(ItemId seed, int qty = 1) {
    const ItemDef& d = item_def(seed);
    if (d.cat != ItemCat::Seed || d.buyPrice <= 0) { return false; }
    if (g.gold < d.buyPrice * qty) { push_toast("NOT ENOUGH GOLD", rgba(255, 150, 120)); play(g_deny, 0.3f); return false; }
    const int got = qty - g.inv.add(seed, qty);                 // only what actually fit the bag
    if (got <= 0) { push_toast("BAG FULL", rgba(255, 150, 120)); play(g_deny, 0.3f); return false; }
    g.gold -= d.buyPrice * got; g.itemsBought += got;           // charge + credit for `got`, not `qty`
    play(g_plant, 0.5f);
    return true;
}
int sell(ItemId produce, int qty) {
    const int have = g.inv.count(produce);
    if (qty < 0 || qty > have) { qty = have; }
    if (qty <= 0 || item_def(produce).sellPrice <= 0) { play(g_deny, 0.3f); return 0; }
    const int unit = sell_price(produce, g.day, g.skills[Skill::Farming].level);
    const int gain = unit * qty;
    g.inv.remove(produce, qty); g.gold += gain; g.totalGoldEarned += gain; g.sold += qty;
    play(g_harvest, 0.6f);
    if (g.gold >= kGoldGoal && !g.goal_reached) {              // celebrate once; keep playing (Stardew-like)
        g.goal_reached = true; push_toast("GOAL REACHED FARM THRIVES", rgba(160, 240, 160));
    }
    return gain;
}
void sell_all() {
    for (int i = 0; i < kItemCount; ++i) {
        const ItemId id = static_cast<ItemId>(i);
        if (item_def(id).cat == ItemCat::Produce) { sell(id, -1); }
    }
}

// ── relationships ───────────────────────────────────────────────────────────────
void bump_max_hearts() {
    for (const auto& n : g.npcs) { g.maxNpcHearts = std::max(g.maxNpcHearts, hearts_of(n.friendship)); }
}
bool talk(NpcId id) {
    if (id == NpcId::Count) { return false; }
    NpcState& n = g.npc(id);
    if (n.talkedToday) { play(g_deny, 0.25f); return false; }
    n.talkedToday = true;
    n.friendship = std::min(kMaxFriend, n.friendship + kTalkPts);
    bump_max_hearts();
    char buf[40]; std::snprintf(buf, sizeof(buf), "%s +FRIEND", kNpc[static_cast<int>(id)].name);
    push_toast(buf, rgba(255, 200, 220));
    play(g_talk, 0.5f);
    return true;
}
bool give_gift(NpcId id) {
    if (id == NpcId::Count) { return false; }
    NpcState& n = g.npc(id);
    const Slot& h = g.inv.held();
    if (h.id == ItemId::None || n.giftedToday) { play(g_deny, 0.3f); return false; }
    const ItemId item = h.id;
    if (!g.inv.remove(item, 1)) { play(g_deny, 0.3f); return false; }
    n.giftedToday = true;
    const GiftTier t = gift_tier(id, item);
    n.friendship = std::clamp(n.friendship + kGiftPts[static_cast<int>(t)], 0, kMaxFriend);
    bump_max_hearts();
    static const char* kReact[4] = {"LOVES IT", "LIKES IT", "OK", "DISLIKES IT"};
    char buf[40]; std::snprintf(buf, sizeof(buf), "%s %s", kNpc[static_cast<int>(id)].name, kReact[static_cast<int>(t)]);
    push_toast(buf, t == GiftTier::Disliked ? rgba(255, 150, 120) : rgba(255, 210, 150));
    play(t == GiftTier::Disliked ? g_deny : g_gift, 0.5f);
    return true;
}

// ── achievements ─────────────────────────────────────────────────────────────────
struct AchDef { AchId id; const char* name; bool (*met)(const Game&); };
const AchDef kAch[static_cast<int>(AchId::Count)] = {
    {AchId::FirstHarvest, "FIRST HARVEST", [](const Game& s){ return s.harvested >= 1; }},
    {AchId::Cultivator,   "CULTIVATOR",    [](const Game& s){ return s.harvested >= 50; }},
    {AchId::Greenhorn,    "GREENHORN",     [](const Game& s){ return s.totalGoldEarned >= 500; }},
    {AchId::Forager,      "FORAGER",       [](const Game& s){ return s.treesChopped >= 20; }},
    {AchId::Prospector,   "PROSPECTOR",    [](const Game& s){ return s.rocksMined >= 20; }},
    {AchId::Skilled,      "SKILLED",       [](const Game& s){ return s.maxSkillLevel >= 5; }},
    {AchId::Master,       "MASTER",        [](const Game& s){ return s.maxSkillLevel >= 10; }},
    {AchId::Friendly,     "FRIENDLY",      [](const Game& s){ return s.maxNpcHearts >= 5; }},
    {AchId::Beloved,      "BELOVED",       [](const Game& s){ return s.maxNpcHearts >= 10; }},
    {AchId::Shopaholic,   "SHOPAHOLIC",    [](const Game& s){ return s.itemsBought >= 20; }},
    {AchId::Collector,    "COLLECTOR",     [](const Game& s){ return s.ever_owned_count() >= kProduceTypes; }},
    {AchId::WellRested,   "WELL RESTED",   [](const Game& s){ return s.nightsSlept >= 10; }},
};
const Rgba8 kAchColor[static_cast<int>(AchId::Count)] = {
    rgba(225, 90, 70), rgba(235,140, 60), rgba(255,215, 70), rgba(120,200, 90),
    rgba(150,120, 90), rgba( 90,200,170), rgba( 80,180,255), rgba(255,140,170),
    rgba(235, 90,160), rgba(190,150,255), rgba(120,230,120), rgba(160,180,210),
};
void check_achievements() {
    for (int i = 0; i < static_cast<int>(AchId::Count); ++i) {
        if (!g.unlocked[i] && kAch[i].met(g)) {
            g.unlocked[i] = true; ++g.achCount;
            char buf[48]; std::snprintf(buf, sizeof(buf), "ACHIEVEMENT %s", kAch[i].name);
            push_toast(buf, kAchColor[i]);
            play(g_ach, 0.6f);
        }
    }
}

// ── dispatch verbs ────────────────────────────────────────────────────────────
void do_action() {
    if (g.panel != Panel::None) { return; }
    const int tx = faced_tx(), ty = faced_ty();
    if (g.npc_at(tx, ty) != NpcId::Count) { talk(g.npc_at(tx, ty)); return; }
    if (g.at(tx, ty) == Tile::Shop) { open_shop(); return; }
    if (harvest(tx, ty)) { return; }
    switch (g.tool) {
        case Tool::Hoe:  till(tx, ty);  break;
        case Tool::Seed: plant(tx, ty); break;
        case Tool::Can:  water(tx, ty); break;
        case Tool::Axe:  chop(tx, ty);  break;
        case Tool::Pick: mine(tx, ty);  break;
        default: break;
    }
}
void do_sleep() {
    ++g.day; ++g.nightsSlept;
    for (auto& c : g.crops) {
        if (c.planted && c.watered) { c.stage = std::min(c.growDays, c.stage + 1); }
        c.watered = false;
    }
    for (auto& n : g.npcs) { n.talkedToday = false; n.giftedToday = false; }
    g.energy = kMaxEnergy; g.clock_min = kDayStartMin; g.clock_acc = 0.0f;
    g.hit_tx = -1; g.hit_ty = -1; g.hits = 0;
    push_toast("DAY STARTS RESTED", rgba(180, 220, 255));
    play(g_sleep, 0.5f);
}

// ── world generation ──────────────────────────────────────────────────────────
void build_world() {
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            Tile t = Tile::Grass;
            const bool in_town = (x >= 17 && x <= 31 && y >= 4 && y <= 9);   // plaza (forced clear)
            const bool in_plot = (x >= 14 && x <= 30 && y >= 10 && y <= 22);
            if (x == 0 || y == 0 || x == kMapW - 1 || y == kMapH - 1) { t = Tile::Tree; }
            else if (x >= 5 && x <= 11 && y >= 4 && y <= 8) { t = Tile::Water; }
            else if (in_town || in_plot) { t = Tile::Grass; }
            else {
                const bool seam = (x >= 3 && x <= 9 && y >= 23 && y <= 27);
                if (seam && (hash2(x, y) % 100u) < 55u) { t = Tile::Rock; }
                else if (hash2(x, y) % 100u < 9u) { t = Tile::Tree; }
                else if (hash2(x + 7, y) % 100u < 4u) { t = Tile::Rock; }
            }
            g.at(x, y) = t;
            g.crop(x, y) = Crop{};
        }
    }
    g.at(18, 6) = Tile::Shop; g.shopTileX = 18; g.shopTileY = 6;          // town shop
    for (int r = 0; r < kDemoGather; ++r) {                               // grove + rock cluster by the farm
        g.at(kDemoTreeX, kDemoRows[r]) = Tile::Tree;
        g.at(kDemoRockX, kDemoRows[r]) = Tile::Rock;
    }
    g.pcx = 20.5f; g.pcy = 15.5f; g.fx = 0; g.fy = 1;
}

void reset_game() {
    g.gold = kStartGold; g.day = 1; g.harvested = 0; g.sold = 0;
    g.state = State::Playing; g.goal_reached = false; g.tool = Tool::Hoe; g.facing_right = true;
    g.input.clear_state();                              // a held key must not survive R and auto-walk
    if (g.autodemo) { std::ofstream("harvest_trace.txt", std::ios::trunc); }   // fresh trace per run
    build_world();
    g.inv = Inventory{};
    g.inv.add(ItemId::ParsnipSeed, 15);
    g.inv.add(ItemId::PotatoSeed, 5);
    g.inv.add(ItemId::Wood, 3);
    g.inv.add(ItemId::Berry, 2);
    g.inv.add(ItemId::Flower, 2);
    g.inv.sel = 0;
    g.skills = Skills{};
    for (auto& n : g.npcs) { n = NpcState{}; }
    g.toasts.clear(); g.panel = Panel::None; g.shopTab = ShopTab::Buy; g.shopCursor = 0;
    g.energy = kMaxEnergy; g.clock_min = kDayStartMin; g.clock_acc = 0.0f;
    g.hit_tx = -1; g.hit_ty = -1; g.hits = 0;
    for (bool& u : g.unlocked) { u = false; }
    g.achCount = 0; g.totalGoldEarned = 0; g.treesChopped = 0; g.rocksMined = 0;
    g.maxSkillLevel = 0; g.maxNpcHearts = 0; g.itemsBought = 0; g.nightsSlept = 0;
    for (bool& b : g.everOwnedProduce) { b = false; }
    g.demo_step = 0; g.demo_t = 0.0f; g.trace_t = 0.0f;
}

// ── scripted, keyboard-free autodemo: drives the full RPG loop ─────────────────
void write_result() {
    if (std::getenv("OKN_HARVEST_ACHDEMO")) {           // force every counter to prove all 12 unlock
        g.harvested = 50; g.totalGoldEarned = 500; g.treesChopped = 20; g.rocksMined = 20;
        g.maxSkillLevel = 10; g.maxNpcHearts = 10; g.itemsBought = 20;
        for (bool& b : g.everOwnedProduce) { b = true; } g.nightsSlept = 10;
        check_achievements();
    }
    const NpcState& m = g.npc(NpcId::Mara);
    const int fLv = g.skills[Skill::Farming].level, gLv = g.skills[Skill::Foraging].level, mLv = g.skills[Skill::Mining].level;
    const bool win = g.harvested == 5 && g.gold > kStartGold && g.inv.count(ItemId::Wood) >= 3 &&
                     g.inv.count(ItemId::Stone) >= 3 && fLv >= 2 && gLv >= 1 && mLv >= 1 &&
                     g.itemsBought >= 1 && m.friendship >= 58 && hearts_of(m.friendship) >= 2 && g.achCount >= 1 &&
                     g.demo_freeX > 21.0f && g.demo_collide_ok;   // free movement + wall collision both proven
    std::ofstream rf("harvest_result.txt");
    rf << (win ? "OKHARVEST WIN" : "OKHARVEST FAIL")
       << " freeX=" << g.demo_freeX << " collide=" << (g.demo_collide_ok ? 1 : 0)
       << " gold=" << g.gold << " day=" << g.day << " harvested=" << g.harvested
       << " sold=" << g.sold << " wood=" << g.inv.count(ItemId::Wood)
       << " stone=" << g.inv.count(ItemId::Stone) << " energy=" << g.energy
       << " fLv=" << fLv << " gLv=" << gLv << " gXp=" << g.skills[Skill::Foraging].xp
       << " mLv=" << mLv << " mXp=" << g.skills[Skill::Mining].xp << " bought=" << g.itemsBought
       << " mara=" << m.friendship << " hearts=" << hearts_of(m.friendship)
       << " ach=" << g.achCount << "/" << static_cast<int>(AchId::Count);
}
void run_autodemo() {
    constexpr float kStepDt = 0.18f;
    g.demo_t += static_cast<float>(sapp_frame_duration());
    if (g.demo_t < kStepDt) { return; }
    g.demo_t = 0.0f;
    const int s = g.demo_step++;
    const int px0 = 18, py = 15, n = 5;                 // 5-wide plot on the player's row
    const int chopSteps = kDemoGather * kChopHits, gatherSteps = chopSteps + kDemoGather * kMineHits;
    // phase offsets
    const int Move = 4, Till = Move + n, Plant = Till + n, Gather = Plant + gatherSteps;
    const int Cycle = Gather + kRipe * (n + 1), Harv = Cycle + n, Shop = Harv + 6, Rel = Shop + 5;

    if (s < Move) {                                     // FREE movement + AABB-collision proof
        move_player(1.0f, 0.0f, 0.12f);                 // walk RIGHT continuously over open grass
        if (s == Move - 1) {
            g.demo_freeX = g.pcx;                       // a continuous (non-grid) X was reached
            g.demo_collide_ok =                         // a tree blocks the player box; open ground does not
                blocked_box(static_cast<float>(kDemoTreeX) + 0.5f, static_cast<float>(kDemoRows[1]) + 0.5f) &&
                !blocked_box(20.5f, 15.5f);
            g.pcx = 20.5f; g.pcy = 15.5f;               // recenter for the coord-driven rest of the demo
        }
    }
    else if (s < Till) { g.tool = Tool::Hoe; g.inv.sel = 0; till(px0 + (s - Move), py); }
    else if (s < Plant) { g.tool = Tool::Seed; plant(px0 + (s - Till), py); }
    else if (s < Gather) {
        const int k = s - Plant;                        // fell each of the 3 trees, then each of the 3 rocks
        if (k < chopSteps) { g.tool = Tool::Axe; chop(kDemoTreeX, kDemoRows[k / kChopHits]); }
        else { g.tool = Tool::Pick; const int rk = k - chopSteps; mine(kDemoRockX, kDemoRows[rk / kMineHits]); }
    } else if (s < Cycle) {
        const int c = s - Gather, within = c % (n + 1);
        if (within < n) { g.tool = Tool::Can; water(px0 + within, py); }
        else { do_sleep(); }
    } else if (s < Harv) { harvest(px0 + (s - Cycle), py); }
    else if (s < Shop) {
        const int k = s - Harv;
        if (k == 0) { open_shop(); }
        else if (k == 1) { buy(ItemId::PotatoSeed, 1); }
        else if (k == 2) { g.shopTab = ShopTab::Sell; }
        else if (k == 3) { sell_all(); }
        else { g.panel = Panel::None; }
    } else if (s < Rel) {
        const int k = s - Shop;
        if (k == 0) { talk(NpcId::Mara); }                          // +8
        else if (k == 1) { g.inv.select(ItemId::Flower); give_gift(NpcId::Mara); }  // loved +50 -> 58
        else if (k == 2) { talk(NpcId::Mara); }                     // gated (no change)
        else if (k == 3) { do_sleep(); }                            // reset daily flags
        else { talk(NpcId::Mara); }                                 // +8 -> 66
    } else {
        write_result();
        // screenshot hook: after the demo, open a panel / move to town to capture UI with
        // real accumulated state (skills leveled, friendship earned, bag full).
        if (const char* show = std::getenv("OKN_HARVEST_SHOW")) {
            g.state = State::Playing; g.toasts.clear();   // clean frame for the capture
            if (!std::strcmp(show, "inv")) { g.panel = Panel::Inventory; }
            else if (!std::strcmp(show, "skills")) { g.panel = Panel::Skills; }
            else if (!std::strcmp(show, "ach")) { g.panel = Panel::Achievements; }
            else if (!std::strcmp(show, "rel")) { g.panel = Panel::Relationships; }
            else if (!std::strcmp(show, "shop")) { open_shop(); }
            else if (!std::strcmp(show, "town")) { g.pcx = 23.5f; g.pcy = 9.5f; g.fx = 0; g.fy = -1; }
            else if (!std::strcmp(show, "move")) { g.pcx = 12.34f; g.pcy = 6.5f; g.fx = -1; g.fy = 0; }  // off-grid, against the pond
        }
        g.autodemo = false;
    }
}

void update(float dt) {
    // clock + toasts always advance
    g.clock_acc += dt;
    while (g.clock_acc >= kRealSecPerGameMin && g.clock_min < kDayEndMin) {
        g.clock_acc -= kRealSecPerGameMin; g.clock_min += 1.0f;
    }
    tick_toasts(dt);

    // FREE movement: WASD/arrows set a direction; move_player integrates it with
    // axis-separated AABB collision (walk anywhere, slide along walls).
    if (g.autodemo) { run_autodemo(); }
    else if (g.state == State::Playing && g.panel == Panel::None) {
        float dcol = 0.0f, drow = 0.0f;
        if (g.input.held(Action::Up))    { drow -= 1.0f; }
        if (g.input.held(Action::Down))  { drow += 1.0f; }
        if (g.input.held(Action::Left))  { dcol -= 1.0f; }
        if (g.input.held(Action::Right)) { dcol += 1.0f; }
        move_player(dcol, drow, dt);
    }
    if (!g.autodemo && g.state == State::Playing && g.panel == Panel::None) {
        if (g.input.just(Action::Act))   { do_action(); }
        if (g.input.just(Action::Sleep)) { do_sleep(); }
    }

    check_achievements();   // once/frame safety net (12 int compares)

    if (g.autodemo) {
        g.trace_t += dt;
        if (g.trace_t >= 0.5f) {
            g.trace_t = 0.0f;
            std::ofstream f("harvest_trace.txt", std::ios::app);
            if (f) {
                const NpcState& m = g.npc(NpcId::Mara);
                f << "pcx=" << g.pcx << " pcy=" << g.pcy << " gold=" << g.gold << " day=" << g.day
                  << " tool=" << static_cast<int>(g.tool) << " energy=" << g.energy
                  << " clock=" << static_cast<int>(g.clock_min) << " harvested=" << g.harvested
                  << " wood=" << g.inv.count(ItemId::Wood) << " stone=" << g.inv.count(ItemId::Stone)
                  << " parsnip=" << g.inv.count(ItemId::Parsnip)
                  << " seeds=" << g.inv.count(ItemId::ParsnipSeed)
                  << " fLv=" << g.skills[Skill::Farming].level
                  << " gLv=" << g.skills[Skill::Foraging].level
                  << " mLv=" << g.skills[Skill::Mining].level
                  << " mara=" << m.friendship << " hearts=" << hearts_of(m.friendship)
                  << " ach=" << g.achCount << " nights=" << g.nightsSlept
                  << " toasts=" << g.toasts.size() << " panel=" << static_cast<int>(g.panel)
                  << " state=" << static_cast<int>(g.state) << "\n";
            }
        }
    }
}

// ── render ─────────────────────────────────────────────────────────────────────
void quad(SpriteBatch& b, Vec2 c, float w, float h, Rgba8 col, unsigned tex = 0, bool flip = false) {
    Sprite s; s.position = c; s.size = {flip ? -w : w, h}; s.color = col; s.texture_id = tex; b.add(s);
}
// Text/number HUD glyphs come from the engine's bitmap font (okn-render bitmap_text.hpp:
// draw_text / draw_num via ADL) — the A-Z table this game authored now lives there.
void draw_tool_icon(SpriteBatch& b, Tool t, Vec2 c, float sz) {
    switch (t) {
        case Tool::Hoe:
            quad(b, {c.x - sz * 0.1f, c.y}, sz * 0.12f, sz * 0.7f, rgba(140, 90, 50));
            quad(b, {c.x + sz * 0.12f, c.y + sz * 0.28f}, sz * 0.42f, sz * 0.14f, rgba(180, 180, 190)); break;
        case Tool::Seed:
            quad(b, c, sz * 0.5f, sz * 0.5f, rgba(150, 110, 60));
            quad(b, {c.x, c.y + sz * 0.3f}, sz * 0.28f, sz * 0.16f, rgba(110, 80, 40)); break;
        case Tool::Can:
            quad(b, c, sz * 0.5f, sz * 0.45f, rgba(120, 150, 180));
            quad(b, {c.x + sz * 0.36f, c.y + sz * 0.08f}, sz * 0.3f, sz * 0.12f, rgba(120, 150, 180)); break;
        case Tool::Axe:
            quad(b, {c.x - sz * 0.05f, c.y}, sz * 0.12f, sz * 0.7f, rgba(150, 100, 60));
            quad(b, {c.x + sz * 0.18f, c.y + sz * 0.22f}, sz * 0.3f, sz * 0.3f, rgba(170, 175, 185)); break;
        case Tool::Pick:
            quad(b, {c.x, c.y}, sz * 0.12f, sz * 0.7f, rgba(150, 100, 60));
            quad(b, {c.x, c.y + sz * 0.3f}, sz * 0.62f, sz * 0.12f, rgba(150, 155, 165)); break;
        default: break;
    }
}
void draw_item_icon(SpriteBatch& b, ItemId id, Vec2 c, float sz) {
    const IconSpec& ic = item_def(id).icon;
    for (int i = 0; i < ic.n; ++i) { const IconQuad& q = ic.q[i]; quad(b, {c.x + q.ox * sz, c.y + q.oy * sz}, q.w * sz, q.h * sz, q.col); }
}
// scrim + centered card + title; returns the card center for the caller to lay out rows.
Vec2 begin_panel(SpriteBatch& hud, float vw, float vh, const char* title, float pw, float ph) {
    quad(hud, {vw * 0.5f, vh * 0.5f}, vw, vh, rgba(8, 10, 14, 150));            // dim world
    const Vec2 ctr{vw * 0.5f, vh * 0.5f};
    quad(hud, ctr, pw + 0.5f, ph + 0.5f, rgba(20, 16, 12, 245));
    quad(hud, ctr, pw, ph, rgba(46, 38, 28, 250));
    quad(hud, {ctr.x, ctr.y + ph * 0.5f - 0.55f}, pw, 0.9f, rgba(64, 52, 38, 250));  // title bar
    draw_text(hud, title, ctr.x - static_cast<float>(std::strlen(title)) * 0.5f * (4.0f * 0.22f),
              ctr.y + ph * 0.5f - 0.3f, 0.22f, rgba(250, 232, 180));
    return ctr;
}

void render() {
    SpriteBatch world, hud;
    const float w = sapp_widthf(), h = sapp_heightf();
    const float vh = kViewTiles, vw = vh * (w / h);
    const Vec2 pw_ = player_world();
    const float hw = vw * 0.5f, hh = vh * 0.5f;

    Camera2D wc;
    // Order the clamp bounds: if the viewport is wider/taller than the map (e.g. a window
    // stretched past 2.5:1), lo would exceed hi and std::clamp is UB — min/max centers the map.
    const float loX = std::min(hw, static_cast<float>(kMapW) - hw), hiX = std::max(hw, static_cast<float>(kMapW) - hw);
    const float loY = std::min(-static_cast<float>(kMapH) + hh, -hh), hiY = std::max(-static_cast<float>(kMapH) + hh, -hh);
    wc.center = {std::clamp(pw_.x, loX, hiX), std::clamp(pw_.y, loY, hiY)};
    wc.viewport_h = vh; wc.viewport_w = vw;

    const int x0 = std::max(0, static_cast<int>(wc.center.x - hw) - 1);
    const int x1 = std::min(kMapW - 1, static_cast<int>(wc.center.x + hw) + 1);
    const int y0 = std::max(0, static_cast<int>(-wc.center.y - hh) - 1);
    const int y1 = std::min(kMapH - 1, static_cast<int>(-wc.center.y + hh) + 1);

    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            const Vec2 c = tile_world(tx, ty);
            const bool chk = ((tx + ty) & 1) != 0;
            switch (g.at(tx, ty)) {
                case Tile::Grass: quad(world, c, 1.0f, 1.0f, chk ? rgba(122, 192, 98) : rgba(112, 180, 90)); break;
                case Tile::Water: quad(world, c, 1.0f, 1.0f, chk ? rgba(74, 134, 204) : rgba(66, 122, 192)); break;
                case Tile::Soil: {
                    const Crop& cr = g.crop(tx, ty);
                    const bool wet = cr.planted && cr.watered;
                    quad(world, c, 1.0f, 1.0f, wet ? rgba(92, 60, 38) : rgba(146, 102, 64));
                    quad(world, c, 0.92f, 0.92f, wet ? rgba(104, 68, 42) : rgba(160, 114, 72));
                    break;
                }
                case Tile::Tree:
                    quad(world, c, 1.0f, 1.0f, chk ? rgba(122, 192, 98) : rgba(112, 180, 90));
                    quad(world, {c.x, c.y - 0.18f}, 0.22f, 0.5f, rgba(96, 62, 36));
                    quad(world, {c.x, c.y + 0.18f}, 0.9f, 0.78f, rgba(54, 132, 70));
                    quad(world, {c.x, c.y + 0.30f}, 0.62f, 0.5f, rgba(66, 152, 84));
                    break;
                case Tile::Rock:
                    quad(world, c, 1.0f, 1.0f, chk ? rgba(122, 192, 98) : rgba(112, 180, 90));
                    quad(world, {c.x, c.y - 0.05f}, 0.7f, 0.6f, rgba(120, 120, 132));
                    quad(world, {c.x - 0.1f, c.y + 0.05f}, 0.4f, 0.32f, rgba(150, 150, 162));
                    break;
                case Tile::Shop:
                    quad(world, c, 1.0f, 1.0f, rgba(112, 180, 90));
                    quad(world, {c.x, c.y - 0.05f}, 0.92f, 0.78f, rgba(150, 90, 70));
                    quad(world, {c.x, c.y + 0.42f}, 1.04f, 0.34f, rgba(110, 60, 45));
                    quad(world, {c.x, c.y - 0.2f}, 0.3f, 0.4f, rgba(70, 45, 35));
                    quad(world, {c.x, c.y + 0.5f}, 0.3f, 0.22f, rgba(245, 220, 120));
                    break;
            }
            const Crop& cr = g.crop(tx, ty);
            if (cr.planted) {
                const float ripef = static_cast<float>(cr.stage) / static_cast<float>(std::max(1, cr.growDays));
                const float gscale = 0.18f + 0.42f * ripef;
                quad(world, {c.x, c.y - 0.18f}, 0.12f, gscale, rgba(60, 140, 70));
                quad(world, {c.x, c.y - 0.18f + gscale * 0.5f}, gscale * 0.9f, gscale * 0.7f,
                     cr.stage >= cr.growDays ? rgba(70, 165, 80) : rgba(86, 178, 96));
                if (cr.stage >= cr.growDays) {
                    const Rgba8 fc = item_def(cr.produce).icon.n > 0 ? item_def(cr.produce).icon.q[0].col : rgba(225, 90, 70);
                    quad(world, {c.x, c.y + 0.06f}, 0.28f, 0.28f, fc);
                }
            }
        }
    }

    // NPCs (world space) + floating hearts when the player is near
    for (int i = 0; i < static_cast<int>(NpcId::Count); ++i) {
        const NpcDef& d = kNpc[i];
        if (d.hx < x0 || d.hx > x1 || d.hy < y0 || d.hy > y1) { continue; }
        const Vec2 c = tile_world(d.hx, d.hy);
        quad(world, {c.x, c.y - 0.10f}, 0.46f, 0.62f, d.body);
        quad(world, {c.x, c.y + 0.26f}, 0.34f, 0.34f, rgba(245, 200, 150));
        quad(world, {c.x, c.y + 0.38f}, 0.40f, 0.18f, d.hair);
        quad(world, {c.x - 0.07f, c.y + 0.27f}, 0.05f, 0.05f, rgba(30, 30, 40));
        quad(world, {c.x + 0.07f, c.y + 0.27f}, 0.05f, 0.05f, rgba(30, 30, 40));
        if (std::max(std::abs(d.hx - cur_tx()), std::abs(d.hy - cur_ty())) <= 2) {
            const int hearts = hearts_of(g.npcs[i].friendship);
            for (int hp = 0; hp < 10; ++hp) {
                const int row = hp / 5, col = hp % 5;
                const float px = c.x - 0.4f + col * 0.2f, py = c.y + 0.72f - row * 0.2f;
                quad(world, {px, py}, 0.16f, 0.16f, hp < hearts ? rgba(225, 70, 90) : rgba(70, 55, 55, 180));
            }
        }
    }

    // tile cursor + player
    {
        const Vec2 fc = tile_world(faced_tx(), faced_ty());
        const Rgba8 cur = rgba(255, 255, 255, 110);
        quad(world, {fc.x, fc.y + 0.46f}, 1.0f, 0.08f, cur);
        quad(world, {fc.x, fc.y - 0.46f}, 1.0f, 0.08f, cur);
        quad(world, {fc.x - 0.46f, fc.y}, 0.08f, 1.0f, cur);
        quad(world, {fc.x + 0.46f, fc.y}, 0.08f, 1.0f, cur);
    }
    quad(world, {pw_.x, pw_.y + 0.18f}, 0.8f, 1.0f, rgba(255, 255, 255), kTexFarmer, !g.facing_right);

    // ── HUD ──
    Camera2D hc; hc.center = {vw * 0.5f, vh * 0.5f}; hc.viewport_h = vh; hc.viewport_w = vw;
    const float top = vh - 0.5f;
    quad(hud, {vw * 0.5f, top}, vw, 1.0f, rgba(40, 32, 24, 210));
    quad(hud, {0.8f, top}, 0.5f, 0.5f, rgba(255, 215, 70));                 // gold
    draw_num(hud, g.gold, 1.2f, top + 0.25f, 0.18f, rgba(255, 245, 150));
    quad(hud, {vw * 0.5f - 1.6f, top}, 0.45f, 0.45f, rgba(255, 220, 120));  // day
    draw_text(hud, "DAY", vw * 0.5f - 1.35f, top + 0.2f, 0.13f, rgba(230, 230, 230));
    draw_num(hud, g.day, vw * 0.5f - 0.2f, top + 0.25f, 0.18f, rgba(255, 255, 255));
    {                                                                       // clock
        const float cx = vw * 0.5f + 1.3f;
        quad(hud, {cx, top}, 0.42f, 0.42f, rgba(120, 150, 210));
        draw_num(hud, clock_hh(g.clock_min), cx + 0.35f, top + 0.2f, 0.15f,
                 is_late(g.clock_min) ? rgba(255, 140, 120) : rgba(255, 255, 255));
        quad(hud, {cx + 0.95f, top}, 0.05f, 0.2f, rgba(230, 230, 230));
        const int mm = clock_mm(g.clock_min);                          // zero-padded to 2 digits
        draw_num(hud, mm / 10, cx + 1.1f, top + 0.2f, 0.15f, rgba(220, 220, 230));
        draw_num(hud, mm % 10, cx + 1.62f, top + 0.2f, 0.15f, rgba(220, 220, 230));
    }
    quad(hud, {vw - 2.6f, top}, 0.42f, 0.42f, rgba(255, 215, 70));          // achievement badge
    draw_num(hud, g.achCount, vw - 2.3f, top + 0.22f, 0.16f, rgba(255, 245, 150));
    quad(hud, {vw - 1.75f, top}, 0.1f, 0.04f, rgba(220, 220, 220));
    draw_num(hud, static_cast<int>(AchId::Count), vw - 1.6f, top + 0.22f, 0.16f, rgba(200, 210, 200));

    // energy bar (bottom-left)
    {
        const float bx = 2.6f, by = 1.5f, bw = 4.0f, bh = 0.42f;
        quad(hud, {bx, by}, bw + 0.12f, bh + 0.12f, rgba(20, 16, 12, 230));
        quad(hud, {bx, by}, bw, bh, rgba(50, 42, 34, 255));
        const float frac = std::clamp(static_cast<float>(g.energy) / static_cast<float>(kMaxEnergy), 0.0f, 1.0f);
        const Rgba8 ec = g.energy <= kLowEnergy ? rgba(220, 70, 60) : rgba(90, 200, 110);
        quad(hud, {bx - bw * 0.5f + bw * frac * 0.5f, by}, bw * frac, bh * 0.8f, ec);
        quad(hud, {bx - bw * 0.5f - 0.35f, by}, 0.4f, 0.5f, rgba(250, 220, 90));   // energy icon
        draw_num(hud, g.energy, bx - 0.4f, by + 0.16f, 0.12f, rgba(235, 235, 235));
    }

    // tool hotbar (5 slots, bottom-center) + held item indicator
    const float slot = 1.1f, bx = vw * 0.5f - 2.0f * slot, by = 0.85f;
    for (int i = 0; i < static_cast<int>(Tool::Count); ++i) {
        const Vec2 sc{bx + slot * static_cast<float>(i), by};
        const bool sel = static_cast<int>(g.tool) == i;
        quad(hud, sc, slot * 0.92f, slot * 0.92f, sel ? rgba(250, 230, 120) : rgba(60, 50, 40, 220));
        quad(hud, sc, slot * 0.74f, slot * 0.74f, rgba(35, 28, 22, 230));
        draw_tool_icon(hud, static_cast<Tool>(i), sc, slot * 0.7f);
        draw_num(hud, i + 1, sc.x - 0.5f, sc.y + 0.5f, 0.1f, rgba(200, 200, 200));
    }
    {                                                                       // held slot (bottom-right)
        const Vec2 hcc{vw - 1.0f, 1.0f};
        const Slot& hs = g.inv.held();
        quad(hud, hcc, 1.05f, 1.05f, rgba(250, 230, 120));
        quad(hud, hcc, 0.86f, 0.86f, rgba(35, 28, 22, 235));
        if (hs.id != ItemId::None) {
            draw_item_icon(hud, hs.id, hcc, 0.9f);
            draw_num(hud, hs.count, hcc.x + 0.16f, hcc.y - 0.28f, 0.14f, rgba(255, 255, 255));
        }
    }

    // ── modal panels ──
    if (g.panel == Panel::Inventory) {
        const Vec2 ctr = begin_panel(hud, vw, vh, "INVENTORY", 10.5f, 8.0f);
        const int cols = 6, rows = 4; const float cell = 1.5f;
        const float ox = ctr.x - (cols - 1) * 0.5f * cell, oy = ctr.y + (rows - 1) * 0.5f * cell - 0.4f;
        for (int i = 0; i < kInvSlots; ++i) {
            const Vec2 sc{ox + (i % cols) * cell, oy - (i / cols) * cell};
            const bool sel = i == g.inv.sel;
            quad(hud, sc, cell * 0.92f, cell * 0.92f, sel ? rgba(250, 230, 120) : rgba(60, 50, 40, 235));
            quad(hud, sc, cell * 0.78f, cell * 0.78f, rgba(30, 24, 18, 240));
            const Slot& s = g.inv.slots[i];
            if (s.id != ItemId::None) {
                draw_item_icon(hud, s.id, sc, cell * 0.8f);
                draw_num(hud, s.count, sc.x + 0.05f, sc.y - 0.4f, 0.13f, rgba(255, 255, 255));
            }
        }
        const Slot& hs = g.inv.held();
        draw_text(hud, hs.id == ItemId::None ? "EMPTY" : item_def(hs.id).name,
                  ctr.x - 2.0f, ctr.y - 3.5f, 0.2f, rgba(250, 232, 180));
    } else if (g.panel == Panel::Skills) {
        const Vec2 ctr = begin_panel(hud, vw, vh, "SKILLS", 10.0f, 6.5f);
        for (int i = 0; i < static_cast<int>(Skill::Count); ++i) {
            const float ry = ctr.y + 1.4f - i * 1.5f;
            const SkillState& s = g.skills.s[i];
            draw_text(hud, skill_name(static_cast<Skill>(i)), ctr.x - 4.4f, ry + 0.3f, 0.14f, rgba(230, 230, 230));
            draw_text(hud, "LV", ctr.x + 0.5f, ry + 0.3f, 0.14f, rgba(190, 190, 190));
            draw_num(hud, s.level, ctr.x + 1.8f, ry + 0.3f, 0.18f, rgba(255, 245, 150));
            const int lo = kXpToLevel[s.level], hi = (s.level < kMaxSkillLevel) ? kXpToLevel[s.level + 1] : lo;
            const float frac = (s.level >= kMaxSkillLevel) ? 1.0f : (hi > lo ? static_cast<float>(s.xp - lo) / static_cast<float>(hi - lo) : 0.0f);
            quad(hud, {ctr.x + 3.4f, ry}, 1.7f, 0.3f, rgba(30, 26, 20, 235));
            if (frac > 0.01f) { quad(hud, {ctr.x + 3.4f - 0.8f + 1.6f * frac * 0.5f, ry}, 1.6f * frac, 0.24f, rgba(120, 200, 120)); }
        }
    } else if (g.panel == Panel::Achievements) {
        const Vec2 ctr = begin_panel(hud, vw, vh, "ACHIEVEMENTS", 11.0f, 13.5f);
        for (int i = 0; i < static_cast<int>(AchId::Count); ++i) {
            const float ry = ctr.y + 5.4f - i * 1.0f;
            const bool got = g.unlocked[i];
            quad(hud, {ctr.x - 4.8f, ry}, 0.5f, 0.5f, got ? rgba(60, 160, 80) : rgba(70, 64, 58));
            if (got) {
                quad(hud, {ctr.x - 4.86f, ry - 0.08f}, 0.1f, 0.22f, rgba(235, 255, 235));
                quad(hud, {ctr.x - 4.7f, ry + 0.02f}, 0.1f, 0.34f, rgba(235, 255, 235));
            }
            const Rgba8 nc = got ? kAchColor[i] : rgba(kAchColor[i].r, kAchColor[i].g, kAchColor[i].b, 90);
            draw_text(hud, kAch[i].name, ctr.x - 4.2f, ry + 0.28f, 0.18f, got ? nc : rgba(120, 116, 110));
        }
    } else if (g.panel == Panel::Relationships) {
        const Vec2 ctr = begin_panel(hud, vw, vh, "TOWNSFOLK", 9.0f, 6.0f);
        for (int i = 0; i < static_cast<int>(NpcId::Count); ++i) {
            const float ry = ctr.y + 1.4f - i * 1.4f;
            const NpcDef& d = kNpc[i];
            quad(hud, {ctr.x - 3.6f, ry}, 0.6f, 0.6f, d.body);
            quad(hud, {ctr.x - 3.6f, ry + 0.22f}, 0.5f, 0.18f, d.hair);
            draw_text(hud, d.name, ctr.x - 2.9f, ry + 0.3f, 0.18f, rgba(235, 235, 235));
            const int hearts = hearts_of(g.npcs[i].friendship);
            for (int hpi = 0; hpi < 10; ++hpi) {
                quad(hud, {ctr.x + 0.6f + hpi * 0.32f, ry}, 0.26f, 0.26f,
                     hpi < hearts ? rgba(225, 70, 90) : rgba(70, 55, 55, 200));
            }
        }
    } else if (g.panel == Panel::Shop) {
        const Vec2 ctr = begin_panel(hud, vw, vh, "SHOP", 11.0f, 9.0f);
        const bool buyT = g.shopTab == ShopTab::Buy;
        quad(hud, {ctr.x - 2.7f, ctr.y + 3.0f}, 2.4f, 0.75f, buyT ? rgba(250, 230, 120) : rgba(70, 56, 40));
        draw_text(hud, "BUY", ctr.x - 3.15f, ctr.y + 3.2f, 0.18f, buyT ? rgba(40, 30, 20) : rgba(210, 200, 180));
        quad(hud, {ctr.x + 0.1f, ctr.y + 3.0f}, 2.4f, 0.75f, !buyT ? rgba(250, 230, 120) : rgba(70, 56, 40));
        draw_text(hud, "SELL", ctr.x - 0.5f, ctr.y + 3.2f, 0.18f, !buyT ? rgba(40, 30, 20) : rgba(210, 200, 180));
        quad(hud, {ctr.x + 3.6f, ctr.y + 3.0f}, 0.4f, 0.4f, rgba(255, 215, 70));
        draw_num(hud, g.gold, ctr.x + 3.85f, ctr.y + 3.2f, 0.16f, rgba(255, 245, 150));
        // rows: in Buy, the 3 seeds; in Sell, the 3 produce (short crop labels)
        const ItemId buyRows[3] = {ItemId::ParsnipSeed, ItemId::PotatoSeed, ItemId::CauliflowerSeed};
        const ItemId sellRows[3] = {ItemId::Parsnip, ItemId::Potato, ItemId::Cauliflower};
        const char* rowName[3] = {"PARSNIP", "POTATO", "CAULIF"};
        for (int r = 0; r < 3; ++r) {
            const float ry = ctr.y + 1.3f - r * 1.2f;
            const ItemId id = buyT ? buyRows[r] : sellRows[r];
            if (r == g.shopCursor) { quad(hud, {ctr.x - 4.7f, ry}, 0.4f, 0.4f, rgba(255, 255, 255)); }
            draw_item_icon(hud, id, {ctr.x - 4.0f, ry}, 0.9f);
            draw_text(hud, rowName[r], ctr.x - 3.4f, ry + 0.25f, 0.15f, rgba(230, 230, 230));
            const int price = buyT ? item_def(id).buyPrice : sell_price(id, g.day, g.skills[Skill::Farming].level);
            quad(hud, {ctr.x + 1.3f, ry}, 0.32f, 0.32f, rgba(255, 215, 70));
            draw_num(hud, price, ctr.x + 1.6f, ry + 0.2f, 0.16f, rgba(255, 245, 150));
            draw_text(hud, "X", ctr.x + 3.5f, ry + 0.2f, 0.14f, rgba(190, 190, 190));
            draw_num(hud, g.inv.count(id), ctr.x + 3.95f, ry + 0.2f, 0.16f, rgba(200, 230, 200));
        }
        draw_text(hud, buyT ? "E BUY   TAB SELL   ESC CLOSE" : "E SELL  F ALL  TAB BUY  ESC X",
                  ctr.x - 4.9f, ctr.y - 3.3f, 0.105f, rgba(210, 200, 180));
    }

    // toasts (top-center stack)
    for (std::size_t i = 0; i < g.toasts.size(); ++i) {
        const Toast& t = g.toasts[i];
        const float y = vh - 1.6f - 0.72f * static_cast<float>(i);
        const std::uint8_t a = static_cast<std::uint8_t>(std::clamp(t.life / 0.5f, 0.0f, 1.0f) * 235.0f);
        const float tpx = 0.14f, tw = static_cast<float>(std::strlen(t.text)) * 4.0f * tpx;   // true text width
        quad(hud, {vw * 0.5f, y}, std::max(8.0f, tw + 0.8f), 0.6f, rgba(28, 24, 20, a));
        draw_text(hud, t.text, vw * 0.5f - tw * 0.5f, y + 0.15f, tpx, rgba(t.col.r, t.col.g, t.col.b, a));
    }

    if (g.goal_reached) {                                   // non-blocking "goal reached" badge (top bar, after gold)
        const float gx = 3.7f, ty = vh - 0.5f;
        quad(hud, {gx, ty}, 0.42f, 0.42f, rgba(120, 230, 120));
        draw_text(hud, "GOAL", gx + 0.35f, ty + 0.18f, 0.13f, rgba(170, 240, 170));
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

// ── farmer sprite ────────────────────────────────────────────────────────────
void write_png(const char* path, const Image& img) {
    stbi_write_png(path, img.width(), img.height(), 4, img.pixels().data(), img.width() * 4);
}
Image make_farmer() {
    Image im(16, 20, rgba(0, 0, 0, 0));
    auto fill = [&](int x0, int y0, int x1, int y1, Rgba8 c) {
        for (int y = y0; y <= y1; ++y) { for (int x = x0; x <= x1; ++x) { im.set(x, y, c); } } };
    const Rgba8 straw = rgba(225, 200, 110), skin = rgba(245, 200, 150),
                green = rgba(70, 150, 80), denim = rgba(70, 100, 170), boot = rgba(90, 55, 30);
    fill(2, 2, 13, 4, straw); fill(4, 0, 11, 2, straw);
    fill(4, 5, 11, 9, skin);
    im.set(6, 7, rgba(30, 30, 40)); im.set(9, 7, rgba(30, 30, 40));
    fill(4, 10, 11, 14, green); fill(4, 14, 11, 18, denim);
    fill(4, 18, 7, 19, boot); fill(8, 18, 11, 19, boot);
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
        g_harvest = decode(720.0f, 0.30f, 0.7f, true);
        g_sleep = decode(180.0f, 0.40f, 0.8f);
        g_deny = decode(140.0f, 0.10f, 1.4f);
        g_levelup = decode(520.0f, 0.45f, 0.6f, true);
        g_ach = decode(900.0f, 0.28f, 0.7f, true);
        g_talk = decode(520.0f, 0.18f, 0.9f, true);
        g_gift = decode(680.0f, 0.22f, 0.7f, true);
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

void toggle_panel(Panel p) { g.panel = (g.panel == p) ? Panel::None : p; }

void on_event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
        // shop is modal: capture nav keys while open
        if (g.panel == Panel::Shop) {
            switch (ev->key_code) {
                case SAPP_KEYCODE_ESCAPE: case SAPP_KEYCODE_B: g.panel = Panel::None; return;
                case SAPP_KEYCODE_TAB:
                    g.shopTab = (g.shopTab == ShopTab::Buy) ? ShopTab::Sell : ShopTab::Buy; g.shopCursor = 0; return;
                case SAPP_KEYCODE_UP: case SAPP_KEYCODE_W: g.shopCursor = std::max(0, g.shopCursor - 1); return;
                case SAPP_KEYCODE_DOWN: case SAPP_KEYCODE_S: g.shopCursor = std::min(2, g.shopCursor + 1); return;
                case SAPP_KEYCODE_SPACE: case SAPP_KEYCODE_E: {
                    const ItemId buyRows[3] = {ItemId::ParsnipSeed, ItemId::PotatoSeed, ItemId::CauliflowerSeed};
                    const ItemId sellRows[3] = {ItemId::Parsnip, ItemId::Potato, ItemId::Cauliflower};
                    if (g.shopTab == ShopTab::Buy) { buy(buyRows[g.shopCursor], 1); }
                    else { sell(sellRows[g.shopCursor], -1); }
                    return;
                }
                case SAPP_KEYCODE_F: if (g.shopTab == ShopTab::Sell) { sell_all(); } return;
                default: return;
            }
        }
        switch (ev->key_code) {
            case SAPP_KEYCODE_ESCAPE: if (g.panel != Panel::None) { g.panel = Panel::None; } else { sapp_request_quit(); } return;
            case SAPP_KEYCODE_R: reset_game(); return;
            case SAPP_KEYCODE_1: g.tool = Tool::Hoe; return;
            case SAPP_KEYCODE_2: g.tool = Tool::Seed; return;
            case SAPP_KEYCODE_3: g.tool = Tool::Can; return;
            case SAPP_KEYCODE_4: g.tool = Tool::Axe; return;
            case SAPP_KEYCODE_5: g.tool = Tool::Pick; return;
            case SAPP_KEYCODE_I: toggle_panel(Panel::Inventory); return;
            case SAPP_KEYCODE_K: toggle_panel(Panel::Skills); return;
            case SAPP_KEYCODE_J: toggle_panel(Panel::Achievements); return;
            case SAPP_KEYCODE_G: toggle_panel(Panel::Relationships); return;
            case SAPP_KEYCODE_LEFT_BRACKET: g.inv.cycle(-1); return;
            case SAPP_KEYCODE_RIGHT_BRACKET: g.inv.cycle(1); return;
            case SAPP_KEYCODE_F:
                if (g.state == State::Playing && g.panel == Panel::None) { give_gift(g.npc_at(faced_tx(), faced_ty())); }
                return;
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
    for (auto* b : {&g_till, &g_plant, &g_water, &g_harvest, &g_sleep, &g_deny, &g_levelup, &g_ach, &g_talk, &g_gift}) {
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
    d.window_title = "OmniHarvest — grid farm sim (inventory, shop, skills, achievements, NPCs)";
    d.logger.func = slog_func;
    return d;
}
