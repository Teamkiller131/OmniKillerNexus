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
    target_link_libraries(platformer PRIVATE okn-math okn-audio okn-physics)
    target_compile_features(platformer PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(platformer PRIVATE /WX-)   # sokol/stb headers aren't /WX-clean
    endif()
endif()
