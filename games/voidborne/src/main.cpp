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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
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

constexpr int kPop = 300;               // souls aboard
constexpr int kPlots = 8;               // ecology-bay plots
constexpr float kTile = 38.0f;          // ship-world tile size (px)
constexpr float kPlayerR = 0.32f;       // player collision half-extent (tiles)
constexpr float kMoveSpeed = 4.4f;      // tiles/sec

// One ecology plot.
struct Plot {
    int crop = -1;          // index into cropDefs, or -1 = empty soil
    float growth = 0;       // accumulated grow-days
    bool watered = false;
    bool ripe = false;
};

// Top-down deck: '#'=wall, '.'=floor, 'D'=ecology-bay door (functional),
// 'd'=stub door, '@'=player spawn. 28×16.
const char* kDeck[] = {
    "############################",
    "#..........................#",
    "#..######.......######.....#",
    "#..#....#.......#....#......#",
    "#..#.bb.#.......#.ee.#......#",
    "#..#.bb.#.......#.ee.#......#",
    "#..###D###......###d###.....#",
    "#..........................#",
    "#..........................#",
    "#..........................#",
    "#..######.......######.....#",
    "#..#....#.......#....#......#",
    "#..#.rr.#.......#.gg.#......#",
    "#..#....#.......#....#......#",
    "#..###d###..@...###d###.....#",
    "############################",
};
constexpr int kDeckH = 16, kDeckW = 28;
constexpr int kBayDoorX = 6, kBayDoorY = 6;   // the functional door

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
    float pcx = 12.5f, pcy = 14.5f;     // continuous tile position
    int fx = 0, fy = -1;                // facing

    // ── settlement bookkeeping (for the HUD + verification) ──
    float lastFoodDelta = 0, lastO2Delta = 0;
    int totalHarvested = 0;
    float totalFoodHarvested = 0;

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
}

// ── time advance (the original TimeManager accumulator) ──
void advance_time(float dt) {
    if (g.paused) { return; }
    const float speed = (g.speedIndex == 0) ? 1.0f : (g.speedIndex == 1) ? 2.0f : 4.0f;
    g.accum += dt * speed;
    while (g.accum >= g.secondsPerHour) {
        g.accum -= g.secondsPerHour;
        if (++g.hour >= 24) { g.hour = 0; ++g.day; daily_settlement(); }
    }
}

// ── ship-world helpers ──
char deck_at(int x, int y) {
    if (x < 0 || y < 0 || x >= kDeckW || y >= kDeckH) { return '#'; }
    return kDeck[y][x];
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
bool at_bay_door() {
    const int tx = static_cast<int>(std::floor(g.pcx)), ty = static_cast<int>(std::floor(g.pcy));
    return std::abs(tx - kBayDoorX) <= 1 && std::abs(ty - kBayDoorY) <= 1;
}

// ── rendering ──────────────────────────────────────────────────────────────────
void draw_world() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(8, 10, 16, 255));

    const float ox = vp->Pos.x + (vp->Size.x - kDeckW * kTile) * 0.5f;
    const float oy = vp->Pos.y + 96.0f;   // below the HUD
    auto sx = [&](float c) { return ox + c * kTile; };
    auto sy = [&](float r) { return oy + r * kTile; };

    for (int y = 0; y < kDeckH; ++y) {
        for (int x = 0; x < kDeckW; ++x) {
            const char c = deck_at(x, y);
            const ImVec2 a(sx(static_cast<float>(x)), sy(static_cast<float>(y)));
            const ImVec2 b(a.x + kTile, a.y + kTile);
            ImU32 col;
            if (c == '#') { col = IM_COL32(40, 48, 62, 255); }
            else if (c == 'b') { col = IM_COL32(28, 56, 40, 255); }      // ecology bay floor
            else if (c == 'e') { col = IM_COL32(54, 44, 30, 255); }      // engineering
            else if (c == 'r') { col = IM_COL32(30, 44, 58, 255); }      // research
            else if (c == 'g') { col = IM_COL32(50, 36, 52, 255); }      // bridge
            else if (c == 'D' || c == 'd') { col = IM_COL32(120, 96, 40, 255); }  // door
            else { col = ((x + y) & 1) ? IM_COL32(24, 28, 38, 255) : IM_COL32(20, 24, 33, 255); }
            dl->AddRectFilled(a, b, col);
            if (c == '#') { dl->AddRect(a, b, IM_COL32(58, 70, 90, 120)); }
        }
    }
    // room labels
    auto label = [&](float cx, float cy, const char* t, ImU32 col) {
        dl->AddText(ImVec2(sx(cx), sy(cy)), col, t);
    };
    label(3.6f, 3.4f, "ECOLOGY", IM_COL32(120, 220, 150, 255));
    label(16.4f, 3.4f, "ENGINE", IM_COL32(220, 170, 110, 255));
    label(3.7f, 11.4f, "RESEARCH", IM_COL32(120, 180, 230, 255));
    label(16.7f, 11.4f, "BRIDGE", IM_COL32(210, 150, 220, 255));

    // player
    const ImVec2 pc(sx(g.pcx), sy(g.pcy));
    dl->AddCircleFilled(pc, kTile * 0.34f, IM_COL32(235, 240, 255, 255));
    dl->AddCircleFilled(pc, kTile * 0.34f, IM_COL32(70, 130, 220, 90));
    dl->AddCircle(pc, kTile * 0.34f, IM_COL32(40, 60, 110, 255), 0, 2.0f);
    // facing pip
    dl->AddCircleFilled(ImVec2(pc.x + g.fx * kTile * 0.28f, pc.y + g.fy * kTile * 0.28f),
                        kTile * 0.08f, IM_COL32(50, 70, 120, 255));

    // interaction prompt at the bay door
    const ImVec2 doorc(sx(kBayDoorX + 0.5f), sy(kBayDoorY + 0.5f));
    if (at_bay_door()) {
        dl->AddText(ImVec2(doorc.x - 36, doorc.y - kTile), IM_COL32(255, 240, 160, 255), "[E] Ecology Bay");
    } else {
        dl->AddText(ImVec2(doorc.x - 18, doorc.y - kTile), IM_COL32(160, 200, 170, 180), "BAY");
    }
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
    if (g.lastFoodDelta != 0.0f) {
        ImGui::SameLine();
        ImGui::TextColored(g.lastFoodDelta >= 0 ? ImVec4(0.5f, 0.9f, 0.5f, 1) : ImVec4(0.95f, 0.5f, 0.4f, 1),
                           "   last day food %+.1f", g.lastFoodDelta);
    }
    ImGui::TextDisabled("WASD walk the ship  ·  E enter the Ecology Bay  ·  Space pause  ·  TAB speed");
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

