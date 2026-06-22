// VOIDBORNE (孤舟 · 青鸟号) — a generation-ship management sim, ported from the
// Godot 4.6 / C# original (git@github.com:Teamkiller131/VOIDBORNE.git) onto the
// OmniKillerNexus engine. UI + the 2D ship world run on TeamkillerUniGUI (Dear
// ImGui); the simulation re-implements the original's day-tick settlement model
// in C++; the data layer reuses the original's JSON verbatim (rapidjson).
//
// This is the M0–M3 vertical slice:
//   M0  engine stack: UniGUI window + ImGui + rapidjson data (crops.json)
//   M1  the ticking core loop: a clock (hour→day), 6-resource economy, daily
//       settlement with ordered systems (life-support consume / ecology produce),
//       speed (1×/2×/4×) + pause
//   M2  the explorable ship: a top-down deck drawn with ImGui draw lists, free
//       continuous walking + AABB collision, walk-to-a-door interaction
//   M3  the Ecology Bay department: 8 plots, plant/water/harvest the crops, an
//       environment model that scales yield, feeding Food/O2 into the economy
//
// `--selftest` checks the data layer headlessly; `--autodemo` drives the whole
// loop headlessly (plant→grow days→harvest, asserting the economy moved) and
// writes voidborne_result.txt; `--frames N` renders N frames and exits.

#include <unigui/app/app.h>
#include <unigui/fonts/font_manager.h>
#include <imgui.h>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// ── data model (ports of the original's C# DTOs) ──────────────────────────────
struct CropDef {
    std::string id, name, category;        // category: staple | medicine | oxygen
    int growthDays = 0;
    float yield = 0, waterPerDay = 0, powerPerDay = 0;
    float idealTemp = 0, idealHumidity = 0, idealCo2 = 0;
    ImU32 color = IM_COL32(200, 200, 200, 255);
};

// The original GameResources.cs: 6 core + 3 derived resources (Gen-1 baseline).
struct GameResources {
    float power = 100, water = 1200, oxygen = 21.0f, co2 = 0.4f, food = 600, parts = 50;
    float medicine = 20, rawMaterials = 30, alienSpecimens = 0;
};

// ── M4 events (events.json + events_personal.json) ──
struct EventEffect { std::string key; float delta = 0; };   // "resource.food" -> -10
struct EventOption {
    std::string label, deptGate, requireRes;
    int requireAmt = 0;
    std::vector<EventEffect> effects;
};
struct EventDef {
    std::string id, title, body, deptGate, triggerRes, triggerFlag;
    int weight = 10, cooldown = 10, minDay = 0, lastFired = -999;
    float triggerBelow = -1;                  // fire only when triggerRes < this (optional)
    bool milestone = false, fired = false;    // milestones force-fire once their gate is met (act beats)
    std::vector<EventOption> options;
};
// ── M4 crew (crew.json) ──
struct CrewMember {
    std::string id, name, role, department, shift;
    int tier = 1, skill = 50, loyalty = 50;
    float voteLean = 0.5f;
};
// ── M5 departments ──
struct RecipeDef { std::string id, name, requiresTech; int rawCost = 0, powerCost = 0, partsOut = 0, days = 1; };
struct ResearchDef { std::string id, name, line, unlockTech; int days = 1, powerPerDay = 0; float failRate = 0.1f; };
struct RouteDef { std::string id, name, risk, desc; int days = 10, rawBonus = 0, partsBonus = 0; };
struct TradeOffer { std::string id, name; int foodCost = 0, rawCost = 0, medicineGain = 0, partsGain = 0, waterGain = 0, minDay = 0; };

constexpr int kPop = 300;               // souls aboard
constexpr int kPlots = 8;               // ecology-bay plots
constexpr float kTile = 32.0f;          // ship-world tile size (px) — /8 = 4px art pixels
constexpr float kPlayerR = 0.32f;       // player collision half-extent (tiles)
constexpr float kMoveSpeed = 9.0f;      // tiles/sec — brisk
constexpr float kMapScale = 1.8f;       // enlarge every deck blueprint

// One ecology plot.
struct Plot {
    int crop = -1;          // index into cropDefs, or -1 = empty soil
    float growth = 0;       // accumulated grow-days
    bool watered = false;
    bool ripe = false;
};

// ── multi-deck spaceship (the original's 6 decks D0..D5 joined by an elevator) ──
// Each deck is a horizontal hull section: a central corridor with rooms packed
// left-to-right in a top band (south-facing doors) + a bottom band (north-facing
// doors), and the elevator at the far left. The player rides the elevator between
// decks. Panel ids: 0 bay,1 eng,2 log,3 mfg,4 sci,5 crew,6 captain,7 nav, -1 = decor.
enum class RoomKind {
    Generic, Quarters, Mess, Medical, Lounge, School, Gym, Lab, Bridge, Nav, Comms,
    Office, Conference, Engineering, Power, Water, Logistics, Manufacturing, Storage,
    Recycle, Hydro, SeedBank, Algae, Engine, Reactor, Fuel, Cargo, Dock,
};
struct RoomSpec { std::string label; int width; int panel; RoomKind kind; };
struct DeckDef { std::string label, purpose; ImU32 accent; int width, height; std::vector<RoomSpec> top, bottom; };

struct Room {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, doorx = 0, doory = 0;
    bool doorNorth = false;
    float doorOpen = 0.0f;      // 0 shut .. 1 open — animated from player/crew proximity (Phase 2)
    int chamfer = 0;            // corner-bevel size (tiles) — furniture insets to clear it (Phase 3)
    std::string name;
    int panel = -1;
    RoomKind kind = RoomKind::Generic;
    ImU32 floorCol = IM_COL32(24, 28, 38, 255);
    ImU32 labelCol = IM_COL32(200, 210, 220, 255);
    bool isVoid = false;       // Hydro Bay G glows once the seed is discovered
};
struct WorldNpc {
    float x = 0, y = 0, phase = 0; int fx = 0, fy = 1;
    ImU32 color = IM_COL32(150, 150, 170, 255), hair = 0, skin = 0;
    float walk = 0, homeX = 0, homeY = 0;       // walk-cycle phase + wander anchor
    std::string name;
};
constexpr int kStartDeck = 3;   // habitation, like the original

struct Game {
    std::vector<CropDef> cropDefs;
    GameResources res;

    // ── time (the original TimeManager) ──
    int day = 1, hour = 6;
    float accum = 0.0f;
    float secondsPerHour = 1.2f;
    int speedIndex = 1;                 // -> {1,2,4}×
    bool paused = false;
    float simTime = 0.0f;               // speed-scaled animation/sim clock (drives walk/doors/crew/fx)

    // ── ecology bay ──
    std::array<Plot, kPlots> plots{};
    float bayTemp = 22, bayHumidity = 62, bayLight = 1.0f, bayCo2 = 0.40f;
    int selCrop = 0;                    // crop chosen to plant
    int selPlot = 0;
    bool inBay = false;                 // bay panel open?

    // ── ship world / player ──
    std::vector<DeckDef> decks;         // the 6 deck blueprints (static, built once)
    int curDeck = kStartDeck;           // which deck the player is on
    std::vector<std::string> deck;      // the CURRENT deck's tilemap ('#' wall, '.' floor)
    std::vector<Room> rooms;            // the current deck's rooms
    int deckW = 0, deckH = 0;           // current deck size
    int hullCenter = 10, hullMaxHH = 8, hullStern = 6, hullBow = 16;   // fuselage profile (for the smooth hull skin)
    int elevX = 2, elevY = 8;           // elevator tile on the current deck
    bool elevatorOpen = false;          // deck-select overlay up?
    std::vector<WorldNpc> npcsHere;     // crew standing on the current deck
    float pcx = 3.5f, pcy = 8.5f;       // continuous tile position
    int fx = 0, fy = -1;                // facing
    float playerWalk = 0;               // walk-cycle phase (advances while moving)
    float camX = 0, camY = 0;           // smoothed camera (Phase 6)
    bool camInit = false;               // snap the camera on the first frame of a deck
    float deckFade = 0;                 // 1→0 fade-in when a deck loads

    // ── settlement bookkeeping (for the HUD + verification) ──
    float lastFoodDelta = 0, lastO2Delta = 0;
    int totalHarvested = 0;
    float totalFoodHarvested = 0;

    // ── M4: events / crew / morale ──
    std::vector<EventDef> events;
    std::vector<CrewMember> crew;
    float morale = 70;
    int playerSkill = 20, playerPerf = 0;
    std::unordered_map<std::string, int> flags;
    int curEvent = -1;                 // active modal event index, or -1
    int eventsResolved = 0;
    std::string lastEventTitle;
    std::uint32_t rng = 0x1234567u;

    // ── M5: departments ──
    std::vector<RecipeDef> recipes;
    std::vector<ResearchDef> research;
    std::vector<RouteDef> routes;
    std::vector<TradeOffer> offers;
    float powerAlloc[5] = {0.25f, 0.20f, 0.20f, 0.20f, 0.15f};   // eco/mfg/sci/life/reserve
    int mfgQueue[4] = {-1, -1, -1, -1};                          // recipe indices in flight
    float mfgProgress[4] = {0, 0, 0, 0};
    int sciActive = -1; float sciProgress = 0;
    int techPoints[5] = {0, 0, 0, 0, 0};                         // survival/eng/social/mfg/frontier
    std::unordered_map<std::string, int> tech;                  // unlocked techs
    int ration = 1;                                              // 0 short / 1 normal / 2 generous
    int offerIdx = 0;

    // ── M6: routes / elections / captain / void-seed / endings ──
    std::string playerRole = "Botanist", playerDept = "Ecology";
    int playerTier = 1;
    bool isCaptain = false, ranForCaptain = false, electionWon = false;
    float captainBudget[5] = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f};     // per department
    int voyageDay = 0, voyageTotal = 400 * 16;                   // compressed 400yr voyage
    float lyTravelled = 0, lyTotal = 120;
    int routeChosen = 0, routeProgress = 0;
    float voidStage = 0;                                         // 0..100 infection
    int voidStance = -1;                                         // -1 none, 0 embrace..4 hide
    bool voidDiscovered = false;
    int ending = -1;                                            // 0..6 once reached
    int generation = 1;

    // ── UI panel state ──
    bool inCrew = false, inEng = false, inLog = false, inMfg = false, inSci = false;
    bool inStarmap = false, inCaptain = false;

    bool autodemo = false;
    int lang = 0;               // 0 = en, 1 = zh (extensible; see langCode/tr/jloc)
};

Game g;
std::string g_dataDir = "data";

// ── data loading ──────────────────────────────────────────────────────────────
ImU32 hex_color(const std::string& h) {
    if (h.size() < 6) { return IM_COL32(200, 200, 200, 255); }
    auto hx = [&](int i) {
        const int c = h[static_cast<std::size_t>(i)];
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return 0;
    };
    return IM_COL32(hx(0) * 16 + hx(1), hx(2) * 16 + hx(3), hx(4) * 16 + hx(5), 255);
}
std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { return {}; }
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
float jnum(const rapidjson::Value& v, const char* k, float def = 0.0f) {
    return (v.HasMember(k) && v[k].IsNumber()) ? v[k].GetFloat() : def;
}
bool load_crops() {
    const std::string txt = read_file(g_dataDir + "/crops.json");
    if (txt.empty()) { return false; }
    rapidjson::Document d; d.Parse(txt.c_str());
    if (d.HasParseError() || !d.HasMember("crops") || !d["crops"].IsArray()) { return false; }
    for (auto& c : d["crops"].GetArray()) {
        CropDef cd;
        cd.id = c["id"].GetString();
        cd.name = c["name"].GetString();
        if (c.HasMember("category")) { cd.category = c["category"].GetString(); }
        cd.growthDays = static_cast<int>(jnum(c, "growthDays"));
        cd.yield = jnum(c, "yield");
        cd.waterPerDay = jnum(c, "waterPerDay");
        cd.powerPerDay = jnum(c, "powerPerDay");
        cd.idealTemp = jnum(c, "idealTemp");
        cd.idealHumidity = jnum(c, "idealHumidity");
        cd.idealCo2 = jnum(c, "idealCo2");
        if (c.HasMember("color")) { cd.color = hex_color(c["color"].GetString()); }
        g.cropDefs.push_back(cd);
    }
    return true;
}

std::string jstr(const rapidjson::Value& v, const char* k, const char* def = "") {
    return (v.HasMember(k) && v[k].IsString()) ? v[k].GetString() : def;
}
float to_num(const rapidjson::Value& v) {
    if (v.IsNumber()) { return v.GetFloat(); }
    if (v.IsString()) { return static_cast<float>(std::atof(v.GetString())); }
    return 0.0f;
}

// ── i18n ─────────────────────────────────────────────────────────────────────
// Player-facing text is bilingual today (en/zh) and extensible to more languages.
// Code strings go through tr(); data strings go through jloc(), which accepts
// either a plain string (single-language) or a locale object {"en":...,"zh":...}.
const char* langCode() { return g.lang == 1 ? "zh" : "en"; }
const char* tr(const char* en, const char* zh) { return g.lang == 1 ? zh : en; }
std::string jloc(const rapidjson::Value& v, const char* k, const char* def = "") {
    if (!v.HasMember(k)) { return def; }
    const rapidjson::Value& f = v[k];
    if (f.IsString()) { return f.GetString(); }                              // single-language
    if (f.IsObject()) {                                                      // {"en":"...","zh":"..."}
        const char* lc = langCode();
        if (f.HasMember(lc) && f[lc].IsString()) { return f[lc].GetString(); }
        if (f.HasMember("en") && f["en"].IsString()) { return f["en"].GetString(); }   // fallback to en
        for (auto& m : f.GetObject()) { if (m.value.IsString()) { return m.value.GetString(); } }
    }
    return def;
}

// ── M4: events + crew loaders ──
void load_events_file(const std::string& path) {
    const std::string txt = read_file(path);
    if (txt.empty()) { return; }
    rapidjson::Document d; d.Parse(txt.c_str());
    if (d.HasParseError() || !d.HasMember("events") || !d["events"].IsArray()) { return; }
    for (auto& e : d["events"].GetArray()) {
        EventDef ev;
        ev.id = jstr(e, "id"); ev.title = jloc(e, "title"); ev.body = jloc(e, "body");
        ev.deptGate = jstr(e, "deptGate");
        ev.milestone = (jstr(e, "type") == "milestone");
        ev.weight = static_cast<int>(jnum(e, "weight", 10));
        ev.cooldown = static_cast<int>(jnum(e, "cooldown", 10));
        auto parse_conds = [&](const rapidjson::Value& arr) {
            for (auto& t : arr.GetArray()) {
                if (t.HasMember("minDay")) { ev.minDay = static_cast<int>(to_num(t["minDay"])); }
                if (t.HasMember("resource") && t.HasMember("below")) { ev.triggerRes = t["resource"].GetString(); ev.triggerBelow = to_num(t["below"]); }
                if (t.HasMember("flag") && t["flag"].IsString()) { ev.triggerFlag = t["flag"].GetString(); }
            }
        };
        if (e.HasMember("trigger") && e["trigger"].IsObject()) {
            const auto& trg = e["trigger"];
            if (trg.HasMember("any") && trg["any"].IsArray()) { parse_conds(trg["any"]); }
            if (trg.HasMember("all") && trg["all"].IsArray()) { parse_conds(trg["all"]); }
        }
        if (e.HasMember("options") && e["options"].IsArray()) {
            for (auto& o : e["options"].GetArray()) {
                EventOption op; op.label = jloc(o, "label"); op.deptGate = jstr(o, "deptGate");
                if (o.HasMember("require") && o["require"].IsObject()) {
                    for (auto& m : o["require"].GetObject()) { op.requireRes = m.name.GetString(); op.requireAmt = static_cast<int>(to_num(m.value)); break; }
                }
                if (o.HasMember("effects") && o["effects"].IsObject()) {
                    for (auto& m : o["effects"].GetObject()) { op.effects.push_back({m.name.GetString(), to_num(m.value)}); }
                }
                ev.options.push_back(op);
            }
        }
        if (!ev.options.empty()) { g.events.push_back(ev); }
    }
}
void load_crew() {
    const std::string txt = read_file(g_dataDir + "/crew.json");
    if (txt.empty()) { return; }
    rapidjson::Document d; d.Parse(txt.c_str());
    if (d.HasParseError() || !d.HasMember("npcs") || !d["npcs"].IsArray()) { return; }
    for (auto& n : d["npcs"].GetArray()) {
        CrewMember c;
        c.id = jstr(n, "id"); c.name = jstr(n, "name"); c.role = jstr(n, "role");
        c.department = jstr(n, "department"); c.shift = jstr(n, "shift");
        c.tier = static_cast<int>(jnum(n, "tier", 1));
        c.skill = static_cast<int>(jnum(n, "skill", 50));
        c.loyalty = static_cast<int>(jnum(n, "loyalty", 50));
        c.voteLean = jnum(n, "voteLean", 0.5f);
        g.crew.push_back(c);
    }
}
// ── M5: department data ──
void load_recipes() {
    const std::string txt = read_file(g_dataDir + "/recipes.json");
    rapidjson::Document d; d.Parse(txt.c_str());
    if (d.HasParseError() || !d.HasMember("recipes") || !d["recipes"].IsArray()) { return; }
    for (auto& r : d["recipes"].GetArray()) {
        RecipeDef rd; rd.id = jstr(r, "id"); rd.name = jstr(r, "name"); rd.requiresTech = jstr(r, "requiresTech");
        rd.rawCost = static_cast<int>(jnum(r, "rawCost")); rd.powerCost = static_cast<int>(jnum(r, "powerCost"));
        rd.partsOut = static_cast<int>(jnum(r, "partsOut")); rd.days = static_cast<int>(jnum(r, "days", 1));
        g.recipes.push_back(rd);
    }
}
void load_research() {
    const std::string txt = read_file(g_dataDir + "/research.json");
    rapidjson::Document d; d.Parse(txt.c_str());
    if (d.HasParseError() || !d.HasMember("projects") || !d["projects"].IsArray()) { return; }
    for (auto& r : d["projects"].GetArray()) {
        ResearchDef rd; rd.id = jstr(r, "id"); rd.name = jstr(r, "name"); rd.line = jstr(r, "line"); rd.unlockTech = jstr(r, "unlockTech");
        rd.days = static_cast<int>(jnum(r, "days", 1)); rd.powerPerDay = static_cast<int>(jnum(r, "powerPerDay")); rd.failRate = jnum(r, "failRate", 0.1f);
        g.research.push_back(rd);
    }
}
void load_routes() {
    const std::string txt = read_file(g_dataDir + "/routes.json");
    rapidjson::Document d; d.Parse(txt.c_str());
    if (d.HasParseError() || !d.HasMember("routes") || !d["routes"].IsArray()) { return; }
    for (auto& r : d["routes"].GetArray()) {
        RouteDef rd; rd.id = jstr(r, "id"); rd.name = jstr(r, "name"); rd.risk = jstr(r, "risk"); rd.desc = jstr(r, "description");
        rd.days = static_cast<int>(jnum(r, "days", 10)); rd.rawBonus = static_cast<int>(jnum(r, "rawBonus")); rd.partsBonus = static_cast<int>(jnum(r, "partsBonus"));
        g.routes.push_back(rd);
    }
}
void load_trade() {
    const std::string txt = read_file(g_dataDir + "/trade_offers.json");
    rapidjson::Document d; d.Parse(txt.c_str());
    if (d.HasParseError() || !d.HasMember("offers") || !d["offers"].IsArray()) { return; }
    for (auto& o : d["offers"].GetArray()) {
        TradeOffer t; t.id = jstr(o, "id"); t.name = jstr(o, "name");
        t.foodCost = static_cast<int>(jnum(o, "foodCost")); t.rawCost = static_cast<int>(jnum(o, "rawCost"));
        t.medicineGain = static_cast<int>(jnum(o, "medicineGain")); t.partsGain = static_cast<int>(jnum(o, "partsGain"));
        t.waterGain = static_cast<int>(jnum(o, "waterGain")); t.minDay = static_cast<int>(jnum(o, "minDay"));
        g.offers.push_back(t);
    }
}
void load_events_all() {
    load_events_file(g_dataDir + "/events.json");
    load_events_file(g_dataDir + "/events_personal.json");
    load_events_file(g_dataDir + "/events_memory.json");   // The Long Memory darkline (Act I+)
}
void load_all_data() {
    load_crops(); load_events_all();
    load_crew(); load_recipes(); load_research(); load_routes(); load_trade();
}
// Re-resolve event text after a language switch (does not touch crew/resource state).
void reload_events() { g.events.clear(); g.curEvent = -1; load_events_all(); }

