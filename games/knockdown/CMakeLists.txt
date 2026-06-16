cmake_minimum_required(VERSION 3.20)

# Knockdown — an Angry-Birds-style physics game on the OmniKillerNexus engine.
# Reuses okn-render's sokol_gfx sprite renderer + D3D11 sokol app TU and okn-audio,
# and drives gameplay with okn-physics (Jolt backend) — including the contact-events
# API (IPhysicsWorld::drain_contacts) for impact sounds + scoring. Windows-only
# (sokol DX11), guarded on sokol.

find_path(SOKOL_INCLUDE_DIR NAMES sokol_gfx.h)
if(SOKOL_INCLUDE_DIR AND WIN32)
    set(RENDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/okn-render")
    add_executable(knockdown WIN32
        "${CMAKE_CURRENT_SOURCE_DIR}/knockdown_app.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/knockdown.manifest"  # PerMonitorV2 DPI awareness
        "${RENDER_DIR}/gpu/gpu_sprite_renderer.cpp"
        "${RENDER_DIR}/gpu/sokol_impl_app.cpp")
    target_include_directories(knockdown PRIVATE
        "${RENDER_DIR}/include" "${SOKOL_INCLUDE_DIR}")
    target_link_libraries(knockdown PRIVATE okn-math okn-audio okn-physics)
    target_compile_features(knockdown PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(knockdown PRIVATE /WX-)   # sokol headers aren't /WX-clean
    endif()
endif()
