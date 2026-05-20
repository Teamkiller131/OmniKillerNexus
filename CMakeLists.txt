cmake_minimum_required(VERSION 3.20)
project(OmniKillerNexus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
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

# 安装/导出暂不启用，避免依赖未导出目标导致的错误
# 若未来需要安装，请确保各模块都有 install(TARGETS ... EXPORT OmniKillerNexusTargets)
# 并把它们加入同一个 EXPORT，再开启 OKN_ENABLE_INSTALL