// ── effects + the event engine ──
float* res_ptr(const std::string& k) {
    GameResources& r = g.res;
    if (k == "food") { return &r.food; } if (k == "water") { return &r.water; } if (k == "power") { return &r.power; }
    if (k == "oxygen") { return &r.oxygen; } if (k == "co2") { return &r.co2; } if (k == "parts") { return &r.parts; }
    if (k == "medicine") { return &r.medicine; } if (k == "raw" || k == "rawMaterials") { return &r.rawMaterials; }
    if (k == "alienSpecimens" || k == "specimens") { return &r.alienSpecimens; }
    return nullptr;
}
void apply_effect(const std::string& key, float d) {
    if (key.rfind("resource.", 0) == 0) { if (float* p = res_ptr(key.substr(9))) { *p = std::max(0.0f, *p + d); } }
    else if (key == "social.morale") { g.morale = std::clamp(g.morale + d, 0.0f, 100.0f); }
    else if (key == "player.skill") { g.playerSkill += static_cast<int>(d); }
    else if (key == "player.perf") { g.playerPerf += static_cast<int>(d); }
    else if (key.rfind("void.", 0) == 0) { g.voidStage = std::clamp(g.voidStage + d, 0.0f, 100.0f); g.voidDiscovered = true; }
    else if (key.rfind("flag.", 0) == 0) { g.flags[key.substr(5)] = d != 0 ? static_cast<int>(d) : 1; }
}
std::uint32_t next_rand() { g.rng = g.rng * 1664525u + 1013904223u; return g.rng; }
bool resource_below(const std::string& res, float below) { float* p = res_ptr(res); return p && *p < below; }
void maybe_trigger_event() {
    if (g.curEvent >= 0 || g.events.empty()) { return; }
    // milestones force-fire once their gate (minDay + optional flag) is met — the act beats
    for (int i = 0; i < static_cast<int>(g.events.size()); ++i) {
        EventDef& e = g.events[static_cast<std::size_t>(i)];
        if (!e.milestone || e.fired || g.day < e.minDay) { continue; }
        if (!e.triggerFlag.empty() && g.flags[e.triggerFlag] <= 0) { continue; }
        e.fired = true; e.lastFired = g.day; g.curEvent = i; g.lastEventTitle = e.title; return;
    }
    std::vector<int> pool; int total = 0;
    for (int i = 0; i < static_cast<int>(g.events.size()); ++i) {
        const EventDef& e = g.events[static_cast<std::size_t>(i)];
        if (e.milestone || e.weight <= 0) { continue; }
        if (g.day - e.lastFired < e.cooldown) { continue; }
        if (g.day < e.minDay) { continue; }
        if (e.triggerBelow >= 0 && !resource_below(e.triggerRes, e.triggerBelow)) { continue; }
        if (!e.triggerFlag.empty() && g.flags[e.triggerFlag] <= 0) { continue; }   // flag-gated consequence events
        if (!e.deptGate.empty() && e.deptGate != g.playerDept) { continue; }
        pool.push_back(i); total += e.weight;
    }
    if (pool.empty() || total <= 0) { return; }
    if (next_rand() % 100u >= 50u) { return; }       // ~half the days surface an event
    int pick = static_cast<int>(next_rand() % static_cast<unsigned>(total)), acc = 0, chosen = pool[0];
    for (int i : pool) { acc += g.events[static_cast<std::size_t>(i)].weight; if (pick < acc) { chosen = i; break; } }
    g.curEvent = chosen; g.events[static_cast<std::size_t>(chosen)].lastFired = g.day;
    g.lastEventTitle = g.events[static_cast<std::size_t>(chosen)].title;
}
void resolve_event(int optIdx) {
    if (g.curEvent < 0) { return; }
    EventDef& e = g.events[static_cast<std::size_t>(g.curEvent)];
    if (optIdx >= 0 && optIdx < static_cast<int>(e.options.size())) {
        EventOption& o = e.options[static_cast<std::size_t>(optIdx)];
        bool can = true;
        if (!o.requireRes.empty()) { float* p = res_ptr(o.requireRes); if (p && *p < static_cast<float>(o.requireAmt)) { can = false; } }
        if (can) { for (auto& ef : o.effects) { apply_effect(ef.key, ef.delta); } ++g.eventsResolved; }
    }
    g.curEvent = -1;
}

// ── M5: department daily ticks (S6/S8/S9) ──
float power_factor(int sink) {   // sink: 0 eco / 1 mfg / 2 sci / 3 life / 4 reserve
    const float cap = g.isCaptain ? (0.6f + g.captainBudget[sink] * 2.0f) : 1.0f;
    return std::clamp(g.powerAlloc[sink] * 5.0f * (g.res.power / 100.0f) * cap, 0.3f, 1.8f);
}
void tick_manufacturing() {
    for (int i = 0; i < 4; ++i) {
        if (g.mfgQueue[i] < 0) { continue; }
        const RecipeDef& r = g.recipes[static_cast<std::size_t>(g.mfgQueue[i])];
        g.mfgProgress[i] += power_factor(1);
        g.res.power = std::max(0.0f, g.res.power - r.powerCost * 0.5f);
        if (g.mfgProgress[i] >= static_cast<float>(r.days)) {
            g.res.parts += r.partsOut; g.mfgQueue[i] = -1; g.mfgProgress[i] = 0;
        }
    }
}
void tick_research() {
    if (g.sciActive < 0) { return; }
    const ResearchDef& p = g.research[static_cast<std::size_t>(g.sciActive)];
    g.sciProgress += power_factor(2);
    g.res.power = std::max(0.0f, g.res.power - p.powerPerDay);
    if (g.sciProgress >= static_cast<float>(p.days)) {
        if (static_cast<float>(next_rand() % 1000u) / 1000.0f >= p.failRate) {
            if (!p.unlockTech.empty()) { g.tech[p.unlockTech] = 1; }
            const int line = p.line == "survival" ? 0 : p.line == "engineering" ? 1 : p.line == "social" ? 2 : p.line == "manufacturing" ? 3 : 4;
            g.techPoints[line]++;
        }
        g.sciActive = -1; g.sciProgress = 0;
    }
}
// ── M6: voyage / void-seed / elections / endings ──
void tick_voyage() {
    g.voyageDay++;
    g.lyTravelled = std::min(g.lyTotal, g.lyTravelled + 0.05f);
    if ((g.voyageDay % 64) == 0) { ++g.generation; }   // a generation ~every 64 ticks
}
void tick_voidseed() {
    g.voidStage = std::clamp(g.voidStage + 0.12f + (g.morale < 40 ? 0.18f : 0.0f), 0.0f, 100.0f);
    if (g.voidStage > 25 && !g.voidDiscovered && (next_rand() % 100u) < 4u) { g.voidDiscovered = true; }
}
void update_player_tier() { g.playerTier = g.playerSkill >= 70 ? 3 : g.playerSkill >= 40 ? 2 : 1; }
float election_yes_share() {
    float yes = 0, tot = 0;
    for (auto& c : g.crew) { const float w = 1.0f + c.tier * 0.5f; tot += w; yes += w * (c.voteLean * 0.6f + (g.morale / 100.0f) * 0.4f); }
    return tot > 0 ? yes / tot : 0;
}
void run_for_captain() {
    if (g.playerTier < 3 || g.ranForCaptain) { return; }
    g.ranForCaptain = true;
    g.electionWon = election_yes_share() >= 0.5f;
    if (g.electionWon) { g.isCaptain = true; g.playerRole = "Captain"; }
}
const char* kEndingName[7] = {     // 0 collapse, 1 suffocation, 2 wake-communion, 3 wake-overrun, 4 captain, 5 understood, 6 quiet
    "THE COLD QUIET", "THE LAST GARDEN", "THE SHIP REMEMBERS", "A SONG WITH NO SINGER",
    "NEW SHORE, NEW DAWN", "WE CARRY THEM WITH US", "A QUIET LANDFALL",
};
const char* kEndingNameZh[7] = {
    "\xE5\x86\xB7\xE5\xAF\x82", "\xE6\x9C\x80\xE5\x90\x8E\xE7\x9A\x84\xE8\x8A\xB1\xE5\x9B\xAD",
    "\xE5\xBD\x92\xE8\x88\x9F\xE6\x9C\x89\xE5\xBF\x86", "\xE6\x97\xA0\xE4\xB8\xBB\xE4\xB9\x8B\xE6\xAD\x8C",
    "\xE6\x96\xB0\xE5\xB2\xB8\xC2\xB7\xE6\x96\xB0\xE7\x94\x9F", "\xE6\x88\x91\xE4\xBB\xAC\xE5\xB8\xA6\xE7\x9D\x80\xE4\xBB\x96\xE4\xBB\xAC",
    "\xE9\x9D\x99\xE9\xBB\x98\xE9\x9D\xA0\xE5\xB2\xB8",
};
const char* ending_name(int i) { return (i >= 0 && i < 7) ? tr(kEndingName[i], kEndingNameZh[i]) : "\xE2\x80\x94"; }
void evaluate_endings() {
    if (g.ending >= 0) { return; }
    if (g.res.food <= 0 && g.morale < 12) { g.ending = 0; return; }
    if (g.res.oxygen <= 2) { g.ending = 1; return; }
    if (g.voidStage >= 100) { g.ending = (g.voidStance == 0) ? 2 : 3; return; }
    if (g.lyTravelled >= g.lyTotal) { g.ending = g.isCaptain ? 4 : (g.voidDiscovered ? 5 : 6); }
}

// ── ecology model (the original EcologyEnvironment: ideal-closeness → yield) ───
float env_factor(const CropDef& c) {
    const float dt = std::fabs(g.bayTemp - c.idealTemp) / 18.0f;
    const float dh = std::fabs(g.bayHumidity - c.idealHumidity) / 40.0f;
    const float dc = std::fabs(g.bayCo2 - c.idealCo2) / 0.5f;
    float f = 1.0f - 0.5f * (dt + dh + dc);
    f *= 0.4f + 0.6f * std::clamp(g.bayLight, 0.0f, 1.2f);   // light cuts production
    return std::clamp(f, 0.25f, 1.25f);
}
// Harvest a ripe plot → resources by crop category. Returns food gained.
float harvest_plot(int i) {
    Plot& p = g.plots[static_cast<std::size_t>(i)];
    if (p.crop < 0 || !p.ripe) { return 0.0f; }
    const CropDef& c = g.cropDefs[static_cast<std::size_t>(p.crop)];
    const float amount = c.yield * env_factor(c);
    if (c.category == "medicine") { g.res.medicine += amount; }
    else if (c.category == "oxygen") { g.res.oxygen = std::min(30.0f, g.res.oxygen + amount * 0.15f); g.res.food += amount * 0.3f; }
    else { g.res.food += amount; }
    g.totalHarvested++; g.totalFoodHarvested += amount;
    p = Plot{};   // back to empty soil
    return amount;
}

// ── the daily settlement (ordered systems, the original DailySettlementCoordinator) ──
void daily_settlement() {
    const float before_food = g.res.food, before_o2 = g.res.oxygen;

    // (order 10) ECOLOGY produce: watered planted crops grow toward ripeness; algae
    // passively scrub CO2 → O2. Crops dry overnight (must be re-watered).
    for (auto& p : g.plots) {
        if (p.crop < 0) { continue; }
        const CropDef& c = g.cropDefs[static_cast<std::size_t>(p.crop)];
        if (p.watered && !p.ripe) {
            p.growth += env_factor(c);
            g.res.water -= c.waterPerDay;
            g.res.power -= c.powerPerDay;
            if (p.growth >= static_cast<float>(c.growthDays)) { p.ripe = true; }
        }
        p.watered = false;
    }

    // (order 50) LIFE SUPPORT consume: 300 mouths eat, drink, breathe. Feeding them is a
    // STANDING STRAIN — the big hydroponics bays cover most of it, but never quite all, so
    // food trends down unless the player actively farms, powers ecology, and keeps morale up.
    const float hydroFeed = 14.0f * (0.40f + g.powerAlloc[0]) * std::clamp(0.55f + g.morale / 250.0f, 0.35f, 1.05f);
    g.res.food = std::max(0.0f, g.res.food + hydroFeed - kPop * 0.05f);   // ~ -15 eaten, ~ +7 grown → a real gap to close
    g.res.water = std::max(0.0f, g.res.water - kPop * 0.032f);            // ~9.6/day
    g.res.oxygen = std::clamp(g.res.oxygen - 0.25f + 0.30f, 0.0f, 30.0f);  // breathe vs recycle
    g.res.co2 = std::clamp(g.res.co2 + 0.04f - 0.045f, 0.0f, 5.0f);
    g.res.power = std::clamp(g.res.power - 4.0f + 8.0f, 0.0f, 300.0f);     // reactor net +

    g.lastFoodDelta = g.res.food - before_food;
    g.lastO2Delta = g.res.oxygen - before_o2;

    // departments (S6/S8/S9) + meta (S11/SX/S14/S17)
    tick_manufacturing();
    tick_research();
    g.res.food += (g.ration - 1) * -3.0f;                     // generous ration costs extra food
    g.res.power = std::clamp(g.res.power, 0.0f, 300.0f);
    tick_voyage();
    tick_voidseed();
    update_player_tier();

    // morale drifts on scarcity / surplus + ration policy (the original S10 flavour). With
    // 300 mouths, low stores bite — and true starvation tips the ship toward collapse.
    const float scarcity = (g.res.food < kPop * 0.5f ? -1.5f : 0.6f)
                         + (g.res.food < kPop * 0.15f ? -2.5f : 0.0f)   // <45 food: people are going hungry
                         + (g.ration - 1) * 0.4f;
    g.morale = std::clamp(g.morale + scarcity + (g.voidStage > 40 ? -0.5f : 0.0f), 0.0f, 100.0f);

    evaluate_endings();
    maybe_trigger_event();   // S13 — a decision may surface for the day
}

