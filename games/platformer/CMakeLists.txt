cmake_minimum_required(VERSION 3.20)

# Platformer — the roadmap-v2 forcing-function game (P5+P6+P7), now the P15
# north-star title. Jolt character controller + input action-map + a disk-loaded
# sprite (stb) + binary progress save + Lua-authored hot-reloaded levels.
# Windows-only (sokol DX11), guarded on sokol + stb.

okn_add_sokol_game(NAME platformer LIBS okn-audio okn-physics okn-input okn-ui NEEDS_STB)

# P10: the SAME game on desktop OpenGL — the target that keeps the GLSL sprite
# shaders exercised (the Linux/WASM prep). Identical sources; only the sokol
# impl TU (GLCORE33 instead of D3D11) differs.
okn_add_sokol_game(NAME platformer-gl
                   SRC "${CMAKE_CURRENT_SOURCE_DIR}/platformer_app.cpp"
                   MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/platformer.manifest"
                   IMPL_TU gpu/sokol_impl_gl.cpp
                   LIBS okn-audio okn-physics okn-input okn-ui NEEDS_STB)

# P15: levels authored in a hot-reloadable Lua file (sol2/Lua, like the slice). Optional
# — without sol2/Lua the game uses its built-in levels. Applied to BOTH backends.
if(TARGET platformer)
    find_package(Lua QUIET)
    find_package(sol2 CONFIG QUIET)
    if(sol2_FOUND AND LUA_FOUND)
        foreach(t platformer platformer-gl)
            if(NOT TARGET ${t})
                continue()
            endif()
            target_sources(${t} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platformer_lua.cpp")
            target_link_libraries(${t} PRIVATE sol2::sol2 ${LUA_LIBRARIES})
            target_compile_definitions(${t} PRIVATE OKN_PLAT_HAS_LUA=1)
            add_custom_command(TARGET ${t} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/platformer_levels.lua"
                    "$<TARGET_FILE_DIR:${t}>/platformer_levels.lua")
        endforeach()
    endif()

    # Download-and-play bundle: exe + resolved runtime DLLs + the Lua levels.
    if(OKN_PACKAGE_GAMES)
        install(TARGETS platformer
                RUNTIME_DEPENDENCIES
                    DIRECTORIES "${CMAKE_BINARY_DIR}/bin"
                    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
                    POST_EXCLUDE_REGEXES ".*[Ss]ystem32.*"
                RUNTIME DESTINATION . COMPONENT platformer)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/platformer_levels.lua"
                DESTINATION . COMPONENT platformer)
    endif()
endif()
