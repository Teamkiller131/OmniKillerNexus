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
    # VOIDBORNE runs on the engine core where it genuinely fits:
    #   okn-ecs    — the crew are real ECS entities + CrewStats components; the
    #                captain election queries the crew World.
    #   okn-asset  — all data files load through AssetIO; events.json is hot-reloaded.
    #   okn-math   — the smoothed camera is an engine Vec2.
    # (okn-core / okn-platform come transitively and are used for real.) The UI
    # stays on Dear ImGui/UniGUI and the tile world is grid-collision, so
    # okn-render / okn-ui / okn-physics are intentionally NOT forced — a management
    # sim doesn't use a sprite renderer or rigid bodies, and bolting them on would
    # be cosmetic.
    target_link_libraries(voidborne PRIVATE
        unigui::unigui okn-ecs okn-asset okn-math)
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

    # Download-and-play bundle: exe + resolved runtime DLLs + data/ + assets/
    # (the same layout the post-build staging gives a dev run).
    if(OKN_PACKAGE_GAMES)
        install(TARGETS voidborne
                RUNTIME_DEPENDENCIES
                    DIRECTORIES "${CMAKE_BINARY_DIR}/bin"
                    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
                    POST_EXCLUDE_REGEXES ".*[Ss]ystem32.*"
                RUNTIME DESTINATION . COMPONENT voidborne)
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data"
                DESTINATION . COMPONENT voidborne)
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                DESTINATION . COMPONENT voidborne)
    endif()
else()
    message(STATUS "voidborne: unigui::unigui not found — skipping (needs the OKN root build)")
endif()
