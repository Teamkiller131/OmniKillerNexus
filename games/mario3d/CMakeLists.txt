cmake_minimum_required(VERSION 3.20)

# Super OmniKiller 3D — a true-3D Mario-like on the engine's new mesh3d renderer
# (perspective camera + depth buffer + lit boxes on sokol_gfx) and Jolt in full 3D.
# Windows-only (sokol DX11), guarded on sokol.

find_path(SOKOL_INCLUDE_DIR NAMES sokol_gfx.h)
if(SOKOL_INCLUDE_DIR AND WIN32)
    set(RENDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/okn-render")
    add_executable(mario3d WIN32
        "${CMAKE_CURRENT_SOURCE_DIR}/mario3d_app.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/mario3d.manifest"  # PerMonitorV2 DPI awareness
        "${RENDER_DIR}/mesh3d/mesh_renderer.cpp"
        "${RENDER_DIR}/gpu/sokol_impl_app.cpp")
    target_include_directories(mario3d PRIVATE
        "${RENDER_DIR}/include" "${SOKOL_INCLUDE_DIR}")
    target_link_libraries(mario3d PRIVATE okn-math okn-audio okn-physics)
    target_compile_features(mario3d PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(mario3d PRIVATE /WX-)   # sokol headers aren't /WX-clean
    endif()
endif()
