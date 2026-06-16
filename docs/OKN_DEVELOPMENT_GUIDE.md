# OmniKillerNexus 开发大纲

## 一、项目总览

OmniKillerNexus 是一个模块化的商用级跨平台游戏引擎框架，先聚焦 2D/2.5D RPG 游戏的完整开发管线，远期规划支持 3D 游戏。覆盖渲染、音频、物理、ECS、网络、脚本、UI、编辑器等完整游戏开发生态。

| 项目 | 说明 |
|------|------|
| 语言 | C++26 |
| 编译器 | MSVC + Clang-cl（均需兼容） |
| 构建 | CMake ≥ 4.3 + Visual Studio 18 2026 |
| 包管理 | vcpkg（`D:\vcpkg`），清单模式 |
| 测试框架 | doctest |
| 格式化 | clang-format（Google style） |
| 平台 | Windows（优先）→ Linux → macOS；iOS/Android 预留 |
| 架构模式 | 接口/实现分离（Service Registry + Defaults），Feature Toggle 细粒度控制 |
| 模块数 | 13 个子模块 + CLI 工具集 |
| 当前状态 | 骨架完成，仅 `okn-core/src/defaults/service_registry_default.cpp` 有真实代码 |

### 技术选型总览

| 模块 | 第三方核心库 | 选型理由 |
|------|-------------|---------|
| okn-core | 无 | 自研服务注册表（编译期 FNV-1a hash + 类型安全 wrapper） |
| okn-math | 无 | 纯自研（algebra/geometry/numeric/color/SIMD） |
| okn-memory | mimalloc（可插拔） | MIT许可，Microsoft 出品，Windows 优先优化 |
| okn-platform | 平台原生 API | Win32/POSIX 系统调用，不引入跨平台抽象库 |
| okn-ecs | 无 | 自研 Archetype/Chunk SoA + Sparse Set |
| okn-asset | assimp + stb_image + basisu | 行业标准导入/解码/压缩管线 |
| okn-render | 无（裸写后端） | 自研 D3D12 → Vulkan → Metal，自研 Render Graph |
| okn-physics | 无（完全自研） | 物理系背景，探索新玩法物理模型 |
| okn-audio | miniaudio | MIT单头文件，9后端全覆盖，ma_node_graph对接DSP链 |
| okn-network | ASIO standalone + msquic | header-only proactor + 微软QUIC，Windows原生优化 |
| okn-script | Lua + QuickJS + CPython | 三语言运行时，绑定生成器自研 |
| okn-ui | 无（自研） | 基于 okn-render Draw List 的运行时UI |
| okn-editor | Qt 6 Quick/QML | 声明式UI开发快，效果好，C++/QML混合架构 |

---

## 二、模块分层与依赖关系

```
Layer 0 (基础设施)     okn-core        okn-math        okn-memory
                            \            |            /
Layer 1 (平台抽象)     okn-platform
                            |            |
Layer 2 (核心服务)     okn-ecs         okn-asset
                      /    |    \       /    |    \
Layer 3 (功能模块)   render network physics audio script
                      \    \    /    /   /
Layer 4 (集成层)       okn-ui    okn-editor    tools/
```

### 各层模块目标