// ── time advance (the original TimeManager accumulator) ──
void advance_time(float dt) {
    if (g.paused || g.curEvent >= 0) { return; }   // a pending event freezes the clock
    const float speed = (g.speedIndex == 0) ? 1.0f : (g.speedIndex == 1) ? 2.0f : 4.0f;
    g.accum += dt * speed;
    while (g.accum >= g.secondsPerHour) {
        g.accum -= g.secondsPerHour;
        if (++g.hour >= 24) { g.hour = 0; ++g.day; daily_settlement(); }
    }
}
// The 1x/2x/4x setting fast-forwards every real-time system, not just the clock.
float speed_mul() { return g.speedIndex == 0 ? 1.0f : g.speedIndex == 1 ? 2.0f : 4.0f;
}

// ── ship-world helpers ──
char deck_at(int x, int y) {
    if (x < 0 || y < 0 || y >= static_cast<int>(g.deck.size()) || x >= static_cast<int>(g.deck[static_cast<std::size_t>(y)].size())) { return '#'; }
    return g.deck[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
}
// tile chars: '.' floor, '#' wall, 'D' door, 'S' space (outside the hull), '1'..'4' diagonal bulkhead
bool is_diag(char c) { return c >= '1' && c <= '4'; }
bool walkable_tile(int x, int y) { const char c = deck_at(x, y); return c == '.' || c == 'D'; }
// A 'D' door tile blocks like a wall until its owning room's door has slid open.
bool door_tile_solid(int x, int y) {
    for (const Room& r : g.rooms) {
        if (r.doory == y && (r.doorx == x || r.doorx + 1 == x)) { return r.doorOpen < 0.42f; }
    }
    return false;   // a stray 'D' with no owner — treat as passable
}
bool tile_solid(int x, int y) {
    const char c = deck_at(x, y);
    if (c == '#' || c == 'S' || is_diag(c)) { return true; }   // hull, vacuum, and diagonal bulkheads all block
    if (c == 'D') { return door_tile_solid(x, y); }
    return false;
}
bool blocked_box(float cx, float cy) {
    const int x0 = static_cast<int>(std::floor(cx - kPlayerR)), x1 = static_cast<int>(std::floor(cx + kPlayerR));
    const int y0 = static_cast<int>(std::floor(cy - kPlayerR)), y1 = static_cast<int>(std::floor(cy + kPlayerR));
    for (int y = y0; y <= y1; ++y) { for (int x = x0; x <= x1; ++x) { if (tile_solid(x, y)) { return true; } } }
    return false;
}
// Crew are soft obstacles: a move is rejected only if it pushes the player CLOSER to a
// nearby crew member than they already are — so you bump and slide, never get trapped.
bool crew_blocks(float nx, float ny, float ox, float oy) {
    constexpr float rmin = kPlayerR + 0.30f;   // combined personal space
    for (const WorldNpc& n : g.npcsHere) {
        const float ndx = nx - n.x, ndy = ny - n.y;
        const float nd2 = ndx * ndx + ndy * ndy;
        if (nd2 < rmin * rmin) {
            const float odx = ox - n.x, ody = oy - n.y;
            if (nd2 < odx * odx + ody * ody) { return true; }   // only block moves that close in
        }
    }
    return false;
}
void move_player(float dcol, float drow, float dt) {
    const float len = std::sqrt(dcol * dcol + drow * drow);
    if (len < 0.0001f) { return; }
    dcol /= len; drow /= len;
    if (std::fabs(dcol) >= std::fabs(drow)) { g.fx = dcol > 0 ? 1 : -1; g.fy = 0; }
    else { g.fx = 0; g.fy = drow > 0 ? 1 : -1; }
    const float step = kMoveSpeed * dt;
    const float nx = g.pcx + dcol * step;
    if (!blocked_box(nx, g.pcy) && !crew_blocks(nx, g.pcy, g.pcx, g.pcy)) { g.pcx = nx; }
    const float ny = g.pcy + drow * step;
    if (!blocked_box(g.pcx, ny) && !crew_blocks(g.pcx, ny, g.pcx, g.pcy)) { g.pcy = ny; }
    g.playerWalk = std::fmod(g.playerWalk + step, 1000.0f);   // advance walk cycle while moving
}
// Animate every door: slide open when the player or a crew member is near, else shut.
void update_doors(float dt) {
    const float k = std::min(1.0f, dt * 10.0f);   // exponential smoothing toward target
    for (Room& r : g.rooms) {
        const float dcx = static_cast<float>(r.doorx) + 1.0f, dcy = static_cast<float>(r.doory) + 0.5f;
        bool nearDoor = std::max(std::fabs(g.pcx - dcx), std::fabs(g.pcy - dcy)) <= 2.0f;
        if (!nearDoor) {
            for (const WorldNpc& n : g.npcsHere) {
                if (std::max(std::fabs(n.x - dcx), std::fabs(n.y - dcy)) <= 1.4f) { nearDoor = true; break; }
            }
        }
        r.doorOpen += ((nearDoor ? 1.0f : 0.0f) - r.doorOpen) * k;
        r.doorOpen = std::clamp(r.doorOpen, 0.0f, 1.0f);
    }
}
// Gentle crew life: each NPC drifts on a small orbit around its spawn, only ever
// stepping onto walkable floor, facing its motion and advancing its walk cycle.
void update_crew(float dt) {
    (void)dt;
    const float t = g.simTime;
    for (WorldNpc& n : g.npcsHere) {
        const float r = 1.1f;
        const float cx = n.homeX + std::sin(t * 0.45f + n.phase) * r;
        const float cy = n.homeY + std::cos(t * 0.37f + n.phase * 1.4f) * r * 0.8f;
        if (tile_solid(static_cast<int>(std::floor(cx)), static_cast<int>(std::floor(cy)))) { continue; }   // never drift into a wall
        const float dx = cx - n.x, dy = cy - n.y, d2 = dx * dx + dy * dy;
        if (d2 > 2.0e-5f) {
            if (std::fabs(dx) >= std::fabs(dy)) { n.fx = dx > 0 ? 1 : -1; n.fy = 0; }
            else { n.fx = 0; n.fy = dy > 0 ? 1 : -1; }
            n.walk = std::fmod(n.walk + std::sqrt(d2) * 6.0f, 1000.0f);
        }
        n.x = cx; n.y = cy;
    }
}
// ── the 6-deck blueprint (a port of the original ShipFloorCatalog) ──
void build_decks_data() {
    using K = RoomKind;
    auto add = [&](const char* label, const char* purpose, ImU32 accent, int w, int h,
                   std::vector<RoomSpec> top, std::vector<RoomSpec> bottom) {
        g.decks.push_back(DeckDef{label, purpose, accent, w, h, std::move(top), std::move(bottom)});
    };
    g.decks.clear();
    add("D0 DRIVE & DOCK", "Propulsion, reactor, fuel, cargo", IM_COL32(255, 122, 77, 255), 60, 18,
        {{"MAIN ENGINE", 16, -1, K::Engine}, {"REACTOR HALL", 12, -1, K::Reactor}, {"FUEL BAY", 12, -1, K::Fuel}},
        {{"CARGO HOLD", 16, -1, K::Cargo}, {"DOCK BAY", 14, -1, K::Dock}, {"CRAWLWAY", 10, -1, K::Generic}});
    add("D1 OPERATIONS", "Power, water, logistics, manufacturing", IM_COL32(255, 193, 94, 255), 58, 18,
        {{"ENGINEERING", 12, 1, K::Engineering}, {"POWER CORE", 10, -1, K::Power}, {"WATER RECLAIM", 10, -1, K::Water}, {"LOGISTICS", 14, 2, K::Logistics}},
        {{"MANUFACTURING", 14, 3, K::Manufacturing}, {"MACHINE SHOP", 10, -1, K::Storage}, {"PARTS STORE", 9, -1, K::Storage}, {"RECYCLING", 9, -1, K::Recycle}});
    add("D2 ECOLOGY", "Hydroponics -- feeding 300 is a daily battle", IM_COL32(123, 216, 138, 255), 80, 20,
        {{"HYDRO BAY A", 14, 0, K::Hydro}, {"HYDRO BAY B", 14, 0, K::Hydro}, {"HYDRO BAY C", 14, 0, K::Hydro}, {"HYDRO BAY D", 14, 0, K::Hydro}},
        {{"HYDRO BAY E", 14, 0, K::Hydro}, {"HYDRO BAY F", 14, 0, K::Hydro}, {"HYDRO BAY G", 14, 0, K::Hydro}, {"SEED BANK", 10, -1, K::SeedBank}, {"ALGAE O2", 9, -1, K::Algae}});
    add("D3 HABITATION", "Quarters for 300 souls, mess, medbay, school, nursery", IM_COL32(159, 180, 216, 255), 94, 20,
        {{"QUARTERS A", 14, 5, K::Quarters}, {"QUARTERS B", 14, 5, K::Quarters}, {"QUARTERS C", 14, 5, K::Quarters}, {"MESS HALL", 18, -1, K::Mess}, {"QUARTERS D", 14, 5, K::Quarters}, {"QUARTERS E", 14, 5, K::Quarters}},
        {{"MEDBAY", 12, -1, K::Medical}, {"LOUNGE", 11, -1, K::Lounge}, {"SCHOOL", 11, -1, K::School}, {"NURSERY", 10, -1, K::School}, {"GYM", 9, -1, K::Gym}, {"QUARTERS F", 14, 5, K::Quarters}, {"QUARTERS G", 14, 5, K::Quarters}});
    add("D4 SCIENCE", "Labs, alien samples, quarantine, archive", IM_COL32(143, 214, 255, 255), 54, 18,
        {{"SCIENCE LAB", 14, 4, K::Lab}, {"ALIEN LAB", 12, -1, K::Lab}, {"ARCHIVE", 10, -1, K::Generic}},
        {{"QUARANTINE", 12, -1, K::Generic}, {"OBSERVATORY", 12, -1, K::Generic}, {"MED RESEARCH", 10, -1, K::Lab}});
    add("D5 COMMAND", "Bridge, navigation, conference, dome", IM_COL32(192, 214, 255, 255), 50, 16,
        {{"BRIDGE", 14, 6, K::Bridge}, {"NAV ROOM", 12, 7, K::Nav}, {"COMMS", 10, -1, K::Comms}},
        {{"CAPTAIN OFFICE", 12, -1, K::Office}, {"CONFERENCE", 12, -1, K::Conference}, {"OBSERVATION DOME", 10, -1, K::Generic}});
}
ImU32 deck_floor(ImU32 accent, RoomKind k) {
    const int cr = accent & 0xFF, cg = (accent >> 8) & 0xFF, cb = (accent >> 16) & 0xFF;
    const float t = (k == RoomKind::Hydro) ? 0.30f : 0.22f;   // darken the accent into a floor tint
    return IM_COL32(static_cast<int>(cr * t), static_cast<int>(cg * t), static_cast<int>(cb * t), 255);
}
// which deck a department works on + its uniform colour
int dept_deck(const std::string& d) {
    if (d == "Ecology") { return 2; }
    if (d == "Engineering" || d == "Resources" || d == "Logistics" || d == "Manufacturing") { return 1; }
    if (d == "Research" || d == "Science") { return 4; }
    if (d == "Command") { return 5; }
    return 3;   // habitation
}
ImU32 dept_color(const std::string& d) {
    if (d == "Ecology") { return IM_COL32(96, 176, 104, 255); }
    if (d == "Engineering") { return IM_COL32(224, 156, 72, 255); }
    if (d == "Resources" || d == "Logistics") { return IM_COL32(120, 170, 220, 255); }
    if (d == "Manufacturing") { return IM_COL32(214, 116, 84, 255); }
    if (d == "Research" || d == "Science") { return IM_COL32(124, 184, 232, 255); }
    if (d == "Command") { return IM_COL32(204, 150, 222, 255); }
    return IM_COL32(150, 152, 172, 255);
}
bool walkable_tile(int x, int y);   // fwd (defined above)
void place_npcs() {
    g.npcsHere.clear();
    std::vector<ImVec2> spots;
    for (const Room& r : g.rooms) {
        spots.push_back(ImVec2((r.x0 + r.x1) * 0.5f, (r.y0 + r.y1) * 0.5f));
        spots.push_back(ImVec2(static_cast<float>(r.x0) + 2.0f, static_cast<float>(r.y0) + 2.0f));
        spots.push_back(ImVec2(static_cast<float>(r.x1) - 2.0f, static_cast<float>(r.y1) - 2.0f));
    }
    const int corr = g.deckH / 2 - 1;
    for (int x = 8; x < g.deckW - 4; x += 5) { spots.push_back(ImVec2(x + 0.5f, corr + 0.5f)); }
    std::size_t si = 0;
    for (int i = 0; i < static_cast<int>(g.crew.size()); ++i) {
        const bool here = dept_deck(g.crew[static_cast<std::size_t>(i)].department) == g.curDeck
                          || g.curDeck == 3                                       // everyone lives on habitation
                          || ((g.curDeck == 0 || g.curDeck == 5) && i < 4);       // a few staff the sparse decks
        if (!here) { continue; }
        while (si < spots.size() && (!walkable_tile(static_cast<int>(spots[si].x), static_cast<int>(spots[si].y))
                                     || deck_at(static_cast<int>(spots[si].x), static_cast<int>(spots[si].y)) == 'D')) { ++si; }
        if (si >= spots.size()) { break; }
        static const ImU32 kHair[] = { IM_COL32(74, 56, 44, 255), IM_COL32(28, 26, 30, 255), IM_COL32(158, 120, 64, 255),
                                       IM_COL32(126, 62, 40, 255), IM_COL32(186, 188, 192, 255), IM_COL32(70, 48, 78, 255) };
        static const ImU32 kSkin[] = { IM_COL32(242, 210, 174, 255), IM_COL32(214, 170, 128, 255),
                                       IM_COL32(178, 130, 96, 255), IM_COL32(140, 100, 72, 255), IM_COL32(250, 224, 198, 255) };
        WorldNpc n; n.x = spots[si].x; n.y = spots[si].y; n.phase = static_cast<float>(i) * 1.7f;
        n.homeX = n.x; n.homeY = n.y;
        n.color = dept_color(g.crew[static_cast<std::size_t>(i)].department);
        n.hair = kHair[static_cast<std::size_t>(i) % 6]; n.skin = kSkin[static_cast<std::size_t>(i * 3 + 1) % 5];
        n.name = g.crew[static_cast<std::size_t>(i)].name; n.fy = (i & 1) ? 1 : -1;
        g.npcsHere.push_back(n); ++si;
    }
}
// deterministic hash → stable per-deck layout variation (no RNG state, survives rebuilds)
static inline unsigned vbhash(unsigned x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
// Generate the CURRENT deck as a SHIP FUSELAGE floating in space: a long hull with a
// rounded stern + tapered bow, vacuum ('S') outside, rooms in the midship around a
// central spine, and every convex corner turned into a 45° bulkhead — so it reads as
// a spaceship section, not a square floor plan.
void build_deck(int d) {
    g.curDeck = std::clamp(d, 0, static_cast<int>(g.decks.size()) - 1);
    const DeckDef& def = g.decks[static_cast<std::size_t>(g.curDeck)];
    const int W = std::max(72, static_cast<int>(std::lround(def.width * kMapScale * 1.5f)));   // long fuselage
    const int H = 27;                          // a big tube: deep enough to house 300 (scrolls a little)
    g.deckW = W; g.deckH = H;
    g.deck.assign(static_cast<std::size_t>(H), std::string(static_cast<std::size_t>(W), 'S'));   // start as open vacuum
    const int center = H / 2;                  // 13
    const int maxHH = center - 2;              // walkable half-height 11: tall rooms hold many bunks
    const int sternLen = std::max(6, W / 16), bowLen = std::max(14, W / 6);
    g.hullCenter = center; g.hullMaxHH = maxHH; g.hullStern = sternLen; g.hullBow = bowLen;
    auto hullHalfF = [&](float x) -> float {   // smooth fuselage half-height profile
        if (x < static_cast<float>(sternLen)) { return maxHH * std::sqrt(std::max(0.0f, (x + 0.5f) / (sternLen + 0.5f))); }
        if (x >= static_cast<float>(W - bowLen)) { const float t = (W - 1 - x) / bowLen; return maxHH * std::pow(std::max(0.0f, t), 0.7f); }
        return static_cast<float>(maxHH);
    };
    // carve the fuselage: interior floor + a 1-tile hull rim, vacuum outside
    for (int x = 0; x < W; ++x) {
        const int h = std::clamp(static_cast<int>(std::lround(hullHalfF(static_cast<float>(x)))), 0, maxHH);
        const int top = center - h, bot = center + h;
        for (int y = top + 1; y < bot; ++y) { g.deck[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '.'; }
        if (top >= 0 && top < H) { g.deck[static_cast<std::size_t>(top)][static_cast<std::size_t>(x)] = '#'; }
        if (bot >= 0 && bot < H && bot != top) { g.deck[static_cast<std::size_t>(bot)][static_cast<std::size_t>(x)] = '#'; }
    }
    const int corr = center;
    g.rooms.clear();
    int hydro = 0;
    auto h01 = [&](int a, int b) { return static_cast<float>(vbhash(static_cast<unsigned>((g.curDeck * 2654435761U) ^ (a * 40503U) ^ (b * 12345U))) & 1023U) / 1023.0f; };
    auto wallRC = [&](int x, int y) { if (x >= 0 && y >= 0 && x < W && y < H && g.deck[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == '.') { g.deck[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '#'; } };
    const int tTop = center - maxHH + 1, tBot = center + maxHH - 1;
    const int xStart = sternLen + 4, xEnd = W - bowLen - 4;
    auto pack = [&](const std::vector<RoomSpec>& specs, bool topBand) {
        int x = xStart, idx = 0;
        for (const RoomSpec& s : specs) {
            const int rw = std::max(6, static_cast<int>(std::lround(s.width * kMapScale)));
            const int x0 = x, x1 = x + rw - 1;
            if (x1 >= xEnd) { break; }
            const bool deep = (s.kind == RoomKind::Hydro || s.kind == RoomKind::Mess || s.kind == RoomKind::Engine || s.kind == RoomKind::Cargo || s.kind == RoomKind::Bridge);
            const int setback = deep ? 0 : static_cast<int>(h01(idx, topBand ? 1 : 2) * 1.99f);
            int y0, y1, dy;
            if (topBand) { y0 = tTop; y1 = (corr - 2) - setback; dy = y1; }
            else { y1 = tBot; y0 = (corr + 2) + setback; dy = y0; }
            if (y1 - y0 < 3) { if (topBand) { y1 = corr - 2; dy = y1; } else { y0 = corr + 2; dy = y0; } }
            for (int xx = x0; xx <= x1; ++xx) { wallRC(xx, y0); wallRC(xx, y1); }
            for (int yy = y0; yy <= y1; ++yy) { wallRC(x0, yy); wallRC(x1, yy); }
            const int dx = (x0 + x1) / 2;
            Room R; R.x0 = x0; R.y0 = y0; R.x1 = x1; R.y1 = y1; R.doorx = dx; R.doory = dy; R.doorNorth = !topBand; R.chamfer = 1;
            R.name = s.label; R.panel = s.panel; R.kind = s.kind;
            R.floorCol = deck_floor(def.accent, s.kind); R.labelCol = def.accent;
            if (s.kind == RoomKind::Hydro) { R.isVoid = (++hydro == 7); }   // Bay G
            g.rooms.push_back(R);
            x = x1 + 2 + (h01(idx * 7 + 3, topBand ? 5 : 9) < 0.4f ? 1 : 0);
            ++idx;
        }
    };
    pack(def.top, true);
    pack(def.bottom, false);
    // re-assert every door (a 2-wide opening on the spine-facing wall)
    for (const Room& r : g.rooms) {
        g.deck[static_cast<std::size_t>(r.doory)][static_cast<std::size_t>(r.doorx)] = 'D';
        g.deck[static_cast<std::size_t>(r.doory)][static_cast<std::size_t>(std::min(W - 2, r.doorx + 1))] = 'D';
    }
    // ── diagonalize every convex exterior corner → 45° bulkheads (the "not square" pass) ──
    {
        const std::vector<std::string> src = g.deck;   // snapshot so conversions don't cascade
        auto op = [&](int x, int y) { if (x < 0 || y < 0 || x >= W || y >= H) { return false; } const char c = src[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]; return c == '.' || c == 'D' || c == 'S'; };
        auto so = [&](int x, int y) { if (x < 0 || y < 0 || x >= W || y >= H) { return true; } return src[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == '#'; };
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (src[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != '#') { continue; }
                const bool n = op(x, y - 1), s = op(x, y + 1), w = op(x - 1, y), e = op(x + 1, y);
                char dc = 0;                                                    // wall-triangle quadrant
                if (n && w && so(x, y + 1) && so(x + 1, y)) { dc = '3'; }       // open NW → wall fills SE
                else if (n && e && so(x, y + 1) && so(x - 1, y)) { dc = '4'; }  // open NE → wall fills SW
                else if (s && w && so(x, y - 1) && so(x + 1, y)) { dc = '2'; }  // open SW → wall fills NE
                else if (s && e && so(x, y - 1) && so(x - 1, y)) { dc = '1'; }  // open SE → wall fills NW
                if (dc) { g.deck[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = dc; }
            }
        }
    }
    g.elevX = std::max(2, sternLen - 1); g.elevY = center;
    g.deck[static_cast<std::size_t>(center)][static_cast<std::size_t>(g.elevX)] = '.';
    g.deck[static_cast<std::size_t>(center)][static_cast<std::size_t>(g.elevX + 1)] = '.';
    g.pcx = static_cast<float>(g.elevX) + 1.6f; g.pcy = static_cast<float>(center) + 0.5f;
    g.camInit = false; g.deckFade = 1.0f;                    // snap camera + fade the new deck in
    place_npcs();
}
int room_of(int tx, int ty) {
    for (int i = 0; i < static_cast<int>(g.rooms.size()); ++i) {
        const Room& r = g.rooms[static_cast<std::size_t>(i)];
        if (tx > r.x0 && tx < r.x1 && ty > r.y0 && ty < r.y1) { return i; }
    }
    return -1;
}
int station_at_player() {
    const int tx = static_cast<int>(std::floor(g.pcx)), ty = static_cast<int>(std::floor(g.pcy));
    for (int i = 0; i < static_cast<int>(g.rooms.size()); ++i) {
        const Room& r = g.rooms[static_cast<std::size_t>(i)];
        if (r.panel >= 0 && std::abs(tx - r.doorx) <= 1 && std::abs(ty - r.doory) <= 1) { return i; }
    }
    return -1;
}
bool near_elevator() {
    const int tx = static_cast<int>(std::floor(g.pcx)), ty = static_cast<int>(std::floor(g.pcy));
    return std::abs(tx - g.elevX) <= 1 && std::abs(ty - g.elevY) <= 1;
}
void open_panel(int p) {
    switch (p) {
        case 0: g.inBay = true; break;     case 1: g.inEng = true; break;     case 2: g.inLog = true; break;
        case 3: g.inMfg = true; break;     case 4: g.inSci = true; break;     case 5: g.inCrew = true; break;
        case 6: g.inCaptain = true; break; case 7: g.inStarmap = true; break;
    }
}

// ── rendering (pixel-art: no anti-aliasing, blocky sprites, snapped camera) ──────
ImU32 lighten(ImU32 c, int add) {
    const int r = std::min(255, static_cast<int>(c & 0xFF) + add);
    const int g = std::min(255, static_cast<int>((c >> 8) & 0xFF) + add);
    const int b = std::min(255, static_cast<int>((c >> 16) & 0xFF) + add);
    return IM_COL32(r, g, b, 255);
}
// multiply RGB by f (f<1 darken, >1 lighten); alpha preserved
ImU32 shade(ImU32 c, float f) {
    const int r = std::clamp(static_cast<int>((c & 0xFF) * f), 0, 255);
    const int g = std::clamp(static_cast<int>(((c >> 8) & 0xFF) * f), 0, 255);
    const int b = std::clamp(static_cast<int>(((c >> 16) & 0xFF) * f), 0, 255);
    return IM_COL32(r, g, b, (c >> 24) & 0xFF);
}
// linear blend a->b by t (0..1)
ImU32 mix(ImU32 a, ImU32 b, float t) {
    auto L = [&](int sh) { return static_cast<int>(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t); };
    return IM_COL32(L(0), L(8), L(16), 255);
}

// ── Hybrid tile-art seam ─────────────────────────────────────────────────────
// Phase 1 draws the ship procedurally on the ImGui draw list. To later drop in
// hand-drawn PNG pixel-art without a rewrite, every tile goes through one of the
// helpers below; when a tileset atlas is loaded the same call site can blit a UV
// rect instead of running the procedural path. The hook is intentionally simple.
struct TileAtlas {
    ImTextureID tex = 0;      // 0 ⇒ procedural (current); set once a PNG tileset is loaded
    float tileuv = 16.0f;     // source tile size in the atlas
    bool active() const { return tex != 0; }
};
TileAtlas g_atlas;            // stays procedural until a real atlas is wired in

// ── Phase 4: detail & lighting helpers ───────────────────────────────────────
// Soft stepped light pool (pixel-art glow): concentric discs brightening inward.
void glow(ImDrawList* dl, ImVec2 c, float r, ImU32 col, int a0 = 50) {
    const int R = static_cast<int>(col & 0xFF), G = static_cast<int>((col >> 8) & 0xFF), B = static_cast<int>((col >> 16) & 0xFF);
    for (int i = 4; i >= 1; --i) {
        dl->AddCircleFilled(c, r * static_cast<float>(i) / 4.0f, IM_COL32(R, G, B, a0 * (5 - i) / 4), 20);
    }
}
// Per-room-kind floor material: a cheap overlay drawn on each floor tile in PASS A.
void floor_pattern(ImDrawList* dl, ImVec2 a, ImVec2 b, int tx, int ty, RoomKind k) {
    using K = RoomKind;
    switch (k) {
        case K::Engine: case K::Reactor: case K::Fuel: case K::Power: case K::Dock:      // hazard caution diagonals
            if (((tx * 3 + ty) % 4) == 0) { dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), IM_COL32(214, 170, 48, 30), 2.5f); }
            break;
        case K::Engineering: case K::Manufacturing: case K::Storage: case K::Recycle: case K::Water: case K::Cargo:   // metal grating
            dl->AddRectFilled(ImVec2(a.x + kTile * 0.5f - 1, a.y), ImVec2(a.x + kTile * 0.5f, b.y), IM_COL32(0, 0, 0, 26));
            dl->AddRectFilled(ImVec2(a.x, a.y + kTile * 0.5f - 1), ImVec2(b.x, a.y + kTile * 0.5f), IM_COL32(0, 0, 0, 26));
            break;
        case K::Medical: case K::Lab:                                                    // clean grid tile
            dl->AddRect(a, b, IM_COL32(120, 165, 185, 16), 0, 0, 1.0f);
            break;
        case K::Lounge: case K::Quarters: case K::School: case K::Gym: case K::Mess:      // soft carpet stipple
            if ((tx + ty) & 1) { dl->AddRectFilled(ImVec2(a.x + kTile * 0.42f, a.y + kTile * 0.42f), ImVec2(a.x + kTile * 0.55f, a.y + kTile * 0.55f), IM_COL32(255, 255, 255, 9)); }
            break;
        default: break;
    }
}
// A starfield viewport set into a space-facing hull wall.
void hull_window(ImDrawList* dl, ImVec2 a, ImVec2 b, int tx, int ty, float driftPhase) {
    dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, b.y - 2), IM_COL32(5, 7, 14, 255));   // deep space
    unsigned h = vbhash(static_cast<unsigned>(tx * 73856093) ^ static_cast<unsigned>(ty * 19349663));
    for (int s = 0; s < 5; ++s) {                                                         // drifting stars
        h = vbhash(h); const float fxp = static_cast<float>(h % 1000) / 1000.0f;
        h = vbhash(h); const float fyp = static_cast<float>(h % 1000) / 1000.0f;
        const float drift = std::fmod(fxp + driftPhase, 1.0f);
        const float px = a.x + 3 + drift * (kTile - 7), py = a.y + 3 + fyp * (kTile - 7);
        const int br = 150 + static_cast<int>(h % 105);
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + 2, py + 2), IM_COL32(br, br, std::min(255, br + 12), 255));
    }
    dl->AddRect(a, b, IM_COL32(44, 52, 70, 255), 0, 0, 2.0f);                             // window frame
    dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, a.y + 4), IM_COL32(70, 80, 104, 255));   // top sill highlight
}
// 8x11 top-down crew sprite. keys: 0 clear,1 hair,2 skin,3 uniform,4 sheen,5 pants,6 eye,7 boots
const char* kCrewPx[11] = {       // eyes/legs are drawn separately (facing + walk cycle)
    "01111110", "12222221", "12222221", "12222221", "03333330",
    "33333333", "34333343", "33333333", "03333330", "05500550", "07700770",
};
// Pixel crew sprite with 4-direction facing, a walk cycle, and per-character palette.
void draw_character(ImDrawList* dl, ImVec2 c, float s, ImU32 uniform, int fx, int fy,
                    bool isPlayer, float walk = 0.0f, ImU32 hairCol = 0, ImU32 skinCol = 0) {
    constexpr int W = 8;
    const float px = std::max(2.0f, std::floor(s / 8.0f));
    const float ox = std::floor(c.x - W * px * 0.5f), oy = std::floor(c.y - 11 * px * 0.82f);
    const ImU32 hair = hairCol ? hairCol : IM_COL32(74, 56, 44, 255);
    const ImU32 skin = skinCol ? skinCol : IM_COL32(240, 208, 172, 255);
    const ImU32 pants = IM_COL32(46, 50, 62, 255), eye = IM_COL32(32, 30, 38, 255),
                boots = IM_COL32(58, 44, 32, 255), sheen = lighten(uniform, 44);
    const int dir = fx > 0 ? 3 : fx < 0 ? 2 : fy < 0 ? 1 : 0;          // 0 down,1 up,2 left,3 right
    const int stepf = (walk > 0.001f) ? (static_cast<int>(walk * 2.5f) & 1) : -1;   // -1 idle
    dl->AddRectFilled(ImVec2(ox + px, oy + 11 * px), ImVec2(ox + (W - 1) * px, oy + 11 * px + px), IM_COL32(0, 0, 0, 70));   // shadow
    // body bitmap rows 0..8 (legs/boots drawn below for the walk cycle)
    for (int r = 0; r < 9; ++r) {
        for (int cc = 0; cc < W; ++cc) {
            const char k = kCrewPx[r][cc];
            if (k == '0') { continue; }
            const ImU32 col = k == '1' ? hair : k == '2' ? skin : k == '3' ? uniform : k == '4' ? sheen
                            : k == '5' ? pants : k == '6' ? eye : boots;
            const float qx = ox + cc * px, qy = oy + r * px;
            dl->AddRectFilled(ImVec2(qx, qy), ImVec2(qx + px + 0.6f, qy + px + 0.6f), col);
        }
    }
    // legs + boots with a 2-frame walk shuffle (one leg lifts as it steps)
    auto leg = [&](int col, float lift) {
        const float qx = ox + col * px, qy = oy + 9 * px - lift;
        dl->AddRectFilled(ImVec2(qx, qy), ImVec2(qx + 2 * px + 0.6f, qy + px + 0.6f), pants);
        dl->AddRectFilled(ImVec2(qx, qy + px), ImVec2(qx + 2 * px + 0.6f, qy + 2 * px + 0.6f), boots);
    };
    const float lift = px * 0.5f;
    leg(1, stepf == 0 ? lift : 0.0f);
    leg(5, stepf == 1 ? lift : 0.0f);
    // facing overlay: eyes shifted toward the look direction, or back-of-head hair when facing up
    if (dir == 1) {
        for (int r = 1; r <= 3; ++r) { for (int cc = 1; cc <= 6; ++cc) { const float qx = ox + cc * px, qy = oy + r * px; dl->AddRectFilled(ImVec2(qx, qy), ImVec2(qx + px + 0.6f, qy + px + 0.6f), hair); } }
    } else {
        const int ex = dir == 2 ? 1 : dir == 3 ? 3 : 2;
        const float ey = oy + 2 * px;
        auto pip = [&](int col) { const float qx = ox + col * px; dl->AddRectFilled(ImVec2(qx, ey), ImVec2(qx + px + 0.6f, ey + px + 0.6f), eye); };
        pip(ex); pip(ex + 3);
    }
    if (isPlayer) {     // bright pixel beacon over the head
        dl->AddRectFilled(ImVec2(ox + 3 * px, oy - px * 2.0f), ImVec2(ox + 5 * px, oy - px), IM_COL32(150, 220, 255, 255));
        dl->AddRectFilled(ImVec2(ox + 3.5f * px, oy - px), ImVec2(ox + 4.5f * px, oy - px * 0.2f), IM_COL32(150, 220, 255, 255));
    }
}

