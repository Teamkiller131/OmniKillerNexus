cmake_minimum_required(VERSION 3.20)

option(OKN_CLI_MONO "Build single okn-cli binary with subcommands" ON)
option(OKN_CLI_ASSET "Build okn-cli-asset" ON)
option(OKN_CLI_SHADER "Build okn-cli-shader" ON)
option(OKN_CLI_SCRIPT "Build okn-cli-script" ON)
option(OKN_CLI_NET "Build okn-cli-net" ON)
option(OKN_CLI_AUDIO "Build okn-cli-audio" ON)
option(OKN_CLI_UI "Build okn-cli-ui" ON)

# Mono CLI (聚合子命令的入口需自行实现)
if(OKN_CLI_MONO)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cli_main.cpp")
        add_executable(okn-cli cli_main.cpp)
        target_link_libraries(okn-cli PRIVATE okn-asset okn-render okn-audio okn-ecs okn-script okn-ui okn-network okn-platform)
    endif()
endif()

# 独立子工具示例（需要相应源文件存在才会构建）
if(OKN_CLI_ASSET AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asset_main.cpp")
    add_executable(okn-cli-asset asset_main.cpp)
    target_link_libraries(okn-cli-asset PRIVATE okn-asset okn-platform)
endif()

if(OKN_CLI_SHADER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shader_main.cpp")
    add_executable(okn-cli-shader shader_main.cpp)
    target_link_libraries(okn-cli-shader PRIVATE okn-render okn-asset okn-platform)
endif()

if(OKN_CLI_SCRIPT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/script_main.cpp")
    add_executable(okn-cli-script script_main.cpp)
    target_link_libraries(okn-cli-script PRIVATE okn-script okn-platform)
endif()

if(OKN_CLI_NET AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/net_main.cpp")
    add_executable(okn-cli-net net_main.cpp)
    target_link_libraries(okn-cli-net PRIVATE okn-network okn-platform)
endif()

if(OKN_CLI_AUDIO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio_main.cpp")
    add_executable(okn-cli-audio audio_main.cpp)
    target_link_libraries(okn-cli-audio PRIVATE okn-audio okn-asset okn-platform)
endif()

if(OKN_CLI_UI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ui_main.cpp")
    add_executable(okn-cli-ui ui_main.cpp)
    target_link_libraries(okn-cli-ui PRIVATE okn-ui okn-asset okn-platform)
endif()

# 安装可执行（若启用顶层 OKN_ENABLE_INSTALL）
if(OKN_ENABLE_INSTALL)
    foreach(t okn-cli okn-cli-asset okn-cli-shader okn-cli-script okn-cli-net okn-cli-audio okn-cli-ui)
        if(TARGET ${t})
            install(TARGETS ${t}
                    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            )
        endif()
    endforeach()
endif()