| 模块 | 目标（target） | 说明 |
|------|---------------|------|
| okn-core | `okn-core-interfaces` (INTERFACE) + `okn-core` (STATIC) | 日志/配置/剖析/时间/定时器/序列化/GUID/服务注册表 |
| okn-math | `okn-math` (STATIC) | 向量/矩阵/四元数/几何体/插值/颜色/SIMD |
| okn-memory | `okn-memory` (STATIC) | 可插拔分配后端/arena/pool/tracking/intrusive |
| okn-platform | `okn-platform` (STATIC) | 线程/文件系统/时间/动态库/输入/虚拟内存/崩溃 |
| okn-ecs | `okn-ecs` (STATIC) | Archetype/Chunk SoA + 稀疏集/查询/调度/层级/快照 |
| okn-asset | `okn-asset` (STATIC) | 导入管线/烘焙/打包/流式/缓存/热重载 |
| okn-render | `okn-render` (STATIC) | D3D12/Vulkan/Metal/OpenGL/Render Graph/PBR/光追 |
| okn-network | `okn-network` (STATIC) | ASIO TCP/UDP + msquic QUIC/可靠层/会话/网关/热升级 |
| okn-physics | `okn-physics` (STATIC) | 刚体/CCD/碰撞/解算器/关节/角色/布料(占位)/流体(占位) |
| okn-audio | `okn-audio` (STATIC) | 基于 miniaudio 后端/DSP/空间化/流式/解码 |
| okn-script | `okn-script` (STATIC) | Lua(优先) + QuickJS + CPython 运行时/绑定生成/热重载/沙箱 |
| okn-ui | `okn-ui` (STATIC) | 运行时 UI 框架/Draw List/控件/布局/样式/动画 |
| okn-editor | `okn-editor` (STATIC) + `okn-editor-app` (EXECUTABLE) | Qt Quick/QML 编辑器/QML 面板/C++ engine 桥接/CLI 桥接 |

### 聚合 SDK（INTERFACE targets）

| SDK | 包含模块 |
|-----|---------|
| `okn-client-sdk` | core + math + memory + render + audio + ui + ecs + script + network + asset + platform + physics |
| `okn-server-sdk` | core + math + memory + network + ecs + script + asset + platform |
| `okn-editor-sdk` | core + math + memory + editor + 全部 client 模块 |

---

## 三、开发阶段

### 阶段总览

| 阶段 | 内容 | 前置 | 可并行？ | 状态 |
|------|------|------|---------|------|
| **P0** | okn-core / okn-math / okn-memory 基础设施 | 无 | 三模块可并行 | 🔴 待开 |
| **P1** | okn-platform 平台抽象 | P0 | — | ⬜ |
| **P2** | okn-ecs / okn-asset 核心服务 | P1 | 两模块可并行 | ⬜ |
| **P3** | okn-render / network / physics / audio / script | P2 | 五模块可并行 | ⬜ |
| **P4** | okn-ui / okn-editor / tools CLI | P3 | ui 和 editor 顺序依赖 | ⬜ |
| **P5** | 全模块测试 + 性能优化 + 跨平台验证 | P4 | 自动测试可并行 | ⬜ |

### P0 — 基础设施层（当前阶段）

目标：三个最底层模块可独立编译、可运行示例、单测通过。

#### P0.1 — okn-core 补全

当前状态：只有 `service_registry` 有实现。`logger.hpp` / `config.hpp` / `profiler.hpp` / `memory.hpp` / `time.hpp` / `timer.hpp` / `hash.hpp` / `guid.hpp` / `id.hpp` / `platform.hpp` / `result.hpp` / `string_utils.hpp` / `types.hpp` 等 19 个 API 头文件全部为空（0 字节）。

实现顺序：
1. 补全 API 头文件：`types.hpp` → `hash.hpp` → `guid.hpp` → `id.hpp` → `result.hpp` → `platform.hpp` → `string_utils.hpp`
2. 补全核心接口：`logger.hpp` → `config.hpp` → `time.hpp` → `timer.hpp` → `profiler.hpp` → `memory.hpp`
3. 实现 defaults：`logger_default` → `config_default` → `time_default` → `timer_default` → `profiler_default` → `memory_default`
4. 补全 `core_factory.hpp/cpp` 工厂函数
5. 编写 samples 和 tests

#### P0.2 — okn-math 实现

纯数学模块，无外部依赖，可与 core 完全并行。

