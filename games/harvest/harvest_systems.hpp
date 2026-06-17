#pragma once

// OmniHarvest — RPG/sim DATA MODEL (pure: no game-state, no rendering, no audio).
// Defines the shared catalogs + value types every system trades in (items, the
// inventory, skills + XP curve + perks, crops, NPCs + gift prefs, achievements,
// the toast queue, the panel enum, energy/time constants) plus the pure helper
// functions over them. The stateful verbs + rendering live in harvest_app.cpp,
// which includes this header before `struct Game`.

#include <algorithm>
#include <array>
#include <cstdint>

#include <okn/render/sprite2d/image.hpp>   // Rgba8 + rgba()

namespace okh {

using okn::render::sprite2d::Rgba8;
using okn::render::sprite2d::rgba;

// ── tiles + tools ─────────────────────────────────────────────────────────────
// Append-only: existing world-gen literals + the render switch rely on the values
// Grass=0,Water=1,Tree=2,Soil=3; Shop/Rock are new.
enum class Tile : std::uint8_t { Grass, Water, Tree, Soil, Shop, Rock };
enum class Tool : int { Hoe, Seed, Can, Axe, Pick, Count };   // hotbar keys 1..5

// ── item catalog ──────────────────────────────────────────────────────────────
// Stable, append-only enum (None=0 stays first, Count last). Other systems key off
// these values, so never reorder.
enum class ItemId : std::uint8_t {
    None = 0,
    ParsnipSeed, PotatoSeed, CauliflowerSeed,   // seeds
    Parsnip, Potato, Cauliflower,               // produce
    Wood, Stone,                                // resources
    Berry, Flower,                              // forage / gifts
    Count
};
enum class ItemCat : std::uint8_t { Seed, Produce, Resource, Forage, Misc };

// An icon = up to 3 colored quads layered back→front, offsets/sizes as fractions of
// the slot size (drawn by draw_item_icon in the TU).
struct IconQuad { float ox, oy, w, h; Rgba8 col; };
struct IconSpec { int n; IconQuad q[3]; };

struct ItemDef {
    const char* name;
    ItemCat     cat;
    int         buyPrice;       // shop buy (0 = not buyable)
    int         sellPrice;      // base sell (0 = not sellable)
    IconSpec    icon;
    int         growDays;       // Seed only: watered-nights to ripen (else 0)
    ItemId      produces;       // Seed only: harvested produce (else None)
};

constexpr int kItemCount = static_cast<int>(ItemId::Count);

// Indexed by ItemId; order MUST match the enum.
inline const ItemDef kItems[kItemCount] = {
//   name             cat               buy sell  icon                                                                                          grow produces
    {"None",          ItemCat::Misc,      0,  0,  {0,{}},                                                                                          0, ItemId::None},
    {"Parsnip Seed",  ItemCat::Seed,     20,  0,  {2,{{0,0,.55f,.55f,rgba(150,110,60)},{0,-.18f,.30f,.16f,rgba(110,80,40)}}},                     3, ItemId::Parsnip},
    {"Potato Seed",   ItemCat::Seed,     30,  0,  {2,{{0,0,.55f,.55f,rgba(150,110,60)},{0,-.18f,.30f,.16f,rgba(120,90,50)}}},                     4, ItemId::Potato},
    {"Caulif Seed",   ItemCat::Seed,     60,  0,  {2,{{0,0,.55f,.55f,rgba(150,110,60)},{0,-.18f,.30f,.16f,rgba(80,110,60)}}},                     5, ItemId::Cauliflower},
    {"Parsnip",       ItemCat::Produce,   0, 35,  {2,{{0,-.10f,.30f,.55f,rgba(238,226,170)},{0,.28f,.34f,.22f,rgba(70,160,80)}}},                 0, ItemId::None},
    {"Potato",        ItemCat::Produce,   0, 60,  {1,{{0,0,.52f,.42f,rgba(170,130,80)}}},                                                         0, ItemId::None},
    {"Cauliflower",   ItemCat::Produce,   0,120,  {2,{{0,.05f,.50f,.46f,rgba(235,235,210)},{0,-.22f,.40f,.18f,rgba(70,150,80)}}},                 0, ItemId::None},
    {"Wood",          ItemCat::Resource,  0,  5,  {2,{{0,0,.55f,.30f,rgba(150,100,55)},{0,0,.50f,.10f,rgba(120,80,45)}}},                         0, ItemId::None},
    {"Stone",         ItemCat::Resource,  0,  8,  {1,{{0,0,.50f,.42f,rgba(150,150,160)}}},                                                        0, ItemId::None},
    {"Berry",         ItemCat::Forage,    0, 15,  {1,{{0,0,.36f,.36f,rgba(200,60,90)}}},                                                          0, ItemId::None},
    {"Flower",        ItemCat::Forage,    0, 25,  {2,{{0,.10f,.30f,.30f,rgba(240,180,70)},{0,-.18f,.10f,.30f,rgba(70,150,80)}}},                  0, ItemId::None},
};
inline const ItemDef& item_def(ItemId id) { return kItems[static_cast<int>(id)]; }
static_assert(sizeof(kItems) / sizeof(kItems[0]) == kItemCount, "kItems must cover every ItemId");

// The three produce types (for the Collector achievement / variety tracking).
constexpr int kProduceTypes = 3;
inline int produce_index(ItemId id) {
    switch (id) {
        case ItemId::Parsnip:     return 0;
        case ItemId::Potato:      return 1;
        case ItemId::Cauliflower: return 2;
        default:                  return -1;
    }
}

// ── inventory ─────────────────────────────────────────────────────────────────
constexpr int kInvSlots = 24;
constexpr int kStackMax  = 99;
struct Slot { ItemId id = ItemId::None; int count = 0; };

struct Inventory {
    std::array<Slot, kInvSlots> slots{};
    int sel = 0;                          // selected ("held") slot — for plant + gift

