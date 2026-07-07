# okn_add_sokol_game — the one place the sokol-game build recipe lives.
#
# Every demo game repeated the same ~10 lines (sokol guard, WIN32 exe with the
# PerMonitorV2 DPI manifest, the shared render TUs, include dirs, C++23, /WX-).
# This function consolidates them, so widening the platform gate for the P10 GL
# backend (WIN32 → WIN32 OR LINUX + the GL impl TU) is ONE edit, not six.
#
#   okn_add_sokol_game(NAME <name>
#       [LIBS <targets...>]        # linked in addition to okn-math
#       [EXTRA_SRC <files...>]     # additional sources (absolute paths)
#       [RENDER_TU <files...>]     # render TUs relative to modules/okn-render
#                                  #   (default: gpu/gpu_sprite_renderer.cpp)
#       [NEEDS_STB])               # game #includes stb_image / stb_image_write
#
# Expects <name>_app.cpp + <name>.manifest next to the calling CMakeLists. If the
# prerequisites (sokol, stb when needed, the platform) are missing, no target is
# created — guard follow-up customization with if(TARGET <name>).
function(okn_add_sokol_game)
    cmake_parse_arguments(G "NEEDS_STB" "NAME" "LIBS;EXTRA_SRC;RENDER_TU" ${ARGN})
    if(NOT G_NAME)
        message(FATAL_ERROR "okn_add_sokol_game: NAME is required")
    endif()

    find_path(SOKOL_INCLUDE_DIR NAMES sokol_gfx.h)
    if(NOT SOKOL_INCLUDE_DIR OR NOT WIN32)   # sokol D3D11 path — P10 widens this gate
        return()
    endif()
    set(_stb_include "")
    if(G_NEEDS_STB)
        find_package(Stb QUIET)
        if(NOT Stb_FOUND)
            return()
        endif()
        set(_stb_include "${Stb_INCLUDE_DIR}")
    endif()

    set(RENDER_DIR "${CMAKE_SOURCE_DIR}/modules/okn-render")
    if(NOT G_RENDER_TU)
        set(G_RENDER_TU "gpu/gpu_sprite_renderer.cpp")
    endif()
    set(_render_srcs "")
    foreach(_tu IN LISTS G_RENDER_TU)
        list(APPEND _render_srcs "${RENDER_DIR}/${_tu}")
    endforeach()

    add_executable(${G_NAME} WIN32
        "${CMAKE_CURRENT_SOURCE_DIR}/${G_NAME}_app.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${G_NAME}.manifest"  # PerMonitorV2 DPI awareness
        ${_render_srcs}
        "${RENDER_DIR}/gpu/sokol_impl_app.cpp"
        ${G_EXTRA_SRC})
    target_include_directories(${G_NAME} PRIVATE
        "${RENDER_DIR}/include" "${SOKOL_INCLUDE_DIR}" ${_stb_include})
    target_link_libraries(${G_NAME} PRIVATE okn-math ${G_LIBS})
    target_compile_features(${G_NAME} PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(${G_NAME} PRIVATE /WX-)   # sokol/stb headers aren't /WX-clean
    endif()
endfunction()