实现顺序：
1. `common/`：`constants.hpp`、`types.hpp`（别名）、`traits.hpp`
2. `algebra/`：`vec2.hpp/cpp` → `vec3.hpp/cpp` → `vec4.hpp/cpp` → `mat3.hpp/cpp` → `mat4.hpp/cpp` → `quat.hpp/cpp` → `transform.hpp/cpp`
3. `geometry/`：`aabb.hpp/cpp` → `sphere.hpp/cpp` → `plane.hpp/cpp` → `ray.hpp/cpp` → `triangle.hpp/cpp` → `frustum.hpp/cpp` → `obb.hpp/cpp` → `intersection.hpp/cpp` → `bounds.hpp/cpp`
4. `numeric/`：`interpolation.hpp/cpp` → `easing.hpp/cpp` → `noise.hpp/cpp` → `random.hpp/cpp`
5. `color/`：`color.hpp/cpp` → `color_space.hpp/cpp`
6. `simd/`：`simd_base.hpp/cpp` → `simd_vec4.hpp/cpp`（SSE/AVX/NEON 封装）
7. `util/`：`compare.hpp/cpp` → `hash.hpp/cpp` → `format.hpp/cpp`

#### P0.3 — okn-memory 实现

需确定后端库引入方式（FetchContent / git submodule / 系统安装）。

实现顺序：
1. 引入 mimalloc（推荐首选）或 rpmalloc 第三方库
2. `backend/`：`backend.hpp` → `sys_backend.hpp/cpp` → `mi_backend.hpp/cpp`（封装 mimalloc）→ `rp_backend.hpp/cpp`（预留）
3. `util/`：`align.hpp/cpp` → `freelist.hpp/cpp` → `handle.hpp/cpp` → `scope_alloc.hpp/cpp`
4. `alloc/`：`allocator.hpp/cpp` → `tag_allocator.hpp/cpp` → `proxy_allocator.hpp/cpp` → `stl_adapter.hpp/cpp` → `override_new.hpp/cpp`
5. `arena/`：`linear_arena.hpp/cpp` → `stack_arena.hpp/cpp` → `segmented_arena.hpp/cpp`
6. `pool/`：`fixed_block_pool.hpp/cpp` → `object_pool.hpp/cpp` → `small_object_pool.hpp/cpp`
7. `tracking/`：`memory_stats.hpp/cpp` → `memory_tracker.hpp/cpp` → `leak_detector.hpp/cpp` → `guard_page.hpp/cpp`
8. `intrusive/`：`list.hpp/cpp` → `spsc_queue.hpp/cpp`

---

### P1 — 平台抽象层

依赖：okn-core + okn-math + okn-memory

平台优先顺序：**Windows → Linux → macOS**（iOS/Android 仅预留编译路径）

实现顺序：
1. `api.hpp` + `config.hpp` + `platform.hpp`（平台探测宏、ISA 检测）
2. `time/`：`clock.hpp/cpp` + `stopwatch.hpp/cpp`（对接 okn-core profile）
3. `thread/`：`thread.hpp/cpp` → `thread_name.hpp/cpp` → `thread_affinity.hpp/cpp` → `tls.hpp/cpp` → `sync.hpp/cpp` → `task_queue.hpp/cpp` → `thread_pool.hpp/cpp` → `job_adapter.hpp/cpp`
4. `fs/`：`path.hpp/cpp` → `file.hpp/cpp` → `mmap.hpp/cpp` → `temp.hpp/cpp` → `fs_info.hpp/cpp`
5. `system_info/`：`cpu_features.hpp/cpp` + `system_info.hpp/cpp`
6. `dll/`：`dynamic_library.hpp/cpp` + `symbol.hpp/cpp`
7. `vm/`：`vm.hpp/cpp` + `vm_info.hpp/cpp`（为 okn-memory 提供虚拟内存钩子）
8. `encoding/`：`codec.hpp/cpp` + `locale.hpp/cpp`
9. `path/`：`path_normalize.hpp/cpp`
10. `crash/`：`crash_handler.hpp/cpp` → `minidump.hpp/cpp`（Windows Minidump / Linux signal handler）→ `signals.hpp/cpp`
11. `input/`：`input.hpp/cpp` + `input_events.hpp/cpp`
12. `hooks/`：`memory_hooks.hpp/cpp` → `physics_hooks.hpp/cpp` → `profile_hooks.hpp/cpp`

---

### P2 — 核心服务层

依赖：okn-core + okn-math + okn-memory + okn-platform

#### P2.1 — okn-ecs 实现

