#include "platformer_lua.hpp"

#include <sol/sol.hpp>   // the platformer target compiles /WX-, so sol2's warnings are fine

namespace plat {

bool load_levels_from_lua(const std::string& path, std::vector<LuaLevel>& out) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table);

    std::vector<LuaLevel> parsed;
    lua.set_function("level", [&parsed](sol::table t) {
        LuaLevel lv;
        sol::optional<sol::table> plats = t["plats"];
        if (plats) {
            sol::table pt = *plats;
            for (std::size_t i = 1;; ++i) {
                sol::optional<sol::table> row = pt[i];
                if (!row) { break; }
                sol::table r = *row;
                lv.plats.push_back(LuaPlat{r[1].get<float>(), r[2].get<float>(),
                                           r[3].get<float>(), r[4].get<float>()});
            }
        }
        sol::optional<sol::table> spawn = t["spawn"];
        if (spawn) { sol::table s = *spawn; lv.spawn_x = s[1].get<float>(); lv.spawn_y = s[2].get<float>(); }
        sol::optional<sol::table> goal = t["goal"];
        if (goal) { sol::table g = *goal; lv.goal_x = g[1].get<float>(); lv.goal_y = g[2].get<float>(); }
        parsed.push_back(std::move(lv));
    });

    const sol::protected_function_result res =
        lua.safe_script_file(path, sol::script_pass_on_error);
    if (!res.valid() || parsed.empty()) { return false; }
    out = std::move(parsed);
    return true;
}

}  // namespace plat
