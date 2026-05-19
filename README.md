# OmniKillerNexus

顶层聚合工程，整合 okn-* 全家桶，产物包括：
- **okn-client-sdk**：客户端开发库（render/ui/audio/ecs/script/network/asset/platform）。
- **okn-server-sdk**：服务端开发库（network/ecs/script/asset/platform）。
- **okn-editor-sdk**：编辑器扩展开发库（editor + 全模块桥接）。
- **okn-editor-app**：编辑器可执行（定义于 `modules/okn-editor`）。
- **CLI 工具**：可选构建，位于 `tools/`。

## 目录结构
- `modules/`：各子模块（以子模块或子仓库形式存在）
    - `okn-asset/`
    - `okn-render/`
    - `okn-audio/`
    - `okn-ecs/`
    - `okn-script/`
    - `okn-ui/`
    - `okn-network/`
    - `okn-editor/`（含 okn-editor-app 入口）
    - `okn-platform/`
- `tools/`：CLI 工具源及 CMake。
- `cmake/`：CMake config 模板等。
- `external/`：第三方依赖占位。
- `sdk/`：SDK 打包相关占位。
- `dist/`：构建输出占位。
- `docs/`：文档占位。

## 子模块添加示例
```bash
git submodule add <repo-url> modules/okn-asset
# 其他模块同理
git submodule update --init --recursive
```
若目录已有同名仓库残留，可先删除目录或加 `--force`。

## 构建要求
- CMake ≥ 3.20
- C++26 编译器
- Ninja / Make / MSVC 均可

## 顶层 CMake 选项
- `OKN_ENABLE_CLIENT_SDK` (ON)
- `OKN_ENABLE_SERVER_SDK` (ON)
- `OKN_ENABLE_EDITOR_SDK` (ON)
- `OKN_BUILD_EDITOR_EXE` (ON) —— 使用子模块中定义的 `okn-editor-app`
- `OKN_BUILD_CLI` (ON)
- `OKN_ENABLE_INSTALL` (OFF 默认；未为各模块配置 install 时请保持关闭)

## 快速构建
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

## 可执行与库
- 可执行：`okn-editor-app`（由 `modules/okn-editor` 定义）
- 接口库（INTERFACE）：`okn-client-sdk`, `okn-server-sdk`, `okn-editor-sdk`
    - 注意：安装导出目前默认关闭，如需安装，需要在各模块补充 `install(TARGETS ... EXPORT OmniKillerNexusTargets)` 并开启 `OKN_ENABLE_INSTALL`.

## CLI 工具
- 见 `tools/` 下的 CMakeLists，可按开关选择构建单一 `okn-cli` 或拆分若干子工具（asset/shader/script/net/audio/ui）。

## 常见问题
- **重复目标名**：确保各模块的库/样例/测试目标名使用各自前缀（如 `okn-asset`, `okn-asset_samples`），不要复用 `okn-editor` 等名字。
- **找不到 editor main**：`okn-editor-app` 的入口在 `modules/okn-editor/src/main.cpp`。顶层不再重复定义入口。
- **安装导出报错**：保持 `OKN_ENABLE_INSTALL=OFF`，待各模块补齐 install 规则后再开启。

## 开发提示
- 将第三方依赖放入 `external/`，或使用 FetchContent/CPM。
- 在 CI 或本地多配置构建时，使用独立的 `-B` 构建目录（如 `build-debug`, `build-release`）。