cmake_minimum_required(VERSION 3.20)

# VOIDBORNE (孤舟 · 青鸟号) — a generation-ship management sim ported from the
# Godot 4.6 / C# original to OmniKillerNexus. All UI + the 2D ship world run on
# TeamkillerUniGUI (Dear ImGui); the data layer reuses the original JSON via
# rapidjson. Builds only when unigui::unigui is available (i.e. the OKN root build).

if(TARGET unigui::unigui)
    find_path(RAPIDJSON_INCLUDE_DIRS NAMES rapidjson/document.h)

    file(GLOB VOIDBORNE_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    add_executable(voidborne ${VOIDBORNE_SRC})
    target_include_directories(voidborne PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src" "${RAPIDJSON_INCLUDE_DIRS}")
    target_link_libraries(voidborne PRIVATE unigui::unigui)
    target_compile_features(voidborne PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(voidborne PRIVATE /WX-)   # imgui/unigui interplay isn't /WX-clean
    endif()

    # Ship the original JSON data + the pixel font next to the executable.
    add_custom_command(TARGET voidborne POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:voidborne>/data"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:voidborne>/assets")
else()
    message(STATUS "voidborne: unigui::unigui not found — skipping (needs the OKN root build)")
endif()