void draw_world() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImDrawListFlags saved = dl->Flags;                 // pixel-art: kill anti-aliasing
    dl->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AntiAliasedFill);
    const ImVec2 p0 = vp->Pos, sz = vp->Size;
    dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), IM_COL32(6, 7, 14, 255));   // deep space
    {   // a starfield drifting slowly across the void — the ship sails through it
        const float st = g.simTime * 1.4f;
        for (int i = 0; i < 95; ++i) {
            unsigned h = vbhash(static_cast<unsigned>(i) * 0x9e3779b9u);
            const float bx = static_cast<float>(h % 1000) / 1000.0f; h = vbhash(h);
            const float by = static_cast<float>(h % 1000) / 1000.0f; h = vbhash(h);
            const float spd = 3.0f + static_cast<float>(h % 100) * 0.22f;
            const float mx = p0.x + std::fmod(bx * sz.x + st * spd, sz.x), my = p0.y + by * sz.y;
            const int br = 80 + static_cast<int>(h % 140);
            const float sp = (h % 9 == 0) ? 2.0f : 1.0f;
            dl->AddRectFilled(ImVec2(mx, my), ImVec2(mx + sp, my + sp), IM_COL32(br, br, std::min(255, br + 24), 255));
        }
    }

    const float hud = 96.0f, viewW = sz.x, viewH = sz.y - hud;
    auto camAxis = [](float playerPx, float full, float view) {
        if (full <= view) { return (full - view) * 0.5f; }
        return std::clamp(playerPx - view * 0.5f, 0.0f, full - view);
    };
    const float fullW = g.deckW * kTile, fullH = g.deckH * kTile;
    const float tcamx = camAxis(g.pcx * kTile, fullW, viewW), tcamy = camAxis(g.pcy * kTile, fullH, viewH);
    if (!g.camInit) { g.camX = tcamx; g.camY = tcamy; g.camInit = true; }            // snap on a new deck
    else { const float ck = std::min(1.0f, ImGui::GetIO().DeltaTime * 8.0f); g.camX += (tcamx - g.camX) * ck; g.camY += (tcamy - g.camY) * ck; }
    const float camx = g.camX, camy = g.camY;
    const float ox = std::floor(p0.x - camx), oy = std::floor(p0.y + hud - camy);   // snap to whole pixels
    auto sx = [&](float c) { return ox + c * kTile; };
    auto sy = [&](float r) { return oy + r * kTile; };
    auto box = [&](float cx, float cy, float w, float h, ImU32 c) { dl->AddRectFilled(ImVec2(sx(cx), sy(cy)), ImVec2(sx(cx + w), sy(cy + h)), c); };

    const int x0 = std::max(0, static_cast<int>(camx / kTile));
    const int x1 = std::min(g.deckW - 1, static_cast<int>((camx + viewW) / kTile) + 1);
    const int y0 = std::max(0, static_cast<int>(camy / kTile));
    const int y1 = std::min(g.deckH - 1, static_cast<int>((camy + viewH) / kTile) + 1);
    // Per-deck hull theme (walls tinted by the deck accent, kept dark/metallic).
    const ImU32 accent = g.decks[static_cast<std::size_t>(g.curDeck)].accent;
    const ImU32 capCol  = mix(IM_COL32(68, 76, 98, 255), accent, 0.17f);   // lit roof of the wall
    const ImU32 capLo   = shade(capCol, 0.70f);                            // shaded lower roof band
    const ImU32 capHi   = shade(capCol, 1.60f);                            // top/left highlight
    const ImU32 faceCol = shade(capCol, 0.46f);                            // the vertical FRONT face (height)
    const ImU32 faceDk  = shade(capCol, 0.26f);                            // dark base of the face
    const ImU32 rivetC  = shade(capCol, 1.70f);
    const ImU32 subfl   = IM_COL32(13, 16, 23, 255);                       // gap colour revealed by bevels
    auto isWall = [&](int x, int y) { return deck_at(x, y) == '#'; };
    auto floorColAt = [&](int tx, int ty) -> ImU32 {
        const int ri = room_of(tx, ty);
        if (ri >= 0) {
            const Room& rr = g.rooms[static_cast<std::size_t>(ri)];
            if (rr.isVoid && g.voidDiscovered) { return IM_COL32(64, 38, 86, 255); }
            return rr.floorCol;
        }
        return ((tx + ty) & 1) ? IM_COL32(24, 28, 38, 255) : IM_COL32(19, 23, 31, 255);
    };

    // ── PASS A: floors (+ a dark base under walls for bevels) + wall-adjacent skirting/AO ──
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            const ImVec2 a(sx(static_cast<float>(tx)), sy(static_cast<float>(ty)));
            const ImVec2 b(a.x + kTile, a.y + kTile);
            const char tc = deck_at(tx, ty);
            if (tc == 'S') { continue; }                          // open vacuum → let the starfield show
            if (tc == '#') { dl->AddRectFilled(a, b, subfl); continue; }
            dl->AddRectFilled(a, b, floorColAt(tx, ty));          // floor / door / diagonal-bulkhead floor half
            dl->AddRectFilled(ImVec2(b.x - 1, a.y), b, IM_COL32(0, 0, 0, 30));   // deck-plating seams
            dl->AddRectFilled(ImVec2(a.x, b.y - 1), b, IM_COL32(0, 0, 0, 30));
            { const int pri = room_of(tx, ty); if (pri >= 0) { floor_pattern(dl, a, b, tx, ty, g.rooms[static_cast<std::size_t>(pri)].kind); } }
            if (isWall(tx - 1, ty)) { dl->AddRectFilled(a, ImVec2(a.x + 3, b.y), IM_COL32(0, 0, 0, 95)); }  // skirting
            if (isWall(tx + 1, ty)) { dl->AddRectFilled(ImVec2(b.x - 3, a.y), b, IM_COL32(0, 0, 0, 95)); }
            if (isWall(tx, ty + 1)) { dl->AddRectFilled(ImVec2(a.x, b.y - 3), b, IM_COL32(0, 0, 0, 70)); }  // wall base to S
            // (north edge gets the hanging wall face from PASS B instead of a skirt)
        }
    }

    // ── PASS B: walls — lit top cap + dark front face (height) + autotiled corners ──
    const float faceH = kTile * 0.52f, riv = kTile * 0.16f, half = kTile * 0.5f;
    const float winDrift = std::fmod(g.simTime * 0.015f, 1.0f);   // slow star drift
    auto cutCorner = [&](float cx, float cy, float dx, float dy) {       // exterior corner → 2-step bevel
        const float n = kTile * 0.34f, h = n * 0.5f;
        auto rect = [&](float ax, float ay, float bx, float by) {
            dl->AddRectFilled(ImVec2(std::min(ax, bx), std::min(ay, by)), ImVec2(std::max(ax, bx), std::max(ay, by)), subfl);
        };
        rect(cx, cy, cx + dx * n, cy + dy * h);
        rect(cx, cy, cx + dx * h, cy + dy * n);
    };
    auto aoCorner = [&](float cx, float cy, float dx, float dy) {        // interior corner → soft AO
        const float n = kTile * 0.34f;
        dl->AddRectFilled(ImVec2(std::min(cx, cx + dx * n), std::min(cy, cy + dy * n)),
                          ImVec2(std::max(cx, cx + dx * n), std::max(cy, cy + dy * n)), IM_COL32(0, 0, 0, 70));
    };
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            if (!isWall(tx, ty)) { continue; }
            const ImVec2 a(sx(static_cast<float>(tx)), sy(static_cast<float>(ty)));
            const ImVec2 b(a.x + kTile, a.y + kTile);
            const bool nF = !isWall(tx, ty - 1), sF = !isWall(tx, ty + 1), wF = !isWall(tx - 1, ty), eF = !isWall(tx + 1, ty);
            if (sF) {       // a wall standing in front of the floor behind it → a vertical face = height
                dl->AddRectFilled(ImVec2(a.x, b.y), ImVec2(b.x, b.y + faceH), faceCol);
                dl->AddRectFilled(ImVec2(a.x + half - 1, b.y), ImVec2(a.x + half, b.y + faceH), shade(faceCol, 0.80f));   // panel seam
                dl->AddRectFilled(ImVec2(a.x, b.y + faceH - 3), ImVec2(b.x, b.y + faceH), faceDk);                        // dark base
                dl->AddRectFilled(ImVec2(a.x, b.y + faceH), ImVec2(b.x, b.y + faceH + 5), IM_COL32(0, 0, 0, 70));         // AO cast on floor
                dl->AddRectFilled(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y + 1), IM_COL32(0, 0, 0, 120));                    // crisp eave line
            }
            dl->AddRectFilled(a, ImVec2(b.x, a.y + half), capCol);                                  // top cap (lit)
            dl->AddRectFilled(ImVec2(a.x, a.y + half), b, capLo);                                   // lower roof band
            if (nF) { dl->AddRectFilled(a, ImVec2(b.x, a.y + 3), capHi); }                          // lit north roof edge
            if (wF) { dl->AddRectFilled(a, ImVec2(a.x + 2, a.y + half), capHi); }                   // lit west edge (raised block)
            if (eF) { dl->AddRectFilled(ImVec2(b.x - 2, a.y), ImVec2(b.x, a.y + half), IM_COL32(0, 0, 0, 95)); }  // shaded east edge
            const bool hullEdge = (tx == 0 || ty == 0 || tx == g.deckW - 1 || ty == g.deckH - 1);
            if (hullEdge && ((tx + ty) % 4 == 0)) { hull_window(dl, a, b, tx, ty, winDrift); }   // starfield viewport
            else { dl->AddRectFilled(ImVec2(a.x + riv, a.y + riv), ImVec2(a.x + riv * 2, a.y + riv * 2), rivetC); }
            // autotiled corners (dual-grid idea on a single grid): cut exterior, shade interior
            if (nF && wF) { cutCorner(a.x, a.y, +1, +1); } else if (!nF && !wF && !isWall(tx - 1, ty - 1)) { aoCorner(a.x, a.y, +1, +1); }
            if (nF && eF) { cutCorner(b.x, a.y, -1, +1); } else if (!nF && !eF && !isWall(tx + 1, ty - 1)) { aoCorner(b.x, a.y, -1, +1); }
            if (sF && wF) { cutCorner(a.x, b.y, +1, -1); } else if (!sF && !wF && !isWall(tx - 1, ty + 1)) { aoCorner(a.x, b.y, +1, -1); }
            if (sF && eF) { cutCorner(b.x, b.y, -1, -1); } else if (!sF && !eF && !isWall(tx + 1, ty + 1)) { aoCorner(b.x, b.y, -1, -1); }
        }
    }

    // ── diagonal bulkheads (45° walls) — the "not square" corners + the hull taper ──
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            const char dc = deck_at(tx, ty);
            if (!is_diag(dc)) { continue; }
            const float ax = sx(static_cast<float>(tx)), ay = sy(static_cast<float>(ty)), bx = ax + kTile, by = ay + kTile;
            const ImVec2 TL(ax, ay), TR(bx, ay), BL(ax, by), BR(bx, by);
            ImVec2 p1, p2, p3, hA, hB, mid;        // wall triangle + lit hypotenuse + the solid (right-angle) corner
            if (dc == '1') { p1 = TL; p2 = TR; p3 = BL; hA = TR; hB = BL; mid = TL; }       // wall fills NW
            else if (dc == '3') { p1 = TR; p2 = BR; p3 = BL; hA = TR; hB = BL; mid = BR; }  // wall fills SE
            else if (dc == '2') { p1 = TL; p2 = TR; p3 = BR; hA = TL; hB = BR; mid = TR; }  // wall fills NE
            else { p1 = TL; p2 = BR; p3 = BL; hA = TL; hB = BR; mid = BL; }                 // '4' wall fills SW
            dl->AddTriangleFilled(p1, p2, p3, capCol);                                      // bulkhead face (lit)
            dl->AddTriangleFilled(ImVec2((hA.x + mid.x) * 0.5f, (hA.y + mid.y) * 0.5f),
                                  ImVec2((hB.x + mid.x) * 0.5f, (hB.y + mid.y) * 0.5f), mid, capLo);   // shaded inner half
            dl->AddLine(hA, hB, capHi, 2.0f);                                              // lit leading edge
            dl->AddLine(hA, hB, IM_COL32(0, 0, 0, 70), 1.0f);                              // thin seam
        }
    }
    // rooms: accent trim + blocky furniture + door + console + label
    for (const Room& r : g.rooms) {
        if (r.x1 < x0 || r.x0 > x1 || r.y1 < y0 || r.y0 > y1) { continue; }
        const ImU32 trim = (r.labelCol & 0x00FFFFFFu) | 0x40000000u;
        dl->AddRect(ImVec2(sx(static_cast<float>(r.x0)) + 3, sy(static_cast<float>(r.y0)) + 3),
                    ImVec2(sx(static_cast<float>(r.x1) + 1) - 3, sy(static_cast<float>(r.y1) + 1) - 3), trim, 0, 0, 2.0f);
        // furniture is bounded to a chamfer-clear interior rect [fx0,fx1]×[fy0,fy1]
        const float ch = static_cast<float>(r.chamfer);
        const float fx0 = static_cast<float>(r.x0) + ch + 1.2f, fy0 = static_cast<float>(r.y0) + ch + 1.2f;
        const float fx1 = static_cast<float>(r.x1) - ch - 1.2f, fy1 = static_cast<float>(r.y1) - ch - 1.2f;
        if (r.kind == RoomKind::Quarters) {                 // dense bunk grid — racked housing for the 300
            for (float by = fy0; by < fy1 - 1.2f; by += 1.62f) {
                for (float bx = fx0; bx < fx1 - 1.1f; bx += 1.66f) {
                    box(bx, by, 1.40f, 1.30f, IM_COL32(52, 58, 72, 255));
                    box(bx + 0.10f, by + 0.10f, 1.18f, 0.76f, IM_COL32(188, 194, 208, 255));
                    box(bx + 0.15f, by + 0.13f, 0.42f, 0.34f, IM_COL32(246, 246, 250, 255));
                    box(bx + 0.10f, by + 0.54f, 1.18f, 0.40f, IM_COL32(88, 120, 172, 255));
                }
            }
        } else if (r.kind == RoomKind::Hydro) {             // blocky plant beds
            for (float py = fy0; py < fy1 - 1.0f; py += 1.5f) {
                for (float px = fx0; px < fx1 - 1.0f; px += 1.5f) {
                    box(px, py, 1.1f, 1.1f, IM_COL32(74, 54, 40, 255));
                    box(px + 0.28f, py + 0.5f, 0.55f, 0.45f, IM_COL32(58, 116, 64, 255));
                    box(px + 0.3f, py + 0.26f, 0.5f, 0.4f, IM_COL32(98, 184, 102, 255));
                    box(px + 0.4f, py + 0.3f, 0.2f, 0.2f, IM_COL32(168, 230, 156, 255));
                }
            }
            for (float gy = fy0 + 1.2f; gy < fy1 - 0.8f; gy += 4.6f) {            // grow-light pools over the beds
                for (float gx = fx0 + 1.5f; gx < fx1 - 0.8f; gx += 5.0f) { glow(dl, ImVec2(sx(gx), sy(gy)), kTile * 1.7f, IM_COL32(150, 240, 170, 255), 26); }
            }
        } else if (r.kind == RoomKind::Reactor) {                                 // pulsing reactor core
            const float pt = 0.5f + 0.5f * std::sin(g.simTime * 2.4f);
            const float cxT = static_cast<float>(r.x0 + r.x1) * 0.5f, cyT = static_cast<float>(r.y0 + r.y1) * 0.5f;
            glow(dl, ImVec2(sx(cxT), sy(cyT)), kTile * (2.4f + pt), IM_COL32(255, 138, 56, 255), 64);
            box(cxT - 1.9f, cyT - 1.9f, 3.8f, 3.8f, IM_COL32(34, 28, 30, 255));
            box(cxT - 1.4f, cyT - 1.4f, 2.8f, 2.8f, IM_COL32(72, 42, 30, 255));
            box(cxT - 0.95f, cyT - 0.95f, 1.9f, 1.9f, mix(IM_COL32(206, 92, 40, 255), IM_COL32(255, 224, 130, 255), pt));
            box(cxT - 0.45f, cyT - 0.45f, 0.9f, 0.9f, IM_COL32(255, 246, 214, 255));
        } else if (r.kind == RoomKind::Mess || r.kind == RoomKind::Conference) {   // tables + plates
            const float tw = std::max(2.0f, fx1 - fx0);
            for (float yy = fy0 + 0.4f; yy < fy1 - 1.0f; yy += 2.4f) {
                box(fx0, yy, tw, 1.0f, IM_COL32(98, 80, 58, 255));
                box(fx0, yy, tw, 0.22f, IM_COL32(124, 102, 74, 255));
                for (float px = fx0 + 0.6f; px < fx1 - 0.6f; px += 1.6f) { box(px - 0.2f, yy + 0.32f, 0.4f, 0.4f, IM_COL32(212, 214, 220, 255)); }
            }
        } else {                                            // lockers with handles
            for (int k = 0; k < 2; ++k) {
                const float lx = k == 0 ? fx0 : fx1 - 1.1f;
                box(lx, fy0 + 0.3f, 1.1f, 1.9f, IM_COL32(56, 62, 76, 255));
                box(lx + 0.12f, fy0 + 0.45f, 0.86f, 1.6f, IM_COL32(70, 78, 94, 255));
                box(lx + 0.48f, fy0 + 0.8f, 0.16f, 0.9f, IM_COL32(150, 158, 176, 255));
            }
        }
        // ── animated airlock: header lintel + jamb posts + two sliding leaves ──
        {
            const float dx0 = sx(static_cast<float>(r.doorx)), dy0 = sy(static_cast<float>(r.doory));
            const float dW = kTile * 2.0f, dH = kTile, open = r.doorOpen;
            const bool console = r.panel >= 0;
            const ImU32 frameC  = console ? IM_COL32(126, 98, 40, 255)  : IM_COL32(74, 80, 98, 255);
            const ImU32 frameHi = console ? IM_COL32(196, 158, 74, 255) : IM_COL32(120, 128, 152, 255);
            const ImU32 leafC   = console ? IM_COL32(158, 126, 52, 255) : IM_COL32(104, 112, 132, 255);
            const ImU32 leafHi  = lighten(leafC, 46), leafLo = shade(leafC, 0.58f);
            const float postW = kTile * 0.15f, headH = kTile * 0.30f, thrH = kTile * 0.15f;
            const float oL = dx0 + postW, oR = dx0 + dW - postW, oT = dy0 + headH, oB = dy0 + dH - thrH;
            const float ctr = (oL + oR) * 0.5f, halfW = (oR - oL) * 0.5f, gap = open * halfW;
            // header lintel (continuous with the wall caps from Phase 1) + underside shadow
            dl->AddRectFilled(ImVec2(dx0, dy0), ImVec2(dx0 + dW, dy0 + headH), capCol);
            dl->AddRectFilled(ImVec2(dx0, dy0 + headH - 2), ImVec2(dx0 + dW, dy0 + headH), shade(capCol, 0.42f));
            // jamb posts
            dl->AddRectFilled(ImVec2(dx0, dy0), ImVec2(dx0 + postW, dy0 + dH), frameC);
            dl->AddRectFilled(ImVec2(dx0 + dW - postW, dy0), ImVec2(dx0 + dW, dy0 + dH), frameC);
            dl->AddRectFilled(ImVec2(dx0, dy0), ImVec2(dx0 + 2, dy0 + dH), frameHi);
            dl->AddRectFilled(ImVec2(dx0 + dW - 2, dy0), ImVec2(dx0 + dW, dy0 + dH), shade(frameC, 0.6f));
            // two leaves, clipped to the opening so they vanish into the jambs when retracted
            dl->PushClipRect(ImVec2(oL, oT), ImVec2(oR, oB), true);
            dl->AddRectFilled(ImVec2(oL, oT), ImVec2(oR, oT + 3), IM_COL32(0, 0, 0, 90));   // pocket shadow under the lintel
            auto leaf = [&](float lx0, float lx1, bool isLeft) {
                dl->AddRectFilled(ImVec2(lx0, oT), ImVec2(lx1, oB), leafC);
                dl->AddRectFilled(ImVec2(lx0, oT), ImVec2(lx1, oT + 3), leafHi);
                dl->AddRectFilled(ImVec2(lx0, oB - 3), ImVec2(lx1, oB), leafLo);
                dl->AddRectFilled(ImVec2(lx0 + 3, oT + 5), ImVec2(lx1 - 3, oB - 5), shade(leafC, 0.84f));   // inset panel
                for (float yy = oT + 8; yy < oB - 6; yy += 8) { dl->AddRectFilled(ImVec2(lx0 + 3, yy), ImVec2(lx1 - 3, yy + 1), IM_COL32(0, 0, 0, 55)); }
                const float me = isLeft ? lx1 - 2 : lx0;                                    // bright meeting edge
                dl->AddRectFilled(ImVec2(me, oT), ImVec2(me + 2, oB), shade(leafC, 1.32f));
            };
            leaf(ctr - halfW - gap, ctr - gap, true);
            leaf(ctr + gap, ctr + halfW + gap, false);
            dl->PopClipRect();
            // threshold plate + light strip that brightens as the door opens
            const ImU32 accentLine = console ? IM_COL32(240, 200, 90, 255) : IM_COL32(90, 200, 230, 255);
            dl->AddRectFilled(ImVec2(oL, oB), ImVec2(oR, dy0 + dH), IM_COL32(26, 30, 40, 255));
            dl->AddRectFilled(ImVec2(oL + 2, dy0 + dH - 3), ImVec2(oR - 2, dy0 + dH - 1), mix(shade(accentLine, 0.4f), accentLine, open));
            // status light on the header: red shut → green open, with a faint glow
            const ImU32 lightCol = mix(IM_COL32(222, 72, 60, 255), IM_COL32(96, 232, 124, 255), open);
            const float lcx = dx0 + dW * 0.5f, lcy = dy0 + headH * 0.45f;
            glow(dl, ImVec2(lcx, lcy + 1), kTile * 0.42f, lightCol, 38);
            dl->AddRectFilled(ImVec2(lcx - 7, lcy - 3), ImVec2(lcx + 7, lcy + 4), shade(lightCol, 0.35f));
            dl->AddRectFilled(ImVec2(lcx - 4, lcy - 2), ImVec2(lcx + 4, lcy + 3), lightCol);
            // frame outline + AO the doorway casts on the floor below
            dl->AddRectFilled(ImVec2(dx0, dy0 + dH), ImVec2(dx0 + dW, dy0 + dH + 4), IM_COL32(0, 0, 0, 55));
            dl->AddRect(ImVec2(dx0, dy0), ImVec2(dx0 + dW, dy0 + dH), IM_COL32(0, 0, 0, 140), 0, 0, 1.5f);
        }
        if (r.panel >= 0) {     // console terminal
            const float cyoff = r.doorNorth ? 0.85f : -1.75f;
            const float cx = sx(static_cast<float>(r.doorx) - 0.15f), cy = sy(static_cast<float>(r.doory) + cyoff);
            glow(dl, ImVec2(cx + kTile * 0.67f, cy + kTile * 0.45f), kTile * 1.1f, IM_COL32(90, 210, 225, 255), 44);
            dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + kTile * 1.35f, cy + kTile * 0.95f), IM_COL32(40, 56, 70, 255));
            dl->AddRectFilled(ImVec2(cx + 3, cy + 3), ImVec2(cx + kTile * 1.35f - 3, cy + kTile * 0.5f), IM_COL32(70, 190, 205, 235));
            dl->AddRectFilled(ImVec2(cx + 5, cy + kTile * 0.2f), ImVec2(cx + kTile * 1.35f - 5, cy + kTile * 0.2f + 2), IM_COL32(150, 240, 245, 200));
        }
        dl->AddText(ImVec2(sx(static_cast<float>(r.x0) + 0.6f) + 1, sy(static_cast<float>(r.y0) + 0.3f) + 1), IM_COL32(0, 0, 0, 160), r.name.c_str());
        dl->AddText(ImVec2(sx(static_cast<float>(r.x0) + 0.6f), sy(static_cast<float>(r.y0) + 0.3f)), r.labelCol, r.name.c_str());
        if (r.isVoid && g.voidDiscovered) { dl->AddText(ImVec2(sx((r.x0 + r.x1) * 0.5f - 2.0f), sy((r.y0 + r.y1) * 0.5f)), IM_COL32(205, 130, 235, 255), "* void seed *"); }
    }
    // elevator (2-tone lift)
    {
        const ImVec2 ea(sx(static_cast<float>(g.elevX)), sy(static_cast<float>(g.elevY))), eb(sx(static_cast<float>(g.elevX) + 1), sy(static_cast<float>(g.elevY) + 2));
        const float emid = (ea.y + eb.y) * 0.5f;
        glow(dl, ImVec2((ea.x + eb.x) * 0.5f, emid), kTile * 1.3f, IM_COL32(240, 214, 120, 255), 40);
        dl->AddRectFilled(ea, ImVec2(eb.x, emid), IM_COL32(90, 80, 42, 255));
        dl->AddRectFilled(ImVec2(ea.x, emid), eb, IM_COL32(50, 44, 24, 255));
        dl->AddRect(ea, eb, IM_COL32(240, 220, 120, 255), 0, 0, 2.0f);
        dl->AddRectFilled(ImVec2((ea.x + eb.x) * 0.5f - 1, ea.y + 2), ImVec2((ea.x + eb.x) * 0.5f + 1, eb.y - 2), IM_COL32(200, 178, 96, 255));
        dl->AddText(ImVec2(ea.x - 4, ea.y - kTile * 0.9f), IM_COL32(240, 220, 120, 255), "LIFT");
    }
    // crew (NPCs) with idle bob + name tag when near
    const float tnow = g.simTime;
    for (const WorldNpc& n : g.npcsHere) {
        if (n.x < x0 - 1 || n.x > x1 + 1 || n.y < y0 - 1 || n.y > y1 + 1) { continue; }
        const float bob = std::sin(tnow * 2.2f + n.phase) * 0.05f;
        draw_character(dl, ImVec2(sx(n.x), sy(n.y + bob)), kTile, n.color, n.fx, n.fy, false, n.walk, n.hair, n.skin);
        if (std::max(std::fabs(n.x - g.pcx), std::fabs(n.y - g.pcy)) <= 3.0f) {
            const float tw = ImGui::CalcTextSize(n.name.c_str()).x;
            const ImVec2 tp(std::floor(sx(n.x) - tw * 0.5f), std::floor(sy(n.y) - kTile * 1.05f));
            dl->AddRectFilled(ImVec2(tp.x - 4, tp.y - 1), ImVec2(tp.x + tw + 4, tp.y + 15), IM_COL32(14, 17, 24, 210));
            dl->AddText(tp, IM_COL32(230, 235, 245, 255), n.name.c_str());
        }
    }
    // player
    draw_character(dl, ImVec2(sx(g.pcx), sy(g.pcy)), kTile, IM_COL32(96, 174, 214, 255), g.fx, g.fy, true, g.playerWalk, IM_COL32(60, 44, 72, 255), IM_COL32(238, 206, 170, 255));
    const ImVec2 pc(sx(g.pcx), sy(g.pcy));
    const int s = station_at_player();
    if (near_elevator()) { dl->AddText(ImVec2(pc.x - 36, pc.y - kTile * 1.35f), IM_COL32(255, 240, 160, 255), "[E] Elevator"); }
    else if (s >= 0) { const std::string t = "[E] " + g.rooms[static_cast<std::size_t>(s)].name; dl->AddText(ImVec2(pc.x - 34, pc.y - kTile * 1.35f), IM_COL32(255, 240, 160, 255), t.c_str()); }

    // ── Phase 6: atmosphere — drifting dust motes, vignette, deck-change fade ──
    const float vw = sz.x, vh = sz.y, wt = p0.y + hud, tt = g.simTime;
    for (int i = 0; i < 48; ++i) {                                   // floating dust motes (screen-space)
        unsigned hh = vbhash(static_cast<unsigned>(i) * 2654435761U);
        const float bx = static_cast<float>(hh % 1000) / 1000.0f; hh = vbhash(hh);
        const float by = static_cast<float>(hh % 1000) / 1000.0f; hh = vbhash(hh);
        const float spd = 5.0f + static_cast<float>(hh % 100) * 0.18f;
        const float mx = p0.x + std::fmod(bx * vw + tt * spd, vw);
        const float my = wt + std::fmod(by * (vh - hud) + tt * spd * 0.35f, vh - hud);
        dl->AddRectFilled(ImVec2(mx, my), ImVec2(mx + 2, my + 2), IM_COL32(200, 215, 235, 18 + static_cast<int>(hh % 26)));
    }
    const ImU32 vd = IM_COL32(0, 0, 0, 105), vt = IM_COL32(0, 0, 0, 0);   // vignette
    const float vb = 86.0f;
    dl->AddRectFilledMultiColor(ImVec2(p0.x, wt), ImVec2(p0.x + vw, wt + vb), vd, vd, vt, vt);
    dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y + vh - vb), ImVec2(p0.x + vw, p0.y + vh), vt, vt, vd, vd);
    dl->AddRectFilledMultiColor(ImVec2(p0.x, wt), ImVec2(p0.x + vb, p0.y + vh), vd, vt, vt, vd);
    dl->AddRectFilledMultiColor(ImVec2(p0.x + vw - vb, wt), ImVec2(p0.x + vw, p0.y + vh), vt, vd, vd, vt);
    if (g.deckFade > 0.001f) { dl->AddRectFilled(ImVec2(p0.x, wt), ImVec2(p0.x + vw, p0.y + vh), IM_COL32(4, 6, 12, static_cast<int>(g.deckFade * 230.0f))); }

    dl->Flags = saved;   // restore AA for the HUD/panels
}

