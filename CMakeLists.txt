cmake_minimum_required(VERSION 3.20)
project(OmniKillerNexus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

if(OKN_ENABLE_CLIENT_SDK)
    add_library(okn-client-sdk INTERFACE)
    target_link_libraries(okn-client-sdk INTERFACE
            okn-render okn-audio okn-ui okn-ecs okn-script okn-network okn-asset okn-platform
    )
endif()

if(OKN_ENABLE_SERVER_SDK)
    add_library(okn-server-sdk INTERFACE)
    target_link_libraries(okn-server-sdk INTERFACE
            okn-network okn-ecs okn-script okn-asset okn-platform
    )
endif()

if(OKN_ENABLE_EDITOR_SDK)
    add_library(okn-editor-sdk INTERFACE)
    target_link_libraries(okn-editor-sdk INTERFACE
            okn-editor okn-render okn-audio okn-ui okn-ecs okn-script okn-network okn-asset okn-platform
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