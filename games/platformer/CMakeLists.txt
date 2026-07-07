cmake_minimum_required(VERSION 3.20)

# Platformer — the roadmap-v2 forcing-function game (P5+P6+P7), now the P15
# north-star title. Jolt character controller + input action-map + a disk-loaded
# sprite (stb) + binary progress save + Lua-authored hot-reloaded levels.
# Windows-only (sokol DX11), guarded on sokol + stb.

okn_add_sokol_game(NAME platformer LIBS okn-audio okn-physics okn-input NEEDS_STB)

# P15: levels authored in a hot-reloadable Lua file (sol2/Lua, like the slice). Optional
# — without sol2/Lua the game uses its built-in levels.
if(TARGET platformer)
    find_package(Lua QUIET)
    find_package(sol2 CONFIG QUIET)
    if(sol2_FOUND AND LUA_FOUND)
        target_sources(platformer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platformer_lua.cpp")
        target_link_libraries(platformer PRIVATE sol2::sol2 ${LUA_LIBRARIES})
        target_compile_definitions(platformer PRIVATE OKN_PLAT_HAS_LUA=1)
        add_custom_command(TARGET platformer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/platformer_levels.lua"
                "$<TARGET_FILE_DIR:platformer>/platformer_levels.lua")
    endif()
endif()
