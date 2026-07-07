cmake_minimum_required(VERSION 3.20)

# Super OmniKiller — a Mario-like side-scroller that exercises the engine broadly:
# Jolt character controller + walking enemies + the CONTACT-EVENTS API (stomp vs.
# hurt), coin pickups, multi-texture disk sprites (stb), a side-scrolling camera,
# lives/score, and audio. Windows-only (sokol DX11), guarded on sokol + stb.

find_path(SOKOL_INCLUDE_DIR NAMES sokol_gfx.h)
find_package(Stb QUIET)
if(SOKOL_INCLUDE_DIR AND Stb_FOUND AND WIN32)
    set(RENDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/okn-render")
    add_executable(mario WIN32
        "${CMAKE_CURRENT_SOURCE_DIR}/mario_app.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/mario.manifest"  # PerMonitorV2 DPI awareness
        "${RENDER_DIR}/gpu/gpu_sprite_renderer.cpp"
        "${RENDER_DIR}/gpu/sokol_impl_app.cpp")
    target_include_directories(mario PRIVATE
        "${RENDER_DIR}/include" "${SOKOL_INCLUDE_DIR}" "${Stb_INCLUDE_DIR}")
    target_link_libraries(mario PRIVATE okn-math okn-audio okn-physics okn-input)
    target_compile_features(mario PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(mario PRIVATE /WX-)   # sokol/stb headers aren't /WX-clean
    endif()
endif()
