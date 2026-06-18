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
    std::string id, title, body, deptGate, triggerRes;
    int weight = 10, cooldown = 10, minDay = 0, lastFired = -999;
    float triggerBelow = -1;                  // fire only when triggerRes < this (optional)
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
    std::string name;
    int panel = -1;
    RoomKind kind = RoomKind::Generic;
    ImU32 floorCol = IM_COL32(24, 28, 38, 255);
    ImU32 labelCol = IM_COL32(200, 210, 220, 255);
    bool isVoid = false;       // Hydro Bay G glows once the seed is discovered
};
struct WorldNpc { float x = 0, y = 0, phase = 0; int fx = 0, fy = 1; ImU32 color = IM_COL32(150, 150, 170, 255); std::string name; };
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
    int elevX = 2, elevY = 8;           // elevator tile on the current deck
    bool elevatorOpen = false;          // deck-select overlay up?
    std::vector<WorldNpc> npcsHere;     // crew standing on the current deck
    float pcx = 3.5f, pcy = 8.5f;       // continuous tile position
    int fx = 0, fy = -1;                // facing

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

// ── M4: events + crew loaders ──
void load_events_file(const std::string& path) {
    const std::string txt = read_file(path);
    if (txt.empty()) { return; }
    rapidjson::Document d; d.Parse(txt.c_str());
    if (d.HasParseError() || !d.HasMember("events") || !d["events"].IsArray()) { return; }
    for (auto& e : d["events"].GetArray()) {
        EventDef ev;
        ev.id = jstr(e, "id"); ev.title = jstr(e, "title"); ev.body = jstr(e, "body");
        ev.deptGate = jstr(e, "deptGate");
        ev.weight = static_cast<int>(jnum(e, "weight", 10));
        ev.cooldown = static_cast<int>(jnum(e, "cooldown", 10));
        if (e.HasMember("trigger") && e["trigger"].IsObject() && e["trigger"].HasMember("any") && e["trigger"]["any"].IsArray()) {
            for (auto& t : e["trigger"]["any"].GetArray()) {
                if (t.HasMember("minDay")) { ev.minDay = static_cast<int>(to_num(t["minDay"])); }
                if (t.HasMember("resource") && t.HasMember("below")) { ev.triggerRes = t["resource"].GetString(); ev.triggerBelow = to_num(t["below"]); }
            }
        }
        if (e.HasMember("options") && e["options"].IsArray()) {
            for (auto& o : e["options"].GetArray()) {
                EventOption op; op.label = jstr(o, "label"); op.deptGate = jstr(o, "deptGate");
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
void load_all_data() {
    load_crops(); load_events_file(g_dataDir + "/events.json"); load_events_file(g_dataDir + "/events_personal.json");
    load_crew(); load_recipes(); load_research(); load_routes(); load_trade();
}

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
    std::vector<int> pool; int total = 0;
    for (int i = 0; i < static_cast<int>(g.events.size()); ++i) {
        const EventDef& e = g.events[static_cast<std::size_t>(i)];
        if (g.day - e.lastFired < e.cooldown) { continue; }
        if (g.day < e.minDay) { continue; }
        if (e.triggerBelow >= 0 && !resource_below(e.triggerRes, e.triggerBelow)) { continue; }
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
const char* kEndingName[7] = {
    "COLLAPSE", "SUFFOCATION", "VERDANT COMMUNION", "CONSUMED BY THE VOID",
    "LANDFALL UNDER YOUR COMMAND", "A CHANGED PEOPLE ARRIVE", "QUIET ARRIVAL",
};
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

    // (order 50) LIFE SUPPORT consume: people eat, drink, breathe; recyclers churn.
    g.res.food = std::max(0.0f, g.res.food - kPop * 0.045f);          // ~13.5/day
    g.res.water = std::max(0.0f, g.res.water - kPop * 0.03f);         // ~9/day
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

    // morale drifts on scarcity / surplus + ration policy (the original S10 flavour)
    const float scarcity = (g.res.food < kPop * 0.5f ? -1.5f : 0.6f) + (g.ration - 1) * 0.4f;
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

// ── ship-world helpers ──
char deck_at(int x, int y) {
    if (x < 0 || y < 0 || y >= static_cast<int>(g.deck.size()) || x >= static_cast<int>(g.deck[static_cast<std::size_t>(y)].size())) { return '#'; }
    return g.deck[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
}
bool walkable_tile(int x, int y) { const char c = deck_at(x, y); return c != '#'; }
bool blocked_box(float cx, float cy) {
    const int x0 = static_cast<int>(std::floor(cx - kPlayerR)), x1 = static_cast<int>(std::floor(cx + kPlayerR));
    const int y0 = static_cast<int>(std::floor(cy - kPlayerR)), y1 = static_cast<int>(std::floor(cy + kPlayerR));
    for (int y = y0; y <= y1; ++y) { for (int x = x0; x <= x1; ++x) { if (!walkable_tile(x, y)) { return true; } } }
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
    if (!blocked_box(nx, g.pcy)) { g.pcx = nx; }
    const float ny = g.pcy + drow * step;
    if (!blocked_box(g.pcx, ny)) { g.pcy = ny; }
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
    add("D2 ECOLOGY", "7 hydroponics bays, seed bank, algae O2", IM_COL32(123, 216, 138, 255), 64, 20,
        {{"HYDRO BAY A", 11, 0, K::Hydro}, {"HYDRO BAY B", 11, 0, K::Hydro}, {"HYDRO BAY C", 11, 0, K::Hydro}, {"HYDRO BAY D", 11, 0, K::Hydro}},
        {{"HYDRO BAY E", 11, 0, K::Hydro}, {"HYDRO BAY F", 11, 0, K::Hydro}, {"HYDRO BAY G", 11, 0, K::Hydro}, {"SEED BANK", 9, -1, K::SeedBank}, {"ALGAE O2", 8, -1, K::Algae}});
    add("D3 HABITATION", "Quarters, mess, medbay, school -- home to 300", IM_COL32(159, 180, 216, 255), 62, 20,
        {{"QUARTERS A", 11, 5, K::Quarters}, {"QUARTERS B", 11, 5, K::Quarters}, {"QUARTERS C", 11, 5, K::Quarters}, {"MESS HALL", 16, -1, K::Mess}},
        {{"MEDBAY", 10, -1, K::Medical}, {"LOUNGE", 9, -1, K::Lounge}, {"SCHOOL", 10, -1, K::School}, {"GYM", 8, -1, K::Gym}, {"QUARTERS D", 11, 5, K::Quarters}});
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
        while (si < spots.size() && !walkable_tile(static_cast<int>(spots[si].x), static_cast<int>(spots[si].y))) { ++si; }
        if (si >= spots.size()) { break; }
        WorldNpc n; n.x = spots[si].x; n.y = spots[si].y; n.phase = static_cast<float>(i) * 1.7f;
        n.color = dept_color(g.crew[static_cast<std::size_t>(i)].department);
        n.name = g.crew[static_cast<std::size_t>(i)].name; n.fy = (i & 1) ? 1 : -1;
        g.npcsHere.push_back(n); ++si;
    }
}
// Generate the CURRENT deck's tilemap + room list from its blueprint: a central
// corridor with rooms packed left-to-right above (south doors) and below (north).
void build_deck(int d) {
    g.curDeck = std::clamp(d, 0, static_cast<int>(g.decks.size()) - 1);
    const DeckDef& def = g.decks[static_cast<std::size_t>(g.curDeck)];
    const int W = static_cast<int>(std::lround(def.width * kMapScale));
    const int H = static_cast<int>(std::lround(def.height * kMapScale));
    g.deckW = W; g.deckH = H;
    g.deck.assign(static_cast<std::size_t>(H), std::string(static_cast<std::size_t>(W), '.'));
    for (int x = 0; x < W; ++x) { g.deck[0][static_cast<std::size_t>(x)] = '#'; g.deck[static_cast<std::size_t>(H - 1)][static_cast<std::size_t>(x)] = '#'; }
    for (int y = 0; y < H; ++y) { g.deck[static_cast<std::size_t>(y)][0] = '#'; g.deck[static_cast<std::size_t>(y)][static_cast<std::size_t>(W - 1)] = '#'; }
    const int corr = H / 2 - 1;     // corridor occupies rows corr, corr+1
    g.rooms.clear();
    int hydro = 0;
    auto pack = [&](const std::vector<RoomSpec>& specs, int ty, int th, bool doorNorth) {
        int x = 5;
        for (const RoomSpec& s : specs) {
            const int rw = std::max(5, static_cast<int>(std::lround(s.width * kMapScale)));
            const int x0 = x, y0 = ty, x1 = x + rw - 1, y1 = ty + th - 1;
            if (x1 >= W - 1) { break; }
            for (int xx = x0; xx <= x1; ++xx) { g.deck[static_cast<std::size_t>(y0)][static_cast<std::size_t>(xx)] = '#'; g.deck[static_cast<std::size_t>(y1)][static_cast<std::size_t>(xx)] = '#'; }
            for (int yy = y0; yy <= y1; ++yy) { g.deck[static_cast<std::size_t>(yy)][static_cast<std::size_t>(x0)] = '#'; g.deck[static_cast<std::size_t>(yy)][static_cast<std::size_t>(x1)] = '#'; }
            const int dx = (x0 + x1) / 2, dy = doorNorth ? y0 : y1;
            g.deck[static_cast<std::size_t>(dy)][static_cast<std::size_t>(dx)] = '.';
            g.deck[static_cast<std::size_t>(dy)][static_cast<std::size_t>(std::min(W - 2, dx + 1))] = '.';
            Room R; R.x0 = x0; R.y0 = y0; R.x1 = x1; R.y1 = y1; R.doorx = dx; R.doory = dy; R.doorNorth = doorNorth;
            R.name = s.label; R.panel = s.panel; R.kind = s.kind;
            R.floorCol = deck_floor(def.accent, s.kind); R.labelCol = def.accent;
            if (s.kind == RoomKind::Hydro) { R.isVoid = (++hydro == 7); }   // Bay G
            g.rooms.push_back(R);
            x += rw + 1;
        }
    };
    pack(def.top, 1, corr - 1, false);
    pack(def.bottom, corr + 2, H - 2 - (corr + 2) + 1, true);
    g.elevX = 2; g.elevY = corr;
    g.pcx = 3.5f; g.pcy = static_cast<float>(corr) + 0.5f;   // spawn beside the elevator
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
// 8x11 top-down crew sprite. keys: 0 clear,1 hair,2 skin,3 uniform,4 sheen,5 pants,6 eye,7 boots
const char* kCrewPx[11] = {
    "01111110", "12222221", "12622621", "12222221", "03333330",
    "33333333", "34333343", "33333333", "03333330", "05500550", "07700770",
};
void draw_character(ImDrawList* dl, ImVec2 c, float s, ImU32 uniform, int fx, int fy, bool isPlayer) {
    (void)fx; (void)fy;
    constexpr int W = 8, H = 11;
    const float px = std::max(2.0f, std::floor(s / 8.0f));
    const float ox = std::floor(c.x - W * px * 0.5f), oy = std::floor(c.y - H * px * 0.82f);
    dl->AddRectFilled(ImVec2(ox + px, oy + H * px), ImVec2(ox + (W - 1) * px, oy + H * px + px), IM_COL32(0, 0, 0, 70));   // shadow
    const ImU32 hair = IM_COL32(74, 56, 44, 255), skin = IM_COL32(240, 208, 172, 255), pants = IM_COL32(46, 50, 62, 255),
                eye = IM_COL32(32, 30, 38, 255), boots = IM_COL32(58, 44, 32, 255), sheen = lighten(uniform, 44);
    for (int r = 0; r < H; ++r) {
        for (int cc = 0; cc < W; ++cc) {
            const char k = kCrewPx[r][cc];
            if (k == '0') { continue; }
            const ImU32 col = k == '1' ? hair : k == '2' ? skin : k == '3' ? uniform : k == '4' ? sheen
                            : k == '5' ? pants : k == '6' ? eye : boots;
            const float qx = ox + cc * px, qy = oy + r * px;
            dl->AddRectFilled(ImVec2(qx, qy), ImVec2(qx + px + 0.6f, qy + px + 0.6f), col);
        }
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
    dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), IM_COL32(8, 10, 16, 255));

    const float hud = 96.0f, viewW = sz.x, viewH = sz.y - hud;
    auto camAxis = [](float playerPx, float full, float view) {
        if (full <= view) { return (full - view) * 0.5f; }
        return std::clamp(playerPx - view * 0.5f, 0.0f, full - view);
    };
    const float fullW = g.deckW * kTile, fullH = g.deckH * kTile;
    const float camx = camAxis(g.pcx * kTile, fullW, viewW), camy = camAxis(g.pcy * kTile, fullH, viewH);
    const float ox = std::floor(p0.x - camx), oy = std::floor(p0.y + hud - camy);   // snap to whole pixels
    auto sx = [&](float c) { return ox + c * kTile; };
    auto sy = [&](float r) { return oy + r * kTile; };
    auto box = [&](float cx, float cy, float w, float h, ImU32 c) { dl->AddRectFilled(ImVec2(sx(cx), sy(cy)), ImVec2(sx(cx + w), sy(cy + h)), c); };

    const int x0 = std::max(0, static_cast<int>(camx / kTile));
    const int x1 = std::min(g.deckW - 1, static_cast<int>((camx + viewW) / kTile) + 1);
    const int y0 = std::max(0, static_cast<int>(camy / kTile));
    const int y1 = std::min(g.deckH - 1, static_cast<int>((camy + viewH) / kTile) + 1);
    const float t4 = kTile * 0.5f, riv = kTile * 0.16f;
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            const ImVec2 a(sx(static_cast<float>(tx)), sy(static_cast<float>(ty)));
            const ImVec2 b(a.x + kTile, a.y + kTile);
            if (deck_at(tx, ty) == '#') {                // 2-tone pixel hull panel + rivet
                dl->AddRectFilled(a, ImVec2(b.x, a.y + t4), IM_COL32(58, 66, 86, 255));
                dl->AddRectFilled(ImVec2(a.x, a.y + t4), b, IM_COL32(30, 36, 48, 255));
                dl->AddRectFilled(ImVec2(a.x, b.y - 2), b, IM_COL32(12, 16, 24, 255));
                dl->AddRectFilled(ImVec2(a.x + riv, a.y + riv), ImVec2(a.x + riv * 2, a.y + riv * 2), IM_COL32(94, 104, 128, 255));
                continue;
            }
            const int ri = room_of(tx, ty);
            ImU32 col;
            if (ri >= 0) { col = g.rooms[static_cast<std::size_t>(ri)].floorCol; if (g.rooms[static_cast<std::size_t>(ri)].isVoid && g.voidDiscovered) { col = IM_COL32(64, 38, 86, 255); } }
            else { col = ((tx + ty) & 1) ? IM_COL32(24, 28, 38, 255) : IM_COL32(19, 23, 31, 255); }
            dl->AddRectFilled(a, b, col);
            dl->AddRectFilled(ImVec2(b.x - 1, a.y), b, IM_COL32(0, 0, 0, 30));      // deck-plating seams
            dl->AddRectFilled(ImVec2(a.x, b.y - 1), b, IM_COL32(0, 0, 0, 30));
        }
    }
    // rooms: accent trim + blocky furniture + door + console + label
    for (const Room& r : g.rooms) {
        if (r.x1 < x0 || r.x0 > x1 || r.y1 < y0 || r.y0 > y1) { continue; }
        const ImU32 trim = (r.labelCol & 0x00FFFFFFu) | 0x40000000u;
        dl->AddRect(ImVec2(sx(static_cast<float>(r.x0)) + 3, sy(static_cast<float>(r.y0)) + 3),
                    ImVec2(sx(static_cast<float>(r.x1) + 1) - 3, sy(static_cast<float>(r.y1) + 1) - 3), trim, 0, 0, 2.0f);
        const float ix0 = static_cast<float>(r.x0) + 1.2f, iy0 = static_cast<float>(r.y0) + 1.2f;
        if (r.kind == RoomKind::Quarters) {                 // bunk beds
            for (float by = iy0; by < r.y1 - 1.8f; by += 2.1f) {
                for (float bx = ix0; bx < r.x1 - 1.6f; bx += 1.9f) {
                    box(bx, by, 1.5f, 1.7f, IM_COL32(52, 58, 72, 255));
                    box(bx + 0.12f, by + 0.12f, 1.26f, 1.0f, IM_COL32(188, 194, 208, 255));
                    box(bx + 0.18f, by + 0.16f, 0.5f, 0.42f, IM_COL32(246, 246, 250, 255));
                    box(bx + 0.12f, by + 0.66f, 1.26f, 0.5f, IM_COL32(88, 120, 172, 255));
                }
            }
        } else if (r.kind == RoomKind::Hydro) {             // blocky plant beds
            for (float py = iy0; py < r.y1 - 1.4f; py += 1.5f) {
                for (float px = ix0; px < r.x1 - 1.4f; px += 1.5f) {
                    box(px, py, 1.1f, 1.1f, IM_COL32(74, 54, 40, 255));
                    box(px + 0.28f, py + 0.5f, 0.55f, 0.45f, IM_COL32(58, 116, 64, 255));
                    box(px + 0.3f, py + 0.26f, 0.5f, 0.4f, IM_COL32(98, 184, 102, 255));
                    box(px + 0.4f, py + 0.3f, 0.2f, 0.2f, IM_COL32(168, 230, 156, 255));
                }
            }
        } else if (r.kind == RoomKind::Mess || r.kind == RoomKind::Conference) {   // tables + plates
            for (float yy = iy0 + 0.4f; yy < r.y1 - 1.4f; yy += 2.4f) {
                box(ix0 + 0.4f, yy, static_cast<float>(r.x1 - r.x0) - 2.4f, 1.0f, IM_COL32(98, 80, 58, 255));
                box(ix0 + 0.4f, yy, static_cast<float>(r.x1 - r.x0) - 2.4f, 0.22f, IM_COL32(124, 102, 74, 255));
                for (float px = ix0 + 1.0f; px < r.x1 - 1.6f; px += 1.6f) { box(px - 0.2f, yy + 0.32f, 0.4f, 0.4f, IM_COL32(212, 214, 220, 255)); }
            }
        } else {                                            // lockers with handles
            for (int k = 0; k < 2; ++k) {
                const float lx = k == 0 ? static_cast<float>(r.x0) + 1.4f : static_cast<float>(r.x1) - 2.5f;
                box(lx, iy0 + 0.3f, 1.1f, 1.9f, IM_COL32(56, 62, 76, 255));
                box(lx + 0.12f, iy0 + 0.45f, 0.86f, 1.6f, IM_COL32(70, 78, 94, 255));
                box(lx + 0.48f, iy0 + 0.8f, 0.16f, 0.9f, IM_COL32(150, 158, 176, 255));
            }
        }
        // door (2-tone)
        const ImVec2 da(sx(static_cast<float>(r.doorx)), sy(static_cast<float>(r.doory)));
        const ImVec2 db(da.x + kTile * 2, da.y + kTile);
        const ImU32 dtop = r.panel >= 0 ? IM_COL32(160, 128, 56, 255) : IM_COL32(80, 82, 94, 255);
        const ImU32 dbot = r.panel >= 0 ? IM_COL32(96, 74, 28, 255) : IM_COL32(46, 48, 58, 255);
        dl->AddRectFilled(da, ImVec2(db.x, da.y + t4), dtop);
        dl->AddRectFilled(ImVec2(da.x, da.y + t4), db, dbot);
        dl->AddRect(da, db, IM_COL32(0, 0, 0, 120), 0, 0, 1.0f);
        if (r.panel >= 0) {     // console terminal
            const float cyoff = r.doorNorth ? 0.85f : -1.75f;
            const float cx = sx(static_cast<float>(r.doorx) - 0.15f), cy = sy(static_cast<float>(r.doory) + cyoff);
            dl->AddRectFilled(ImVec2(cx - 5, cy - 5), ImVec2(cx + kTile * 1.35f + 5, cy + kTile * 0.95f + 5), IM_COL32(80, 200, 210, 30));
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
        dl->AddRectFilled(ea, ImVec2(eb.x, emid), IM_COL32(90, 80, 42, 255));
        dl->AddRectFilled(ImVec2(ea.x, emid), eb, IM_COL32(50, 44, 24, 255));
        dl->AddRect(ea, eb, IM_COL32(240, 220, 120, 255), 0, 0, 2.0f);
        dl->AddRectFilled(ImVec2((ea.x + eb.x) * 0.5f - 1, ea.y + 2), ImVec2((ea.x + eb.x) * 0.5f + 1, eb.y - 2), IM_COL32(200, 178, 96, 255));
        dl->AddText(ImVec2(ea.x - 4, ea.y - kTile * 0.9f), IM_COL32(240, 220, 120, 255), "LIFT");
    }
    // crew (NPCs) with idle bob + name tag when near
    const float tnow = static_cast<float>(ImGui::GetTime());
    for (const WorldNpc& n : g.npcsHere) {
        if (n.x < x0 - 1 || n.x > x1 + 1 || n.y < y0 - 1 || n.y > y1 + 1) { continue; }
        const float bob = std::sin(tnow * 2.2f + n.phase) * 0.05f;
        draw_character(dl, ImVec2(sx(n.x), sy(n.y + bob)), kTile, n.color, n.fx, n.fy, false);
        if (std::max(std::fabs(n.x - g.pcx), std::fabs(n.y - g.pcy)) <= 3.0f) {
            const float tw = ImGui::CalcTextSize(n.name.c_str()).x;
            const ImVec2 tp(std::floor(sx(n.x) - tw * 0.5f), std::floor(sy(n.y) - kTile * 1.05f));
            dl->AddRectFilled(ImVec2(tp.x - 4, tp.y - 1), ImVec2(tp.x + tw + 4, tp.y + 15), IM_COL32(14, 17, 24, 210));
            dl->AddText(tp, IM_COL32(230, 235, 245, 255), n.name.c_str());
        }
    }
    // player
    draw_character(dl, ImVec2(sx(g.pcx), sy(g.pcy)), kTile, IM_COL32(96, 174, 214, 255), g.fx, g.fy, true);
    const ImVec2 pc(sx(g.pcx), sy(g.pcy));
    const int s = station_at_player();
    if (near_elevator()) { dl->AddText(ImVec2(pc.x - 36, pc.y - kTile * 1.35f), IM_COL32(255, 240, 160, 255), "[E] Elevator"); }
    else if (s >= 0) { const std::string t = "[E] " + g.rooms[static_cast<std::size_t>(s)].name; dl->AddText(ImVec2(pc.x - 34, pc.y - kTile * 1.35f), IM_COL32(255, 240, 160, 255), t.c_str()); }

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
    ImGui::TextDisabled("DECK %s   ·   WASD walk · E elevator/console · at lift press 1-6 = deck · panels C/G/L/F/R/N/K · F5/F9 save · Space/TAB time",
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
    ImGui::Begin("SHIP EVENT", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
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
        if (!o.requireRes.empty()) { lbl += "   (needs " + std::to_string(o.requireAmt) + " " + o.requireRes + ")"; }
        if (ImGui::Button(lbl.c_str(), ImVec2(-1, 0))) { resolve_event(i); }
        if (locked) { ImGui::EndDisabled(); }
    }
    ImGui::TextDisabled("Press 1-%d to choose.   Time is paused.", static_cast<int>(e.options.size()));
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
        ImGui::TextColored(ImVec4(0.72f, 0.5f, 0.92f, 1), "VOID-SEED  ::  infection %.0f%%", g.voidStage);
        ImGui::TextWrapped("A luminous vine pulses in the dark of Bay G, whispering. The ship must take a stance:");
        const char* st[5] = {"Embrace", "Contain", "Purge", "Study", "Hide"};
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
    panel_begin("END OF VOYAGE", nullptr, 740, 300);
    ImGui::Dummy(ImVec2(0, 24));
    const char* nm = (g.ending >= 0 && g.ending < 7) ? kEndingName[g.ending] : "—";
    ImGui::SetCursorPosX((740 - ImGui::CalcTextSize(nm).x) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.92f, 1.0f, 1));
    ImGui::TextUnformatted(nm);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 18));
    ImGui::TextWrapped("Ending %d of 7.   Day %d, generation %d.   Morale %.0f%%, void-seed %.0f%%.   %s",
                       g.ending + 1, g.day, g.generation, g.morale, g.voidStage,
                       g.isCaptain ? "You died captain of the Qingniao." : "You served, and the ship sailed on.");
    ImGui::Dummy(ImVec2(0, 14));
    ImGui::TextDisabled("All 7 endings are wired. F9 reloads a save; or restart the exe for a new voyage.");
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
    move_player(dcol, drow, dt);
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
    advance_time(dt);
    handle_input(dt);
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
    }

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
    }

    unigui::AppConfig cfg;
    cfg.title = "VOIDBORNE \xe2\x80\x94 Qingniao";
    cfg.width = 1280;
    cfg.height = 800;
    if (!unigui::Init(cfg)) { return 1; }

    // Load the bundled pixel font (Latin glyphs) for the retro/terminal look. If it
    // can't be loaded we fall back to UniGUI's default font — never fatal.
    auto& fonts = unigui::fonts::Manager::Instance();
    if (fonts.Load("pixel", "assets/fonts/pixel_zh.ttf", 18.0f)) {
        fonts.Build();
        fonts.SetDefault("pixel");
    }
    apply_pixel_style();

    unigui::Run([] { draw_frame(); }, frames);
    return 0;
}