void draw_hud() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 90));
    ImGui::Begin("##hud", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.90f, 1.0f, 1.0f));
    ImGui::TextUnformatted("VOIDBORNE :: QINGNIAO");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("   Day %d   %02d:00   %s   speed %s",
                g.day, g.hour, g.paused ? "[PAUSED]" : "",
                g.speedIndex == 0 ? "1x" : g.speedIndex == 1 ? "2x" : "4x");
    ImGui::SameLine(); if (ImGui::SmallButton(g.paused ? "Resume" : "Pause")) { g.paused = !g.paused; }
    ImGui::SameLine(); if (ImGui::SmallButton("Speed")) { g.speedIndex = (g.speedIndex + 1) % 3; }

    auto chip = [&](const char* n, float v, ImVec4 col) {
        ImGui::SameLine();
        ImGui::TextColored(col, "%s", n); ImGui::SameLine(0, 4); ImGui::Text("%.0f", v);
    };
    ImGui::Text(" "); ImGui::SameLine(0, 0);
    chip("PWR", g.res.power, ImVec4(1.0f, 0.85f, 0.3f, 1));
    chip("H2O", g.res.water, ImVec4(0.3f, 0.6f, 1.0f, 1));
    chip("O2", g.res.oxygen, ImVec4(0.5f, 0.9f, 0.7f, 1));
    chip("CO2", g.res.co2 * 100, ImVec4(0.75f, 0.75f, 0.75f, 1));
    chip("FOOD", g.res.food, ImVec4(0.85f, 0.78f, 0.45f, 1));
    chip("PARTS", g.res.parts, ImVec4(0.72f, 0.52f, 0.40f, 1));
    chip("MED", g.res.medicine, ImVec4(0.9f, 0.5f, 0.5f, 1));
    ImGui::SameLine();
    ImGui::TextColored(g.morale < 35 ? ImVec4(0.95f, 0.5f, 0.4f, 1) : ImVec4(0.6f, 0.85f, 0.7f, 1),
                       "  MORALE %.0f%%", g.morale);
    ImGui::SameLine();
    ImGui::TextDisabled("  voyage %.0f%%  gen %d  ev %d", g.lyTotal > 0 ? g.lyTravelled / g.lyTotal * 100 : 0, g.generation, g.eventsResolved);
    if (g.voidDiscovered) {
        ImGui::SameLine(); ImGui::TextColored(ImVec4(0.72f, 0.5f, 0.92f, 1), "  VOID %.0f%%", g.voidStage);
    }
    ImGui::TextDisabled(tr("DECK %s   ·   WASD walk · E lift/console · at lift 1-6 = deck · panels C/G/L/F/R/N/K · F5/F9 save · F2 \xE4\xB8\xAD/EN · Space/TAB time",
                           "\xE7\x94\xB2\xE6\x9D\xBF %s   ·   WASD \xE8\xA1\x8C\xE8\xB5\xB0 · E \xE7\x94\xB5\xE6\xA2\xAF/\xE7\xBB\x88\xE7\xAB\xAF · \xE7\x94\xB5\xE6\xA2\xAF\xE5\x86\x85 1-6 \xE9\x80\x89\xE5\xB1\x82 · \xE9\x9D\xA2\xE6\x9D\xBF C/G/L/F/R/N/K · F5/F9 \xE5\xAD\x98\xE8\xAF\xBB · F2 \xE4\xB8\xAD/EN · \xE7\xA9\xBA\xE6\xA0\xBC/TAB \xE6\x97\xB6\xE9\x97\xB4"),
                        g.decks.empty() ? "-" : g.decks[static_cast<std::size_t>(g.curDeck)].label.c_str());
    ImGui::End();
}