实现顺序：
1. 基础类型：`entity.hpp/cpp` → `entity_id.hpp/cpp` → `component.hpp/cpp` → `ecs_types.hpp/cpp`
2. 存储核心：`archetype/archetype.hpp/cpp` → `chunk.hpp/cpp` → `chunk_allocator.hpp/cpp` → `chunk_view.hpp/cpp`
3. 稀疏集：`sparse_set.hpp/cpp` → `sparse_storage.hpp/cpp` → `storage.hpp/cpp` → `storage_registry.hpp/cpp`
4. 查询：`filter.hpp/cpp` → `query.hpp/cpp` → `relation.hpp/cpp` → `view.hpp/cpp`
5. World：`world.hpp/cpp` → `world_builder.hpp/cpp`
6. 调度：`system.hpp/cpp` → `system_graph.hpp/cpp` → `scheduler.hpp/cpp` → `job_adapter.hpp/cpp`
7. 层级：`transform.hpp/cpp` → `hierarchy.hpp/cpp`
8. 事件：`event_queue.hpp/cpp` → `event_bus.hpp/cpp`
9. 快照（可选）：`snapshot.hpp/cpp` → `rollback.hpp/cpp`
10. 序列化/反射/脚本桥：`serialize.hpp/cpp` → `registry.hpp/cpp` → `scripting_bridge.hpp/cpp`
11. 生命周期/度量/观测钩子：`lifecycle.hpp/cpp` → `metrics.hpp/cpp` → `debug_view.hpp/cpp` → `profile/log/memory hooks`
12. CLI：`cli_commands.hpp/cpp`

#### P2.2 — okn-asset 实现

实现顺序：
1. 基础类型：`asset_types.hpp` → `asset_id.hpp` → `api.hpp` + `config.hpp`
2. 格式导入器：`model_importer` → `texture_importer` → `material_importer` → 其他格式（FBX/GLTF/OBJ/PNG/JPG/WAV/OGG 等可依赖第三方库）
3. 管线：`mesh_pipeline` → `texture_pipeline` → `animation_pipeline` → 其他管线
4. 打包：`pack_file` → `pack_index` → `pack_reader` → `pack_writer`
5. 注册表：`asset_registry` → `dependency_graph`
6. IO 与流式：`asset_io` → `stream` → `async_loader` → `mip_streamer` → `chunk_streamer` → `upload_queue`
7. 缓存：`cache_manager` → `lru` → `ref_count` → `budget`
8. 热重载：`hot_reload`
9. 序列化/报告/变体：`serialize` → `build_report` → `platform_variant` → `quality_profile`
10. 观测钩子与 CLI：`log/profile/memory hooks` → `cli_commands`

---

### P3 — 功能模块层（五路可并行）

#### P3.1 — okn-render

核心路径（推荐先聚焦一个后端 + 基础管线）：
1. 设备/后端抽象 → 优先实现 D3D12，再补 Vulkan/Metal/OpenGL
2. 资源/内存/描述符 → Buffer/Texture/Swapchain
3. 着色器系统 → HLSL 编译/反射/变体
4. Render Graph → 声明式依赖 + 自动 barrier
5. 基础 PBR 管线 → Forward 模式
6. 光照/阴影 → IBL + CSM
7. 后处理 → HDR/Tonemap/Bloom/TAA
8. 可见性/剔除 → 视锥 + HiZ
9. 计算/光追/流式（按需）

#### P3.2 — okn-network

底层库：ASIO standalone（header-only，TCP/UDP proactor）+ msquic（QUIC）。vcpkg 引入。

核心路径：
1. 通过 vcpkg 引入 `asio`、`msquic`
2. ASIO Transport Adapter → `transport/tcp_acceptor.hpp` → `transport/udp_socket.hpp`（封装 ASIO proactor，对接 okn-platform 线程池）
3. 零拷贝 Buffer 链 → `buffer/` 基于 ASIO `const_buffer_sequence`/`mutable_buffer_sequence` 设计
4. 可靠性层（自研）→ 序号窗口/乱序重排/ACK-NACK/重传，在 UDP 之上构建
5. msquic QUIC Transport → `transport/quic_stream.hpp`（封装 msquic listener/connection/stream）
6. Session → 登录/鉴权/续期/踢出/重放保护/连接复用/多路复用
7. QoS/流控 → 优先级队列发送/背压/水位
8. 路由/网关/热升级（占位阶段可跳过）
9. Fault inject + recorder/replayer 测试工具
10. msgpack 或自研序列化用于帧编解码

