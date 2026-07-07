cmake_minimum_required(VERSION 3.20)

# OmniHarvest — a Stardew-Valley-like 2D TILE-GRID farming sim. A different-genre
# framework test: top-down, grid-based, and deliberately using NO physics — it links
# only okn-math + okn-audio + okn-input + the 2D GPU sprite path, proving that stack
# alone carries a whole game. Windows-only (sokol DX11), guarded on sokol + stb.

find_path(SOKOL_INCLUDE_DIR NAMES sokol_gfx.h)
find_package(Stb QUIET)
if(SOKOL_INCLUDE_DIR AND Stb_FOUND AND WIN32)
    set(RENDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/okn-render")
    add_executable(harvest WIN32
        "${CMAKE_CURRENT_SOURCE_DIR}/harvest_app.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/harvest.manifest"  # PerMonitorV2 DPI awareness
        "${RENDER_DIR}/gpu/gpu_sprite_renderer.cpp"
        "${RENDER_DIR}/gpu/sokol_impl_app.cpp")
    target_include_directories(harvest PRIVATE
        "${RENDER_DIR}/include" "${SOKOL_INCLUDE_DIR}" "${Stb_INCLUDE_DIR}")
    target_link_libraries(harvest PRIVATE okn-math okn-audio okn-input)
    target_compile_features(harvest PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(harvest PRIVATE /WX-)   # sokol/stb headers aren't /WX-clean
    endif()
endif()
