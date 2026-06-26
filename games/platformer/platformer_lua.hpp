#pragma once

// Lua level loader for the platformer — the P15 north-star step that puts authored
// content in a hot-reloadable Lua file. sol2/Lua is confined to platformer_lua.cpp so
// the main game TU stays scripting-free. A neutral POD shape (plain floats) crosses the
// boundary; the game converts it to its own Level/Plat (okn-math Vec2) types.

#include <string>
#include <vector>

namespace plat {

struct LuaPlat { float cx, cy, hx, hy; };
struct LuaLevel {
    std::vector<LuaPlat> plats;
    float spawn_x = 0.0f, spawn_y = 0.0f;
    float goal_x = 0.0f, goal_y = 0.0f;
};

// Run `path` (which calls `level{ plats={{cx,cy,hx,hy},...}, spawn={x,y}, goal={x,y} }`
// once per level) and fill `out`. Returns false and leaves `out` empty on any error, so
// the caller can fall back to the built-in levels.
bool load_levels_from_lua(const std::string& path, std::vector<LuaLevel>& out);

}  // namespace plat