// ── input + frame ───────────────────────────────────────────────────────────────
void handle_input(float dt) {
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) { g.paused = !g.paused; }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) { g.speedIndex = (g.speedIndex + 1) % 3; }
    if (g.inBay) {
        if (ImGui::IsKeyPressed(ImGuiKey_E, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { g.inBay = false; }
        return;
    }
    float dcol = 0, drow = 0;
    if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) { drow -= 1; }
    if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) { drow += 1; }
    if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) { dcol -= 1; }
    if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) { dcol += 1; }
    move_player(dcol, drow, dt);
    if (ImGui::IsKeyPressed(ImGuiKey_E, false) && at_bay_door()) { g.inBay = true; }
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

void draw_frame() {
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 1.0f / 20.0f);
    advance_time(dt);
    handle_input(dt);
    draw_world();
    draw_hud();
    if (g.inBay) { draw_bay_panel(); }
}

// ── headless autodemo: drive the whole loop, assert the economy moved ──
void run_autodemo() {
    // plant + water all plots with the fast staple, then tick days until ripe + harvest.
    const int grain = 0;   // crops[0] = Main Grain (growthDays 4)
    for (auto& p : g.plots) { p = Plot{}; p.crop = grain; p.watered = true; }
    const float food0 = g.res.food;
    int harvested = 0;
    for (int d = 0; d < 8; ++d) {
        for (auto& p : g.plots) { if (p.crop >= 0 && !p.ripe) { p.watered = true; } }   // re-water daily
        daily_settlement();
        for (int i = 0; i < kPlots; ++i) { if (g.plots[static_cast<std::size_t>(i)].ripe) { harvest_plot(i); ++harvested; } }
    }
    const bool ok = harvested >= kPlots && g.totalFoodHarvested > 0 && g.day == 1;   // day untouched (we called settlement directly)
    std::ofstream rf("voidborne_result.txt");
    rf << (ok ? "VOIDBORNE M0-M3 OK" : "VOIDBORNE M0-M3 FAIL")
       << " crops=" << g.cropDefs.size()
       << " harvested=" << g.totalHarvested
       << " foodHarvested=" << static_cast<int>(g.totalFoodHarvested)
       << " food0=" << static_cast<int>(food0) << " food1=" << static_cast<int>(g.res.food)
       << " envFactorGrain=" << env_factor(g.cropDefs[0]);
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

    const bool ok = load_crops();
    if (selftest) {
        std::ofstream rf("voidborne_result.txt");
        rf << (ok && g.cropDefs.size() == 4 ? "VOIDBORNE M0 OK" : "VOIDBORNE M0 FAIL") << " crops=" << g.cropDefs.size();
        return (ok && g.cropDefs.size() == 4) ? 0 : 1;
    }
    if (autodemo) { run_autodemo(); return ok ? 0 : 1; }

    // screenshot hook: OKN_VB_SHOW=bay opens the Ecology Bay seeded with plots at
    // varied growth stages, so a capture shows the M3 department alive.
    if (const char* show = std::getenv("OKN_VB_SHOW")) {
        if (!std::strcmp(show, "bay") && g.cropDefs.size() >= 4) {
            g.inBay = true;
            auto seed = [&](int i, int crop, float grow) {
                Plot& p = g.plots[static_cast<std::size_t>(i)];
                p.crop = crop; p.growth = grow; p.watered = (i % 2) == 0;
                p.ripe = grow >= static_cast<float>(g.cropDefs[static_cast<std::size_t>(crop)].growthDays);
            };
            seed(0, 0, 4.0f);   // grain, ripe
            seed(1, 1, 3.0f);   // herb, ~half
            seed(2, 2, 1.5f);   // algae, ~half
            seed(3, 3, 4.0f);   // hybrid, ~80%
            seed(4, 0, 1.0f);   // grain, young
        }
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