    int add(ItemId id, int n) {           // stack then fill; returns leftover that didn't fit
        if (id == ItemId::None || n <= 0) { return n; }
        for (auto& s : slots) {
            if (s.id == id && s.count < kStackMax) {
                const int take = std::min(n, kStackMax - s.count); s.count += take; n -= take;
                if (n == 0) { return 0; }
            }
        }
        for (auto& s : slots) {
            if (s.id == ItemId::None) {
                const int take = std::min(n, kStackMax); s.id = id; s.count = take; n -= take;
                if (n == 0) { return 0; }
            }
        }
        return n;
    }
    int count(ItemId id) const { int c = 0; for (const auto& s : slots) { if (s.id == id) { c += s.count; } } return c; }
    bool has_space(ItemId id) const {   // can at least one of `id` be added?
        for (const auto& s : slots) { if ((s.id == id && s.count < kStackMax) || s.id == ItemId::None) { return true; } }
        return false;
    }
    bool remove(ItemId id, int n) {       // atomic: checks total first
        if (count(id) < n) { return false; }
        for (auto& s : slots) {
            if (n == 0) { break; }
            if (s.id == id) { const int take = std::min(n, s.count); s.count -= take; n -= take; if (s.count == 0) { s.id = ItemId::None; } }
        }
        return true;
    }
    Slot& held() { return slots[sel]; }
    const Slot& held() const { return slots[sel]; }
    void cycle(int dir) { sel = (sel + dir + kInvSlots) % kInvSlots; }
    int find(ItemId id) const { for (int i = 0; i < kInvSlots; ++i) { if (slots[i].id == id) { return i; } } return -1; }
    void select(ItemId id) { const int i = find(id); if (i >= 0) { sel = i; } }
    int produce_types_owned() const {     // distinct produce types currently held
        bool seen[kProduceTypes]{}; int n = 0;
        for (const auto& s : slots) { const int pi = produce_index(s.id); if (s.count > 0 && pi >= 0 && !seen[pi]) { seen[pi] = true; ++n; } }
        return n;
    }
};

// ── crops ─────────────────────────────────────────────────────────────────────
constexpr int kRipe = 3;   // a crop is harvestable at stage >= its growDays; kRipe = max visual stage
struct Crop {
    bool   planted = false;
    int    stage   = 0;
    bool   watered = false;
    ItemId produce = ItemId::None;
    int    growDays = kRipe;
};

// ── skills ────────────────────────────────────────────────────────────────────
enum class Skill : int { Farming, Foraging, Mining, Count };
constexpr int kMaxSkillLevel = 10;
constexpr int kXpToLevel[kMaxSkillLevel + 1] = { 0, 20, 50, 100, 180, 300, 470, 700, 1000, 1400, 1900 };
constexpr int kXpHarvest = 12;   // Farming, per crop
constexpr int kXpChop    = 8;    // Foraging, per tree
constexpr int kXpMine    = 8;    // Mining, per rock

struct SkillState { int xp = 0; int level = 0; };
struct Skills {
    SkillState s[static_cast<int>(Skill::Count)]{};
    SkillState& operator[](Skill k) { return s[static_cast<int>(k)]; }
    const SkillState& operator[](Skill k) const { return s[static_cast<int>(k)]; }
};
inline int skill_level_for_xp(int xp) {
    int lvl = 0;
    for (int L = 1; L <= kMaxSkillLevel; ++L) { if (xp >= kXpToLevel[L]) { lvl = L; } else { break; } }
    return lvl;
}
// Perks (change real numbers):
constexpr int kFarmSellBonusPctPerLevel = 5;          // +5% produce sell price / Farming level
inline int farm_sell_bonus_pct(int level) { return kFarmSellBonusPctPerLevel * level; }
constexpr int kForageWoodTiers[] = {3, 6, 9};         // +1 wood/chop at each level
constexpr int kMineStoneTiers[]  = {2, 5, 8};         // +1 stone/break at each level
inline int forage_extra_wood(int level) { int n = 0; for (int t : kForageWoodTiers) { if (level >= t) { ++n; } } return n; }
inline int mine_extra_stone(int level)  { int n = 0; for (int t : kMineStoneTiers)  { if (level >= t) { ++n; } } return n; }
inline const char* skill_name(Skill k) {
    switch (k) { case Skill::Farming: return "FARMING"; case Skill::Foraging: return "FORAGING";
                 case Skill::Mining: return "MINING"; default: return "?"; }
}

// ── economy / market ──────────────────────────────────────────────────────────
constexpr int kStartGold = 120;
constexpr int kGoldGoal  = 250;   // human win: reach this much gold

// Deterministic 2D hash (also used by world-gen). Pure → reproducible market.
inline unsigned hash2(int x, int y) {
    unsigned h = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(y) * 19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
// Daily produce-price multiplier in [0.75, 1.45], per-item phase so they don't all peak together.
inline float market_mult(int day, ItemId produce) {
    return 0.75f + static_cast<float>(hash2(day, 100 + static_cast<int>(produce)) % 71u) * 0.01f;
}
// Final per-unit sell price: base * market * Farming perk.
inline int sell_price(ItemId produce, int day, int farmingLevel) {
    const int base = item_def(produce).sellPrice;
    float p = static_cast<float>(base) * market_mult(day, produce);
    p *= static_cast<float>(100 + farm_sell_bonus_pct(farmingLevel)) / 100.0f;
    return static_cast<int>(p + 0.5f);
}

// ── NPCs / relationships ──────────────────────────────────────────────────────
enum class NpcId : int { Mara, Garrett, Pia, Count };
enum class GiftTier : int { Loved, Liked, Neutral, Disliked };
constexpr int kGiftPts[4]   = { +50, +20, +5, -30 };
constexpr int kMaxFriend    = 250;   // 10 hearts
constexpr int kPtsPerHeart  = 25;
constexpr int kTalkPts      = 8;

struct NpcDef {
    const char* name;
    int hx, hy;                 // home tile (stationary in v1)
    Rgba8 body, hair;
    ItemId loved[3];            // None-terminated preference lists
    ItemId liked[3];
    ItemId disliked[3];
};
inline const NpcDef kNpc[static_cast<int>(NpcId::Count)] = {
    {"MARA",    22, 5, rgba(196, 84,120), rgba(60,40,30),
        {ItemId::Flower, ItemId::None},      {ItemId::Berry, ItemId::Cauliflower, ItemId::None}, {ItemId::Stone, ItemId::None}},
    {"GARRETT", 25, 5, rgba(80,110,180), rgba(30,30,40),
        {ItemId::Wood, ItemId::None},        {ItemId::Stone, ItemId::Parsnip, ItemId::None},     {ItemId::Flower, ItemId::None}},
    {"PIA",     28, 5, rgba(210,170, 70), rgba(120,60,30),
        {ItemId::Cauliflower, ItemId::None}, {ItemId::Parsnip, ItemId::Potato, ItemId::None},    {ItemId::Wood, ItemId::None}},
};
struct NpcState { int friendship = 0; bool talkedToday = false; bool giftedToday = false; };
inline int hearts_of(int friendship) { return friendship / kPtsPerHeart; }
inline GiftTier gift_tier(NpcId id, ItemId item) {
    const NpcDef& d = kNpc[static_cast<int>(id)];
    for (ItemId i : d.loved)    { if (i == ItemId::None) { break; } if (i == item) { return GiftTier::Loved; } }
    for (ItemId i : d.liked)    { if (i == ItemId::None) { break; } if (i == item) { return GiftTier::Liked; } }
    for (ItemId i : d.disliked) { if (i == ItemId::None) { break; } if (i == item) { return GiftTier::Disliked; } }
    return GiftTier::Neutral;
}

// ── achievements ──────────────────────────────────────────────────────────────
enum class AchId : int {
    FirstHarvest, Cultivator, Greenhorn, Forager, Prospector, Skilled, Master,
    Friendly, Beloved, Shopaholic, Collector, WellRested, Count
};
// AchDef (predicate over the game state) + kAch[] + kAchColor[] live in the TU,
// after `struct Game` is complete — the predicate needs the full type.

// ── shared toast queue ────────────────────────────────────────────────────────
constexpr int kMaxToasts = 4;
constexpr float kToastLife = 2.6f;
struct Toast { char text[40]{}; float life = 0.0f; Rgba8 col{255, 255, 255, 255}; };

// ── one-panel-at-a-time UI ────────────────────────────────────────────────────
enum class Panel : int { None, Inventory, Skills, Achievements, Relationships, Shop };
enum class ShopTab : int { Buy, Sell };

// ── energy ────────────────────────────────────────────────────────────────────
enum class EAct : int { Till, Plant, Water, Harvest, Chop, Mine, Count };
constexpr int kEnergyCost[static_cast<int>(EAct::Count)] = { 2, 0, 1, 1, 4, 5 };
constexpr int kMaxEnergy = 100;
constexpr int kLowEnergy = 15;
constexpr int kChopHits  = 2;
constexpr int kMineHits  = 3;

// ── time-of-day clock (display/flavor only) ───────────────────────────────────
constexpr float kDayStartMin       = 6.0f * 60.0f;    // 06:00
constexpr float kDayEndMin         = 26.0f * 60.0f;   // 02:00 next day (clamp)
constexpr float kRealSecPerGameMin = 0.6f;
inline int  clock_hh(float m) { return (static_cast<int>(m) / 60) % 24; }
inline int  clock_mm(float m) { return static_cast<int>(m) % 60; }
inline bool is_late(float m)  { return m >= 24.0f * 60.0f; }

}  // namespace okh