void draw_bay_panel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720, 640), ImGuiCond_Always);
    ImGui::Begin("ECOLOGY BAY", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    ImGui::TextDisabled("Department S5 · keep the ship fed & breathing.  [E]/[Esc] leave");
    ImGui::Separator();

    // environment controls (affect yield via closeness-to-ideal)
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.7f, 1), "ENVIRONMENT");
    ImGui::SliderFloat("Temp \xC2\xB0""C", &g.bayTemp, 10, 34, "%.0f");
    ImGui::SliderFloat("Humidity %", &g.bayHumidity, 30, 95, "%.0f");
    ImGui::SliderFloat("Light", &g.bayLight, 0.0f, 1.2f, "%.2f");
    ImGui::Separator();

    // seed selector
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.7f, 1), "SEED");
    for (int i = 0; i < static_cast<int>(g.cropDefs.size()); ++i) {
        if (i) { ImGui::SameLine(); }
        const bool sel = g.selCrop == i;
        ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4(0.32f, 0.5f, 0.34f, 1) : ImVec4(0.18f, 0.22f, 0.26f, 1));
        if (ImGui::Button(g.cropDefs[static_cast<std::size_t>(i)].name.c_str())) { g.selCrop = i; }
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    // plot grid
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.7f, 1), "PLOTS  (click to plant/water/harvest)");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < kPlots; ++i) {
        if (i % 4) { ImGui::SameLine(); }
        ImGui::PushID(i);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float cell = 150.0f, h = 110.0f;
        ImGui::InvisibleButton("plot", ImVec2(cell, h));
        const bool hov = ImGui::IsItemHovered();
        Plot& pl = g.plots[static_cast<std::size_t>(i)];
        ImU32 bg = pl.crop < 0 ? IM_COL32(54, 42, 30, 255) : IM_COL32(40, 60, 40, 255);
        if (pl.watered) { bg = IM_COL32(34, 48, 36, 255); }
        dl->AddRectFilled(p0, ImVec2(p0.x + cell, p0.y + h), bg, 4);
        dl->AddRect(p0, ImVec2(p0.x + cell, p0.y + h), hov ? IM_COL32(200, 220, 160, 255) : IM_COL32(80, 96, 80, 255), 4);
        if (pl.crop >= 0) {
            const CropDef& c = g.cropDefs[static_cast<std::size_t>(pl.crop)];
            const float frac = std::clamp(pl.growth / std::max(1.0f, static_cast<float>(c.growthDays)), 0.0f, 1.0f);
            const float r = 10 + 26 * frac;
            dl->AddCircleFilled(ImVec2(p0.x + cell * 0.5f, p0.y + 52), r, c.color);
            if (pl.ripe) { dl->AddText(ImVec2(p0.x + 8, p0.y + 6), IM_COL32(255, 240, 140, 255), "RIPE!"); }
            dl->AddText(ImVec2(p0.x + 8, p0.y + h - 20), IM_COL32(220, 230, 220, 255), c.name.c_str());
            char buf[32]; std::snprintf(buf, sizeof(buf), "%.0f%%", frac * 100);
            dl->AddText(ImVec2(p0.x + cell - 44, p0.y + 6), IM_COL32(200, 220, 200, 255), buf);
        } else {
            dl->AddText(ImVec2(p0.x + 8, p0.y + h - 20), IM_COL32(150, 130, 110, 255), "empty soil");
        }
        if (ImGui::IsItemClicked()) {
            if (pl.crop < 0) { pl.crop = g.selCrop; pl.growth = 0; pl.ripe = false; pl.watered = true; }
            else if (pl.ripe) { harvest_plot(i); }
            else { pl.watered = true; }
        }
        ImGui::PopID();
    }

    ImGui::Spacing(); ImGui::Separator();
    ImGui::Text("Harvested this voyage: %d crops  (%.0f food-equiv)   |   bay output last day: food %+.1f, O2 %+.2f",
                g.totalHarvested, g.totalFoodHarvested, g.lastFoodDelta, g.lastO2Delta);
    ImGui::TextDisabled("Plant a seed, water daily, and let the days tick (top-bar speed). Click RIPE plots to harvest.");
    ImGui::End();
}

