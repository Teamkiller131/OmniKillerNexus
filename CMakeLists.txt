cmake_minimum_required(VERSION 3.20)
project(OmniKillerNexus LANGUAGES CXX)

# 语言标准：C++26（全引擎统一，AGENTS.md §1.1）。MSVC 没有 /std:c++23，且
# CMake 4.x 尚未提供 CXX26↔MSVC 的映射，所以 MSVC 走下方手动注入的 /std:c++latest
# （即 C++26 预览）；其它编译器通过 CMAKE_CXX_STANDARD 直接请求 C++26。
# 实际生效标准为 C++26 —— 切勿误读下面的 23 为“目标 C++23”。
if(MSVC)
    set(CMAKE_CXX_STANDARD 23)   # CMake 将 23 映射为 /std:c++latest（见下方 add_compile_options）
else()
    set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── 输出目录（必须在 toolchain 之后强制设，vcpkg 可能覆盖） ──
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Static library output dir")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/lib" CACHE PATH "DLL output dir")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Executable output dir")
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG}  "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG}  "${CMAKE_BINARY_DIR}/bin")
endforeach()

# MSVC: /std:c++latest 映射到实际 C++26 支持
# CMake 4.3 尚不支持 CXX26 与 MSVC 的映射，需手动注入标志
if(MSVC)
    add_compile_options(/std:c++latest /W4 /WX /utf-8 /external:W0 /external:anglebrackets /wd4244)
else()
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

option(OKN_ENABLE_CLIENT_SDK "Build client SDK" ON)
option(OKN_ENABLE_SERVER_SDK "Build server SDK" ON)
option(OKN_ENABLE_EDITOR_SDK "Build editor SDK" ON)
option(OKN_BUILD_EDITOR_EXE  "Build editor executable (from okn-editor module)" ON)
option(OKN_BUILD_CLI         "Build CLI tools" ON)
option(OKN_ENABLE_INSTALL    "Enable install/export of SDKs" OFF)  # 先关闭安装

# Make the CRT explicit (dynamic /MD, matching the x64-windows vcpkg deps) so the
# whole tree — engine, Jolt/sol2/sokol, UniGUI, ImGui — shares one runtime.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ── TeamkillerUniGUI: Dear ImGui editor toolkit (powers okn-editor) ──
# Added before the modules so okn-editor can link unigui::unigui. It inherits the
# engine's /W4 /WX; relax /WX for UniGUI's own sources (it's not /WX-clean, and
# UNIGUI_WARNINGS_AS_ERRORS is off by design). Editor code includes <unigui/...>
# via angle brackets, which the root's /external:anglebrackets treats as W0.
set(UNIGUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(UNIGUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(UNIGUI_MODULE_SQLITE  OFF CACHE BOOL "" FORCE)
set(UNIGUI_MODULE_CONFIG  OFF CACHE BOOL "" FORCE)
set(UNIGUI_MODULE_IPC     OFF CACHE BOOL "" FORCE)
set(UNIGUI_MODULE_NETWORK OFF CACHE BOOL "" FORCE)
set(UNIGUI_MODULE_TRADING OFF CACHE BOOL "" FORCE)
set(UNIGUI_BACKEND_DX11   ON  CACHE BOOL "" FORCE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/TeamkillerUniGUI/CMakeLists.txt")
    add_subdirectory(third_party/TeamkillerUniGUI)
    if(MSVC AND TARGET unigui)
        target_compile_options(unigui PRIVATE /WX-)
    endif()
endif()

add_subdirectory(modules/okn-asset)
add_subdirectory(modules/okn-render)
add_subdirectory(modules/okn-audio)
add_subdirectory(modules/okn-ecs)
add_subdirectory(modules/okn-script)
add_subdirectory(modules/okn-ui)
add_subdirectory(modules/okn-network)
add_subdirectory(modules/okn-editor)
add_subdirectory(modules/okn-platform)
add_subdirectory(modules/okn-core)
add_subdirectory(modules/okn-math)
add_subdirectory(modules/okn-memory)
add_subdirectory(modules/okn-physics)

if(OKN_ENABLE_CLIENT_SDK)
    add_library(okn-client-sdk INTERFACE)
    target_link_libraries(okn-client-sdk INTERFACE
            okn-core-interfaces okn-core okn-math okn-memory
            okn-render okn-audio okn-ui okn-ecs okn-script okn-network okn-asset okn-platform okn-physics
    )
endif()

if(OKN_ENABLE_SERVER_SDK)
    add_library(okn-server-sdk INTERFACE)
    target_link_libraries(okn-server-sdk INTERFACE
            okn-core-interfaces okn-core okn-math okn-memory
            okn-network okn-ecs okn-script okn-asset okn-platform
    )
endif()

if(OKN_ENABLE_EDITOR_SDK)
    add_library(okn-editor-sdk INTERFACE)
    target_link_libraries(okn-editor-sdk INTERFACE
            okn-core-interfaces okn-core okn-math okn-memory
            okn-editor okn-render okn-audio okn-ui okn-ecs okn-script okn-network okn-asset okn-platform okn-physics
    )
endif()

if(OKN_BUILD_EDITOR_EXE)
    if(TARGET okn-editor-app)
        # 可在此添加额外属性或安装规则
    endif()
endif()

if(OKN_BUILD_CLI)
    add_subdirectory(tools)
endif()

# Games built on the engine (demos).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/games/flappy/CMakeLists.txt")
    add_subdirectory(games/flappy)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/games/knockdown/CMakeLists.txt")
    add_subdirectory(games/knockdown)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/games/platformer/CMakeLists.txt")
    add_subdirectory(games/platformer)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/games/mario/CMakeLists.txt")
    add_subdirectory(games/mario)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/games/mario3d/CMakeLists.txt")
    add_subdirectory(games/mario3d)
endif()

# 安装/导出暂不启用，避免依赖未导出目标导致的错误
# 若未来需要安装，请确保各模块都有 install(TARGETS ... EXPORT OmniKillerNexusTargets)
# 并把它们加入同一个 EXPORT，再开启 OKN_ENABLE_INSTALL