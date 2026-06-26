-- Platformer levels — authored in Lua, hot-reloaded at runtime (edit + save to see it
-- change live). Each plat is {center_x, center_y, half_w, half_h}; spawn/goal are {x, y}.
-- The game falls back to its built-in levels if this file is missing or errors.

level{
    plats = { {0, 0, 6, 0.5}, {9, 1.5, 1.5, 0.5}, {14, 3.0, 2.0, 0.5} },   -- L0: gentle intro
    spawn = {-3.0, 1.5},
    goal  = {14.0, 4.0},
}

level{
    plats = { {-2, 0, 3, 0.5}, {4, 0.5, 1.5, 0.5}, {9, 2.0, 1.5, 0.5},     -- L1: gaps + a step up
              {13, 3.5, 1.5, 0.5}, {17, 5.0, 2.0, 0.5} },
    spawn = {-3.5, 1.5},
    goal  = {17.0, 6.0},
}

level{
    plats = { {-3, 0, 2.5, 0.5}, {2, 1.5, 1.2, 0.5}, {6, 3.0, 1.2, 0.5},   -- L2: a climb to the top
              {2, 4.5, 1.2, 0.5}, {6, 6.0, 1.2, 0.5}, {10, 7.5, 2.0, 0.5} },
    spawn = {-4.0, 1.5},
    goal  = {10.0, 8.6},
}