#### P3.3 — okn-physics

核心路径：
1. 刚体/运动学体 → 类型定义/接口
2. 碰撞形状 → Box/Sphere/Capsule/Cylinder/Cone/Hull/Mesh/Heightfield
3. Broadphase → SAP/BVH/DBVT
4. 窄相 → GJK/EPA/SAT + Raycast/Sweep/Overlap
5. 解算器 → PGS/Sequential Impulses + 岛屿/睡眠
6. CCD → TOI 计算
7. 关节 → Ball/Hinge/Slider/Distance/6DoF
8. 角色控制器 → 胶囊/坡度/阶梯
9. 布料/流体（占位，后期）

#### P3.4 — okn-audio

底层库：miniaudio（MIT 单头文件，WASAPI/XAudio2/CoreAudio/ALSA/PulseAudio/AAudio/OpenSL/ASIO 全后端 + WAV/FLAC/OGG/MP3 解码器）。vcpkg 引入或直接嵌入。

核心路径：
1. 引入 miniaudio，封装 `ma_engine` 为 `audio_engine` 高层接口
2. backend 抽象层 → 基于 miniaudio 的 `ma_device`/`ma_context`，后端切换通过 config
3. 基础解码器集合 → 封装 miniaudio `ma_decoder` 支持 WAV/FLAC/OGG/MP3
4. DSP 节点图 → 基于 miniaudio `ma_node_graph` 构建 EQ/Compressor/Limiter/Reverb/Delay/Filter 等节点，串联为效果链
5. 混音/总线 → `ma_splitter_node` 实现多总线/子混音，`ma_channel_converter` 处理声道
6. 3D 空间化（自研）→ 距离衰减/HRTF/空气吸收/遮挡/Doppler/监听器，在 miniaudio device callback 中手动处理空间化
7. 流式播放/异步解码 → 双缓冲 + background decode thread
8. Asset 桥接 → 对接 okn-asset 的音频包/变体/热重载

#### P3.5 — okn-script

核心路径：
1. Lua 运行时优先 → VM/Context/模块加载
2. 绑定生成器 → 反射注册表 → auto glue
3. 热重载 → 状态迁移策略
4. JS/Python 运行时（按需，可后补）
5. 沙箱/配额/调试器
6. 事件桥接 → ECS/physics/render

---

### P4 — 集成层

#### P4.1 — okn-ui

自研运行时 UI 框架，基于 okn-render Draw List：
1. ui_core → node tree / rect / transform / visibility
2. Draw List → 批次合并 / 裁剪 / atlas / font atlas，通过 okn-render 提交绘制
3. 基础控件 → Button / Text / Image / Checkbox / Slider / List / Tree / Scroll / Progress / Input / Container
4. 布局 → 线性 / 网格 / Flex（锚点/DPI 适配）
5. 样式/主题 → 状态样式（normal/hover/pressed/disabled/focused），主题管理/切换
6. 输入路由 → 鼠标/键盘/触摸/手柄事件分发，焦点管理，命中测试
7. 动画 → 补间/时间轴，控件状态过渡
8. 脚本/ECS/Asset 桥接

#### P4.2 — okn-editor（Qt Quick / QML）

架构：C++ 引擎层（okn-editor-core）+ QML 声明式 UI 层。Qt 6 Quick，vcpkg 引入 `qtbase`、`qtdeclarative`、`qtquickcontrols2`。