// ── M4: event modal (S13) + crew manifest (S10) ──
void draw_event_modal() {
    if (g.curEvent < 0) { return; }
    const EventDef& e = g.events[static_cast<std::size_t>(g.curEvent)];
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_Always);
    ImGui::Begin(tr("SHIP EVENT", "舰内事件"), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.5f, 1));
    ImGui::TextWrapped("%s", e.title.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::TextWrapped("%s", e.body.c_str());
    ImGui::Spacing(); ImGui::Separator();
    for (int i = 0; i < static_cast<int>(e.options.size()); ++i) {
        const EventOption& o = e.options[static_cast<std::size_t>(i)];
        bool locked = false;
        if (!o.requireRes.empty()) { float* p = res_ptr(o.requireRes); if (p && *p < static_cast<float>(o.requireAmt)) { locked = true; } }
        if (locked) { ImGui::BeginDisabled(); }
        std::string lbl = std::to_string(i + 1) + ".  " + o.label;
        if (!o.requireRes.empty()) { lbl += tr("   (needs ", "   （需 ") + std::to_string(o.requireAmt) + " " + o.requireRes + tr(")", "）"); }
        if (ImGui::Button(lbl.c_str(), ImVec2(-1, 0))) { resolve_event(i); }
        if (locked) { ImGui::EndDisabled(); }
    }
    ImGui::TextDisabled(tr("Press 1-%d to choose.   Time is paused.", "按 1-%d 选择。   时间已暂停。"), static_cast<int>(e.options.size()));
    ImGui::End();
}
void draw_crew_panel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(700, 520), ImGuiCond_Always);
    ImGui::Begin("CREW MANIFEST", &g.inCrew, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::Text("Souls aboard %d   ·   ship morale %.0f%%   ·   generation %d   ·   you: %s (T%d %s)",
                kPop, g.morale, g.generation, g.playerRole.c_str(), g.playerTier, g.playerDept.c_str());
    ImGui::TextDisabled("[C]/[Esc] close");
    ImGui::Separator();
    if (ImGui::BeginTable("crew", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("Role"); ImGui::TableSetupColumn("Dept");
        ImGui::TableSetupColumn("Tier"); ImGui::TableSetupColumn("Skill / Loyalty");
        ImGui::TableHeadersRow();
        for (auto& c : g.crew) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(c.name.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(c.role.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(c.department.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::Text("T%d", c.tier);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%d / %d", c.skill, c.loyalty);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ── M5 department panels + M6 meta panels ──
void panel_begin(const char* title, bool* open, float w, float h) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin(title, open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
}
void draw_eng_panel() {
    panel_begin("ENGINEERING  ::  S6 power grid", &g.inEng, 640, 440);
    ImGui::TextDisabled("Allocate the reactor across five sinks (auto-normalized). [G]/[Esc] close");
    ImGui::Text("Reactor output: %.0f", g.res.power);
    ImGui::Separator();
    const char* names[5] = {"Ecology light", "Manufacturing", "Research", "Life support", "Reserve"};
    for (int i = 0; i < 5; ++i) { ImGui::SliderFloat(names[i], &g.powerAlloc[i], 0.0f, 1.0f, "%.2f"); }
    float sum = 0; for (float a : g.powerAlloc) { sum += a; }
    if (sum > 0) { for (float& a : g.powerAlloc) { a /= sum; } }
    ImGui::Separator();
    ImGui::Text("Effective factors:  eco %.2f   mfg %.2f   sci %.2f   life %.2f",
                power_factor(0), power_factor(1), power_factor(2), power_factor(3));
    if (ImGui::Button("Emergency repair  (-10 parts -> +20 power)") && g.res.parts >= 10) {
        g.res.parts -= 10; g.res.power = std::min(300.0f, g.res.power + 20);
    }
    ImGui::End();
}
void draw_log_panel() {
    panel_begin("LOGISTICS  ::  S7 trade & rationing", &g.inLog, 700, 480);
    ImGui::TextDisabled("[L]/[Esc] close");
    ImGui::Text("Rationing policy:"); ImGui::SameLine();
    if (ImGui::RadioButton("Short", g.ration == 0)) { g.ration = 0; } ImGui::SameLine();
    if (ImGui::RadioButton("Normal", g.ration == 1)) { g.ration = 1; } ImGui::SameLine();
    if (ImGui::RadioButton("Generous", g.ration == 2)) { g.ration = 2; }
    ImGui::Separator();
    ImGui::Text("Open trade windows (day %d):", g.day);
    for (int i = 0; i < static_cast<int>(g.offers.size()); ++i) {
        const TradeOffer& o = g.offers[static_cast<std::size_t>(i)];
        if (g.day < o.minDay) { continue; }
        ImGui::PushID(i);
        ImGui::BulletText("%s", o.name.c_str());
        std::string s;
        if (o.foodCost) { s += "food-" + std::to_string(o.foodCost) + " "; }
        if (o.rawCost) { s += "raw-" + std::to_string(o.rawCost) + " "; }
        s += "=> ";
        if (o.medicineGain) { s += "med+" + std::to_string(o.medicineGain) + " "; }
        if (o.partsGain) { s += "parts+" + std::to_string(o.partsGain) + " "; }
        if (o.waterGain) { s += "water+" + std::to_string(o.waterGain) + " "; }
        ImGui::SameLine(); ImGui::TextDisabled("%s", s.c_str());
        ImGui::SameLine();
        const bool afford = g.res.food >= o.foodCost && g.res.rawMaterials >= o.rawCost;
        if (!afford) { ImGui::BeginDisabled(); }
        if (ImGui::SmallButton("Accept")) {
            g.res.food -= o.foodCost; g.res.rawMaterials -= o.rawCost;
            g.res.medicine += o.medicineGain; g.res.parts += o.partsGain; g.res.water += o.waterGain;
        }
        if (!afford) { ImGui::EndDisabled(); }
        ImGui::PopID();
    }
    ImGui::End();
}
void draw_mfg_panel() {
    panel_begin("MANUFACTURING  ::  S8 lathe queue", &g.inMfg, 720, 480);
    ImGui::TextDisabled("Click a recipe to enqueue (consumes raw). [F]/[Esc] close");
    ImGui::Text("Raw %.0f   Parts %.0f", g.res.rawMaterials, g.res.parts);
    ImGui::Separator();
    ImGui::Columns(2, "mfg", true);
    ImGui::TextDisabled("RECIPES");
    for (int i = 0; i < static_cast<int>(g.recipes.size()); ++i) {
        const RecipeDef& r = g.recipes[static_cast<std::size_t>(i)];
        const bool techok = r.requiresTech.empty() || g.tech.count(r.requiresTech) > 0;
        const bool afford = g.res.rawMaterials >= static_cast<float>(r.rawCost);
        if (!(techok && afford)) { ImGui::BeginDisabled(); }
        const std::string lbl = r.name + "  (raw " + std::to_string(r.rawCost) + ", " + std::to_string(r.days) +
                                "d -> " + std::to_string(r.partsOut) + "p)";
        if (ImGui::Button(lbl.c_str(), ImVec2(-1, 0))) {
            for (int q = 0; q < 4; ++q) {
                if (g.mfgQueue[q] < 0) { g.mfgQueue[q] = i; g.mfgProgress[q] = 0; g.res.rawMaterials -= r.rawCost; break; }
            }
        }
        if (!(techok && afford)) { ImGui::EndDisabled(); }
    }
    ImGui::NextColumn();
    ImGui::TextDisabled("QUEUE");
    for (int q = 0; q < 4; ++q) {
        if (g.mfgQueue[q] < 0) { ImGui::Text("slot %d: idle", q + 1); continue; }
        const RecipeDef& r = g.recipes[static_cast<std::size_t>(g.mfgQueue[q])];
        ImGui::TextUnformatted(r.name.c_str());
        ImGui::ProgressBar(g.mfgProgress[q] / std::max(1, r.days), ImVec2(-1, 0));
    }
    ImGui::Columns(1);
    ImGui::End();
}
void draw_sci_panel() {
    panel_begin("RESEARCH  ::  S9/S12 tech tree", &g.inSci, 720, 540);
    ImGui::TextDisabled("Start a hypothesis; success unlocks a tech + a line point. [R]/[Esc] close");
    const char* lines[5] = {"Survival", "Engineering", "Social", "Manufacturing", "Frontier"};
    for (int i = 0; i < 5; ++i) { if (i) { ImGui::SameLine(); } ImGui::Text("%s:%d", lines[i], g.techPoints[i]); }
    ImGui::Separator();
    if (g.sciActive >= 0) {
        const ResearchDef& p = g.research[static_cast<std::size_t>(g.sciActive)];
        ImGui::Text("Active: %s", p.name.c_str());
        ImGui::ProgressBar(g.sciProgress / std::max(1, p.days), ImVec2(-1, 0));
    } else { ImGui::TextDisabled("No active project."); }
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(g.research.size()); ++i) {
        const ResearchDef& p = g.research[static_cast<std::size_t>(i)];
        const bool done = !p.unlockTech.empty() && g.tech.count(p.unlockTech) > 0;
        ImGui::PushID(i);
        if (done) { ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "[done] %s (%s)", p.name.c_str(), p.line.c_str()); }
        else {
            const bool busy = g.sciActive >= 0;
            if (busy) { ImGui::BeginDisabled(); }
            const std::string lbl = p.name + "  [" + p.line + ", " + std::to_string(p.days) + "d, fail " +
                                    std::to_string(static_cast<int>(p.failRate * 100)) + "%]";
            if (ImGui::Button(lbl.c_str(), ImVec2(-1, 0))) { g.sciActive = i; g.sciProgress = 0; }
            if (busy) { ImGui::EndDisabled(); }
        }
        ImGui::PopID();
    }
    ImGui::End();
}
void draw_starmap_panel() {
    panel_begin("NAVIGATION  ::  S11 star map", &g.inStarmap, 720, 560);
    ImGui::TextDisabled("[N]/[Esc] close");
    ImGui::Text("Voyage  %.1f / %.0f ly   ·   generation %d   ·   day %d", g.lyTravelled, g.lyTotal, g.generation, g.day);
    ImGui::ProgressBar(g.lyTotal > 0 ? g.lyTravelled / g.lyTotal : 0.0f, ImVec2(-1, 0));
    ImGui::Separator();
    ImGui::Text("Preferred next route (the crew weighs in):");
    for (int i = 0; i < static_cast<int>(g.routes.size()); ++i) {
        const RouteDef& r = g.routes[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const bool sel = g.routeChosen == i;
        ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4(0.30f, 0.50f, 0.34f, 1) : ImVec4(0.16f, 0.20f, 0.24f, 1));
        const std::string lbl = r.name + "   [" + r.risk + " risk, " + std::to_string(r.days) + "d]";
        if (ImGui::Button(lbl.c_str(), ImVec2(-1, 0))) { g.routeChosen = i; }
        ImGui::PopStyleColor();
        ImGui::TextDisabled("   %s", r.desc.c_str());
        ImGui::PopID();
    }
    if (g.voidDiscovered) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.82f, 0.60f, 1), tr("THE LONG MEMORY  ::  awakening %.0f%%", "漫长记忆  ::  苏醒 %.0f%%"), g.voidStage);
        ImGui::TextWrapped("%s", tr("In Bay G the oldest vine warms to a soft gold, and the air carries a tune no one is playing. The ship is starting to remember its dead. How will you hold it?",
                                    "G 区最老的藤蔓泛起柔和的金光，空气里飘着无人弹奏的旋律。飞船开始记起逝去的人。你将如何对待它？"));
        const char* st[5] = { tr("Wake", "唤醒"), tr("Guide", "引导"), tr("Witness", "见证"), tr("Contain", "守界"), tr("Silence", "静默") };
        for (int i = 0; i < 5; ++i) { if (i) { ImGui::SameLine(); } if (ImGui::RadioButton(st[i], g.voidStance == i)) { g.voidStance = i; } }
    }
    ImGui::End();
}
void draw_captain_panel() {
    panel_begin("COMMAND  ::  S16 election & budget", &g.inCaptain, 660, 460);
    ImGui::TextDisabled("[K]/[Esc] close");
    if (!g.isCaptain) {
        ImGui::Text("You: %s  —  Tier %d  (%s)", g.playerRole.c_str(), g.playerTier, g.playerDept.c_str());
        ImGui::TextWrapped("Reach Tier 3 in your department, then stand for Captain. The crew votes by loyalty, lean and ship morale.");
        ImGui::Text("Projected yes-vote: %.0f%%", election_yes_share() * 100);
        if (g.ranForCaptain && !g.electionWon) { ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1), "The last election did not go your way."); }
        const bool can = g.playerTier >= 3 && !g.ranForCaptain;
        if (!can) { ImGui::BeginDisabled(); }
        if (ImGui::Button("Stand for Captain")) { run_for_captain(); }
        if (!can) { ImGui::EndDisabled(); }
        if (g.playerTier < 3) { ImGui::TextDisabled("(need Tier 3 — gain skill via events / your department)"); }
    } else {
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "You command the Qingniao.");
        ImGui::TextDisabled("Allocate the ship budget across departments (auto-normalized):");
        const char* dnm[5] = {"Ecology", "Engineering", "Logistics", "Manufacturing", "Research"};
        for (int i = 0; i < 5; ++i) { ImGui::SliderFloat(dnm[i], &g.captainBudget[i], 0.0f, 1.0f, "%.2f"); }
        float s = 0; for (float b : g.captainBudget) { s += b; }
        if (s > 0) { for (float& b : g.captainBudget) { b /= s; } }
    }
    ImGui::End();
}
void draw_ending_panel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(4, 4, 8, 235));
    panel_begin(tr("END OF VOYAGE", "航程终点"), nullptr, 740, 300);
    ImGui::Dummy(ImVec2(0, 24));
    const char* nm = ending_name(g.ending);
    ImGui::SetCursorPosX((740 - ImGui::CalcTextSize(nm).x) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.92f, 1.0f, 1));
    ImGui::TextUnformatted(nm);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 18));
    ImGui::TextWrapped(tr("Ending %d of 7.   Day %d, generation %d.   Morale %.0f%%, awakening %.0f%%.   %s",
                          "结局 %d / 7    第 %d 天 · 第 %d 代    士气 %.0f%%，苏醒 %.0f%%。   %s"),
                       g.ending + 1, g.day, g.generation, g.morale, g.voidStage,
                       g.isCaptain ? tr("You led them to New Shore.", "你带领他们抵达新岸。")
                                   : tr("You served, and the ship sailed on.", "你尽了本分，飞船继续航行。"));
    ImGui::Dummy(ImVec2(0, 14));
    ImGui::TextDisabled("%s", tr("All 7 endings are wired. F9 reloads a save; or restart for a new voyage.",
                                 "七种结局均已实装。F9 读取存档，或重启开始新的航程。"));
    ImGui::End();
}

