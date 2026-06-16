cmake_minimum_required(VERSION 3.20)

# Flappy Bird — a game built on the OmniKillerNexus engine. Reuses okn-render's
# sokol_gfx sprite renderer + D3D11 sokol app TU, okn-audio for sound, and the
# header-only 2D sprite path. Windows-only (sokol DX11), guarded on sokol.

find_path(SOKOL_INCLUDE_DIR NAMES sokol_gfx.h)
if(SOKOL_INCLUDE_DIR AND WIN32)
    set(RENDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/okn-render")
    add_executable(flappy WIN32
        "${CMAKE_CURRENT_SOURCE_DIR}/flappy_app.cpp"
        "${RENDER_DIR}/gpu/gpu_sprite_renderer.cpp"
        "${RENDER_DIR}/gpu/sokol_impl_app.cpp")
    target_include_directories(flappy PRIVATE
        "${RENDER_DIR}/include" "${SOKOL_INCLUDE_DIR}")
    target_link_libraries(flappy PRIVATE okn-math okn-audio)
    target_compile_features(flappy PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(flappy PRIVATE /WX-)   # sokol headers aren't /WX-clean
    endif()
endif()