编辑器总架构：
```
QML Layer (声明式 UI)          C++ Layer (引擎桥接)
─────────────────────────────  ─────────────────────────────
main.qml (主窗口/QML Application)  editor_app (QGuiApplication + QQmlApplicationEngine)
├── MainWindow.qml                 ├── EditorEngine (C++ context property, 暴露给 QML)
│   ├── MenuBar.qml                │   ├── ProjectManager
│   ├── ToolBar.qml                │   ├── AssetRegistry → 对接 okn-asset
│   ├── DockingLayout.qml          │   ├── ECSBridge     → 对接 okn-ecs
│   │   ├── HierarchyPanel.qml     │   ├── RenderBridge   → 对接 okn-render (视口)
│   │   ├── InspectorPanel.qml     │   ├── PhysicsBridge  → 对接 okn-physics
│   │   ├── Viewport.qml           │   ├── AudioBridge    → 对接 okn-audio
│   │   ├── AssetBrowser.qml       │   ├── ScriptBridge   → 对接 okn-script
│   │   ├── ConsolePanel.qml       │   ├── NetworkBridge  → 对接 okn-network
│   │   ├── ProfilerPanel.qml      │   └── CommandManager (Undo/Redo)
│   │   └── ...其他面板              │
│   └── StatusBar.qml              └── QML type registrations
```

实现顺序：
1. 搭建 Qt Quick 最小框架 → `editor_app` + `main.qml` 显示空窗口
2. C++ EditorEngine 注册为 QML context property，CommandManager (Undo/Redo)
3. RenderBridge → 将 okn-render 视口嵌入 QML `QQuickItem`（`QQuickFramebufferObject` 或自定义 `QSGRenderNode`）
4. ECSBridge → 暴露 World/Entity/Component 模型给 QML（通过 `QAbstractListModel` 或 `Q_PROPERTY`）
5. 核心面板 QML 实现 → Hierarchy (TreeView) → Inspector (动态属性表) → AssetBrowser (列表/预览) → Console
6. Dock 布局 → Qt Quick `DockWidget` 或自研 QML docking 容器
7. 其他面板 → Animation/Physics/Material/Shader/Audio/Script/Profiler 等 QML 面板
8. 主题/I18N/设置 → QML theme engine + `qsTr()`
9. 插件系统 → QML `QQmlExtensionPlugin` + C++ plugin loader
10. CLI 桥接 → EditorEngine 调用 CLI 工具（shader compile/asset pack 等）

QML 交互模式：
```qml
// InspectorPanel.qml 示例
ListView {
    model: EditorEngine.selection.components  // C++ model
    delegate: ComponentProperties {
        onPropertyChanged: EditorEngine.commandManager.execute(
            "set_component_property", {entity: id, component: name, property: key, value: val}
        )
    }
}
```

#### P4.3 — tools/ CLI

mono CLI → asset/shader/script/net/audio/ui 子工具（对应 7 个占位 main.cpp），与 okn-editor 的 CLI 桥接联动。

---

### P5 — 测试与优化

1. 全模块单元测试填充（~90 个 test_*.cpp 占位文件 → 全部实现）
2. 所有 samples（~55 个占位）实现为可运行的完整示例
3. 性能剖析与瓶颈优化
4. 跨平台编译验证（Linux/macOS CI）
5. 安装/导出支持（当前 `OKN_ENABLE_INSTALL=OFF`，需在各模块补齐 install 规则）

---

## 四、开发规范

### 4.1 接口/实现分离模式

所有模块应遵循 `okn-core` 的 service_registry 模式：

```
include/okn/<module>/api/       ← 接口头（纯虚/CRTP，无三方依赖，不含实现）
include/okn/<module>/impl/      ← 可选，默认实现对外声明（工厂函数）
src/defaults/                   ← 默认实现源码
src/                            ← 其他实现源码
```

使用方式：
```cpp
// 仅需接口
target_link_libraries(<t> PUBLIC okn-core-interfaces)

// 需默认实现
target_link_libraries(<t> PRIVATE okn-core)

// 自定义实现可替换默认
registry.register_service<ILogger>(&my_custom_logger);
```

### 4.2 代码风格