// ── M7: save / load (S/D1) — JSON snapshot of the session state ──
void save_game() {
    rapidjson::StringBuffer sb; rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    w.StartObject();
    w.Key("day"); w.Int(g.day); w.Key("hour"); w.Int(g.hour);
    w.Key("food"); w.Double(g.res.food); w.Key("water"); w.Double(g.res.water);
    w.Key("power"); w.Double(g.res.power); w.Key("oxygen"); w.Double(g.res.oxygen);
    w.Key("co2"); w.Double(g.res.co2); w.Key("parts"); w.Double(g.res.parts);
    w.Key("medicine"); w.Double(g.res.medicine); w.Key("raw"); w.Double(g.res.rawMaterials);
    w.Key("morale"); w.Double(g.morale);
    w.Key("skill"); w.Int(g.playerSkill); w.Key("tier"); w.Int(g.playerTier);
    w.Key("ration"); w.Int(g.ration); w.Key("void"); w.Double(g.voidStage);
    w.Key("voidStance"); w.Int(g.voidStance); w.Key("voidDisc"); w.Bool(g.voidDiscovered);
    w.Key("captain"); w.Bool(g.isCaptain); w.Key("gen"); w.Int(g.generation);
    w.Key("ly"); w.Double(g.lyTravelled); w.Key("route"); w.Int(g.routeChosen);
    w.Key("events"); w.Int(g.eventsResolved); w.Key("harvested"); w.Int(g.totalHarvested);
    w.Key("tp"); w.StartArray(); for (int t : g.techPoints) { w.Int(t); } w.EndArray();
    w.EndObject();
    std::ofstream f("voidborne_save.json"); f << sb.GetString();
}
bool load_game() {
    const std::string txt = read_file("voidborne_save.json");
    if (txt.empty()) { return false; }
    rapidjson::Document d; d.Parse(txt.c_str());
    if (d.HasParseError()) { return false; }
    auto gi = [&](const char* k, int def) { return (d.HasMember(k) && d[k].IsNumber()) ? d[k].GetInt() : def; };
    auto gf = [&](const char* k, float def) { return (d.HasMember(k) && d[k].IsNumber()) ? d[k].GetFloat() : def; };
    auto gb = [&](const char* k, bool def) { return (d.HasMember(k) && d[k].IsBool()) ? d[k].GetBool() : def; };
    g.day = gi("day", 1); g.hour = gi("hour", 6);
    g.res.food = gf("food", 600); g.res.water = gf("water", 1200); g.res.power = gf("power", 100);
    g.res.oxygen = gf("oxygen", 21); g.res.co2 = gf("co2", 0.4f); g.res.parts = gf("parts", 50);
    g.res.medicine = gf("medicine", 20); g.res.rawMaterials = gf("raw", 30);
    g.morale = gf("morale", 70); g.playerSkill = gi("skill", 20); g.playerTier = gi("tier", 1);
    g.ration = gi("ration", 1); g.voidStage = gf("void", 0); g.voidStance = gi("voidStance", -1);
    g.voidDiscovered = gb("voidDisc", false); g.isCaptain = gb("captain", false);
    g.generation = gi("gen", 1); g.lyTravelled = gf("ly", 0); g.routeChosen = gi("route", 0);
    g.eventsResolved = gi("events", 0); g.totalHarvested = gi("harvested", 0);
    if (d.HasMember("tp") && d["tp"].IsArray()) { int i = 0; for (auto& t : d["tp"].GetArray()) { if (i < 5 && t.IsNumber()) { g.techPoints[i++] = t.GetInt(); } } }
    if (g.isCaptain) { g.playerRole = "Captain"; }
    return true;
}

// ── input + frame ───────────────────────────────────────────────────────────────
void close_all_panels() {
    g.inBay = g.inCrew = g.inEng = g.inLog = g.inMfg = g.inSci = g.inStarmap = g.inCaptain = false;
}
bool any_panel_open() {
    return g.inBay || g.inCrew || g.inEng || g.inLog || g.inMfg || g.inSci || g.inStarmap || g.inCaptain;
}
void handle_input(float dt) {
    if (g.curEvent >= 0) {   // event modal: number keys pick an option
        for (int i = 0; i < 9; ++i) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false)) { resolve_event(i); break; }
        }
        return;
    }
    if (g.elevatorOpen) {    // deck-select: 1-6 travel, E/Esc cancel
        for (int i = 0; i < 6; ++i) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false)) { build_deck(i); g.elevatorOpen = false; break; }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { g.elevatorOpen = false; }
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) { g.paused = !g.paused; }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) { g.speedIndex = (g.speedIndex + 1) % 3; }
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) { save_game(); }
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) { load_game(); }
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) { g.lang = (g.lang + 1) % 2; reload_events(); }   // EN <-> 中文
    // one management panel at a time; the key toggles it
    auto toggle = [&](bool& flag) { const bool was = flag; close_all_panels(); flag = !was; };
    if (ImGui::IsKeyPressed(ImGuiKey_C, false)) { toggle(g.inCrew); }
    if (ImGui::IsKeyPressed(ImGuiKey_G, false)) { toggle(g.inEng); }
    if (ImGui::IsKeyPressed(ImGuiKey_L, false)) { toggle(g.inLog); }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) { toggle(g.inMfg); }
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { toggle(g.inSci); }
    if (ImGui::IsKeyPressed(ImGuiKey_N, false)) { toggle(g.inStarmap); }
    if (ImGui::IsKeyPressed(ImGuiKey_K, false)) { toggle(g.inCaptain); }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { close_all_panels(); }

    if (any_panel_open()) {
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { close_all_panels(); }
        return;   // panels are modal-ish: no walking while one is open
    }
    float dcol = 0, drow = 0;
    if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) { drow -= 1; }
    if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) { drow += 1; }
    if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) { dcol -= 1; }
    if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) { dcol += 1; }
    move_player(dcol, drow, dt * speed_mul());
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
        if (near_elevator()) { g.elevatorOpen = true; }
        else { const int s = station_at_player(); if (s >= 0) { open_panel(g.rooms[static_cast<std::size_t>(s)].panel); } }
    }
}

// Flat, square, "pixel-terminal" styling on top of UniGUI's theme.
void apply_pixel_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = s.ChildRounding = s.FrameRounding = s.PopupRounding = 0.0f;
    s.GrabRounding = s.ScrollbarRounding = s.TabRounding = 0.0f;
    s.WindowBorderSize = s.FrameBorderSize = 1.0f;
    s.WindowPadding = ImVec2(12, 10);
    s.ItemSpacing = ImVec2(8, 7);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.08f, 0.11f, 0.97f);
    c[ImGuiCol_Border] = ImVec4(0.30f, 0.52f, 0.42f, 0.55f);
    c[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.16f, 0.18f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.16f, 0.22f, 0.24f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.38f, 0.30f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.18f, 0.28f, 0.22f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.72f, 0.55f, 1.0f);
}

// the elevator's deck-select overlay (cross-deck travel)
void draw_elevator_overlay() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Always);
    ImGui::Begin("ELEVATOR", &g.elevatorOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(0.62f, 0.9f, 1.0f, 1), "QINGNIAO -- select a deck");
    ImGui::TextDisabled("press 1-6, or click   ·   [E]/[Esc] cancel");
    ImGui::Separator();
    for (int i = static_cast<int>(g.decks.size()) - 1; i >= 0; --i) {   // top deck (D5) first
        const DeckDef& d = g.decks[static_cast<std::size_t>(i)];
        const bool cur = i == g.curDeck;
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Button, cur ? ImVec4(0.30f, 0.45f, 0.50f, 1) : ImVec4(0.16f, 0.20f, 0.24f, 1));
        const std::string lbl = std::to_string(i + 1) + "   " + d.label + (cur ? "   (you are here)" : "");
        if (ImGui::Button(lbl.c_str(), ImVec2(-1, 0))) { build_deck(i); g.elevatorOpen = false; }
        ImGui::PopStyleColor();
        ImGui::TextDisabled("       %s", d.purpose.c_str());
        ImGui::PopID();
    }
    ImGui::End();
}

void draw_frame() {
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 1.0f / 20.0f);
    g.simTime += dt * speed_mul();   // walk / doors / crew / fx fast-forward with the speed setting
    advance_time(dt);
    handle_input(dt);
    update_doors(dt * speed_mul());
    update_crew(dt);
    g.deckFade = std::max(0.0f, g.deckFade - dt * 2.2f);
    draw_world();
    draw_hud();
    if (g.elevatorOpen) { draw_elevator_overlay(); }
    if (g.inBay) { draw_bay_panel(); }
    if (g.inCrew) { draw_crew_panel(); }
    if (g.inEng) { draw_eng_panel(); }
    if (g.inLog) { draw_log_panel(); }
    if (g.inMfg) { draw_mfg_panel(); }
    if (g.inSci) { draw_sci_panel(); }
    if (g.inStarmap) { draw_starmap_panel(); }
    if (g.inCaptain) { draw_captain_panel(); }
    if (g.ending >= 0) { draw_ending_panel(); }
    if (g.curEvent >= 0) { draw_event_modal(); }
}

// ── headless autodemo: drive every system (M0-M7) and assert each fired ──
void run_autodemo() {
    // M3 ecology: plant + water + grow + harvest 8 plots.
    for (auto& p : g.plots) { p = Plot{}; p.crop = 0; p.watered = true; }
    int harvested = 0;
    for (int d = 0; d < 8; ++d) {
        for (auto& p : g.plots) { if (p.crop >= 0 && !p.ripe) { p.watered = true; } }
        daily_settlement();
        for (int i = 0; i < kPlots; ++i) { if (g.plots[static_cast<std::size_t>(i)].ripe) { harvest_plot(i); ++harvested; } }
    }
    // M4 event: force-fire the first event and take option 0; check the effect landed.
    const int evBefore = g.eventsResolved;
    if (!g.events.empty()) { g.curEvent = 0; resolve_event(0); }
    const bool eventsOk = !g.events.empty() && g.eventsResolved > evBefore;

    // M5 manufacturing: enqueue a recipe and tick it to completion (deterministic).
    const float partsBefore = g.res.parts;
    if (!g.recipes.empty()) { g.res.rawMaterials += 30; g.res.power = 200; g.mfgQueue[0] = 0; g.mfgProgress[0] = 0; }
    // M5 research: start a project.
    if (!g.research.empty()) { g.sciActive = 0; g.sciProgress = 0; }
    for (int d = 0; d < 12; ++d) { tick_manufacturing(); tick_research(); }
    const bool mfgOk = g.res.parts > partsBefore;
    bool sciOk = false; for (int t : g.techPoints) { if (t > 0) { sciOk = true; } }
    // M5 trade: accept the first offer.
    if (!g.offers.empty()) { g.res.food += 100; const TradeOffer& o = g.offers[0]; g.res.food -= o.foodCost; g.res.medicine += o.medicineGain; }

    // M6 election: become T3 and stand for captain.
    g.playerSkill = 80; update_player_tier(); run_for_captain();
    const bool electionRan = g.ranForCaptain;
    // M6 void-seed + endings: reveal the seed, finish the voyage, evaluate.
    g.voidStage = 30; g.voidDiscovered = true; g.voidStance = 1;
    g.lyTravelled = g.lyTotal; evaluate_endings();
    const bool endingOk = g.ending >= 0;

    // M7 save/load: round-trip the session.
    save_game();
    const int savedDay = g.day; g.day = 999;
    const bool loadOk = load_game() && g.day == savedDay;

    const bool ok = harvested >= kPlots && eventsOk && mfgOk && electionRan && endingOk && loadOk;
    std::ofstream rf("voidborne_result.txt");
    rf << (ok ? "VOIDBORNE M0-M7 OK" : "VOIDBORNE M0-M7 FAIL")
       << " crops=" << g.cropDefs.size() << " events=" << g.events.size() << " crew=" << g.crew.size()
       << " recipes=" << g.recipes.size() << " research=" << g.research.size() << " routes=" << g.routes.size()
       << " offers=" << g.offers.size()
       << " harvested=" << g.totalHarvested << " eventsResolved=" << g.eventsResolved
       << " mfgOk=" << mfgOk << " parts=" << static_cast<int>(g.res.parts) << " sciOk=" << sciOk
       << " tier=" << g.playerTier << " captain=" << g.isCaptain << " electionRan=" << electionRan
       << " void=" << static_cast<int>(g.voidStage) << " ending=" << g.ending
       << "(" << (g.ending >= 0 ? kEndingName[g.ending] : "-") << ")"
       << " saveLoad=" << loadOk;
}

}  // namespace

int main(int argc, char* argv[]) {
    int frames = 0;
    bool selftest = false, autodemo = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) { frames = std::atoi(argv[++i]); }
        else if (!std::strcmp(argv[i], "--selftest")) { selftest = true; }
        else if (!std::strcmp(argv[i], "--autodemo")) { autodemo = true; }
        else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) { g_dataDir = argv[++i]; }
        else if (!std::strcmp(argv[i], "--lang") && i + 1 < argc) { g.lang = (std::string(argv[++i]) == "zh") ? 1 : 0; }
    }
    if (const char* lc = std::getenv("OKN_VB_LANG")) { if (std::string(lc) == "zh") { g.lang = 1; } }

    load_all_data();
    const bool ok = g.cropDefs.size() == 4;
    if (selftest) {
        const bool good = ok && !g.events.empty() && !g.crew.empty() && !g.recipes.empty() &&
                          !g.research.empty() && !g.routes.empty();
        std::ofstream rf("voidborne_result.txt");
        rf << (good ? "VOIDBORNE DATA OK" : "VOIDBORNE DATA FAIL")
           << " crops=" << g.cropDefs.size() << " events=" << g.events.size() << " crew=" << g.crew.size()
           << " recipes=" << g.recipes.size() << " research=" << g.research.size() << " routes=" << g.routes.size();
        return good ? 0 : 1;
    }
    if (autodemo) { run_autodemo(); return ok ? 0 : 1; }

    build_decks_data();        // the 6-deck blueprint
    build_deck(kStartDeck);    // start on the habitation deck (like the original)

    // screenshot hooks: OKN_VB_SHOW=<panel> opens a panel with demo state so captures
    // show each milestone alive.
    if (const char* show = std::getenv("OKN_VB_SHOW")) {
        const std::string s = show;
        if (s == "bay" && g.cropDefs.size() >= 4) {
            g.inBay = true;
            auto seed = [&](int i, int crop, float grow) {
                Plot& p = g.plots[static_cast<std::size_t>(i)];
                p.crop = crop; p.growth = grow; p.watered = (i % 2) == 0;
                p.ripe = grow >= static_cast<float>(g.cropDefs[static_cast<std::size_t>(crop)].growthDays);
            };
            seed(0, 0, 4.0f); seed(1, 1, 3.0f); seed(2, 2, 1.5f); seed(3, 3, 4.0f); seed(4, 0, 1.0f);
        } else if (s == "event" && !g.events.empty()) { g.curEvent = 0; }
        else if (s == "memory") { for (int i = 0; i < static_cast<int>(g.events.size()); ++i) { if (g.events[static_cast<std::size_t>(i)].id == "memory_milestone_archive") { g.curEvent = i; break; } } }
        else if (s == "mfg") { g.inMfg = true; g.res.rawMaterials = 60; g.mfgQueue[0] = 0; g.mfgProgress[0] = 0.4f; if (g.recipes.size() > 2) { g.mfgQueue[1] = 2; g.mfgProgress[1] = 1.2f; } }
        else if (s == "sci") { g.inSci = true; g.sciActive = 0; g.sciProgress = 1.5f; g.techPoints[0] = 2; g.techPoints[1] = 1; }
        else if (s == "nav") { g.inStarmap = true; g.lyTravelled = 38; g.generation = 3; g.voidDiscovered = true; g.voidStage = 34; g.voidStance = 1; }
        else if (s == "captain") { g.inCaptain = true; g.playerSkill = 80; g.playerTier = 3; }
        else if (s == "ending") { g.ending = 4; g.lyTravelled = g.lyTotal; g.isCaptain = true; g.generation = 6; g.day = 380; }
        else if (s == "eng") { g.inEng = true; }
        else if (s == "log") { g.inLog = true; }
        else if (s == "crew") { g.inCrew = true; }
        else if (s == "elev") { g.elevatorOpen = true; }
        // deck world-views: ride to a named deck for the screenshot
        else if (s == "drive") { build_deck(0); }
        else if (s == "ops") { build_deck(1); }
        else if (s == "ecology") { build_deck(2); }
        else if (s == "habitation") { build_deck(3); }
        else if (s == "science") { build_deck(4); }
        else if (s == "command") { build_deck(5); }
        // park the player in a doorway so the airlock animates open for the capture
        else if (s == "doors") { build_deck(3); if (g.rooms.size() > 3) { const Room& rr = g.rooms[3]; g.pcx = static_cast<float>(rr.doorx) + 1.0f; g.pcy = static_cast<float>(rr.doory) + 0.5f; } }
        // center on the reactor core for the capture
        else if (s == "reactor") { build_deck(0); for (const Room& rr : g.rooms) { if (rr.kind == RoomKind::Reactor) { g.pcx = static_cast<float>(rr.x0 + rr.x1) * 0.5f; g.pcy = static_cast<float>(rr.y1) + 1.5f; break; } } }
    }

    unigui::AppConfig cfg;
    cfg.title = "VOIDBORNE \xe2\x80\x94 Qingniao";
    cfg.width = 1280;
    cfg.height = 800;
    if (!unigui::Init(cfg)) { return 1; }

    // Load the bundled fusion-pixel font WITH CJK glyph ranges so Chinese renders.
    // UniGUI's Manager::Load() can't pass glyph ranges, so add it via raw ImGui —
    // added before the first frame, the backend uploads the full atlas on NewFrame.
    {
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig fc; fc.OversampleH = 1; fc.OversampleV = 1; fc.PixelSnapH = true;
        ImFont* zh = io.Fonts->AddFontFromFileTTF("assets/fonts/pixel_zh.ttf", 18.0f, &fc,
                                                  io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (zh) { io.FontDefault = zh; }
        io.Fonts->Build();
    }
    apply_pixel_style();

    unigui::Run([] { draw_frame(); }, frames);
    return 0;
}
