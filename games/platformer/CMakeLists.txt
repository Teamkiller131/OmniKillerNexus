cmake_minimum_required(VERSION 3.20)

# Platformer — the roadmap-v2 forcing-function game (P5+P6+P7). Jolt character
# controller + input action-map + a disk-loaded sprite (stb) + binary progress save.
# Windows-only (sokol DX11), guarded on sokol + stb.

find_path(SOKOL_INCLUDE_DIR NAMES sokol_gfx.h)
find_package(Stb QUIET)
if(SOKOL_INCLUDE_DIR AND Stb_FOUND AND WIN32)
    set(RENDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/okn-render")
    add_executable(platformer WIN32
        "${CMAKE_CURRENT_SOURCE_DIR}/platformer_app.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/platformer.manifest"  # PerMonitorV2 DPI awareness
        "${RENDER_DIR}/gpu/gpu_sprite_renderer.cpp"
        "${RENDER_DIR}/gpu/sokol_impl_app.cpp")
    target_include_directories(platformer PRIVATE
        "${RENDER_DIR}/include" "${SOKOL_INCLUDE_DIR}" "${Stb_INCLUDE_DIR}")
    target_link_libraries(platformer PRIVATE okn-math okn-audio okn-physics okn-input)
    target_compile_features(platformer PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(platformer PRIVATE /WX-)   # sokol/stb headers aren't /WX-clean
    endif()

    # P15: levels authored in a hot-reloadable Lua file (sol2/Lua, like the slice). Optional
    # — without sol2/Lua the game uses its built-in levels.
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