- C++26 标准，不使用编译器扩展（`CMAKE_CXX_EXTENSIONS OFF`）
- 文件名：蛇形命名（`asset_registry.hpp`、`memory_tracker.cpp`）
- 类/结构体/枚举：**PascalCase**（`class AssetRegistry;`、`struct ProfileSample`、`enum class LogLevel`），接口前缀 `I`（`class ILogger`）—— 与 AGENTS.md §1.1 及现有代码一致
- 函数/方法：蛇形命名（`void register_service();`）
- 变量/参数/成员：蛇形命名（私有成员尾下划线表示，如 `min_level_`）
- 模板参数：使用 `class T` 关键字
- 命名空间：`okn::<module>`
- 头文件防护：`#pragma once`（不使用宏防护）
- 平台条件编译：`#ifdef _WIN32` / `#elif defined(__linux__)` / `#elif defined(__APPLE__)`
- clang-format：Google style，根目录 `.clang-format` 文件

### 4.3 CMake 规范

- 每个模块一个 `project(okn-<name> LANGUAGES CXX)`
- Feature toggle 用 `option()`，通过 `target_compile_definitions` 传导
- 禁止在子模块中修改全局 CMake 状态（如全局 CXX_STANDARD）
- 第三方库通过 vcpkg 清单模式引入（`vcpkg.json` + CMake `find_package`）
- vcpkg 根路径：`D:\vcpkg`，CMake 配置时指定 `-DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake`
- Samples 和 Tests 各自有独立的 `CMakeLists.txt`，受 `BUILD_SAMPLES` / `BUILD_TESTS` 控制

### 4.4 测试要求

- 测试框架：**doctest**（vcpkg 引入 `doctest`）
- 每个核心 `.cpp` 对应一个 `test_<name>.cpp`
- 覆盖正常路径 + 边界条件 + 错误路径
- Tests 目录结构对应 include 目录结构
- 测试示例：
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("vec3 addition") {
    auto a = vec3{1, 2, 3};
    auto b = vec3{4, 5, 6};
    CHECK(a + b == vec3{5, 7, 9});
}
```

### 4.5 vcpkg 集成指南

根 `vcpkg.json`（后续创建）:
```json
{
    "name": "okn",
    "version": "0.1.0",
    "dependencies": [
        "mimalloc",
        "miniaudio",
        "asio",
        "msquic",
        "doctest",
        "assimp",
        "stb",
        "basisu",
        "lua",
        "quickjs",
        "qtbase",
        "qtdeclarative",
        "qtquickcontrols2"
    ]
}
```

CMake 配置命令:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### 4.6 平台支持策略

| 平台 | 优先级 | 说明 |
|------|--------|------|
| Windows | P0 | 主开发平台 |
| Linux | P1 | 需 CI 验证 |
| macOS | P1 | 需 CI 验证 |
| iOS | P2 | 预留条件编译 |
| Android | P2 | 预留条件编译 |

---

## 五、里程碑

| 里程碑 | 验收标准 | 预计达到阶段 |
|--------|---------|-------------|
| **M0** | okn-core / okn-math / okn-memory 三模块：编译通过 + 示例可运行 + 单测全部通过 | P0 完成 |
| **M1** | okn-platform 编译通过；Windows 平台线程/文件/时间/动态库可用 | P1 完成 |
| **M2** | okn-ecs + okn-asset 编译通过；基础 ECS sample + 资源导入 sample 跑通 | P2 完成 |
| **M3** | 5 个功能模块至少 2 个可编译、可运行基础示例 | P3 中期 |
| **M4** | 全模块编译通过；okn-editor-app 可启动并显示空窗口 | P4 完成 |
| **M5** | CI 集成通过；跨平台（Win/Linux/macOS）编译验证 | P5 完成 |

---

## 六、常见问题

- **重复目标名**：各模块的库/样例/测试目标名使用各自前缀（如 `okn-asset_samples`）
- **找不到 editor main**：`okn-editor-app` 入口在 `modules/okn-editor/src/main.cpp`，顶层不重复定义
- **子模块未拉取**：`git submodule update --init --recursive`
- **新增子模块后**：需同时在根 `CMakeLists.txt` 添加 `add_subdirectory()` 并在对应 SDK target 中添加 `target_link_libraries`
