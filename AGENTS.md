# AGENTS.md — AI Agent 开发宪法

OmniKillerNexus 商用级 C++26 跨平台游戏引擎。**本文件为所有 AI Agent 的唯一宪法，写任何代码前必须先读本文件。**

---

## 一、不可违反的核心规则

### 1.1 强制规范
- **C++26**，禁用编译器扩展（`CMAKE_CXX_EXTENSIONS OFF`）
- **命名规则**：
  - 类/结构体/枚举：**PascalCase**（`class LoggerDefault`，`enum class LogLevel`，`struct ProfileSample`）
  - 接口前缀 `I`（`class ILogger`，`class IServiceRegistry`）
  - 文件名：**snake_case**，后缀 `.hpp` / `.cpp`（`logger_default.hpp`）
  - 函数/方法：**snake_case**（`auto make_default_logger()`，`float dot(const Vec3&) const`）
  - 变量/参数/成员：**snake_case**（`int entity_count`，`min_level_` 后缀下划线表示私有成员）
  - 模板参数：使用 `class T` 关键字
- **`#pragma once`** 防护，不使用 `#ifndef` 宏防护
- **命名空间** `okn::<module>`，如 `okn::math`、`okn::ecs`
- 平台条件编译 `#ifdef _WIN32` / `#elif defined(__linux__)` / `#elif defined(__APPLE__)`

### 1.2 禁止事项
- 禁止在 `.hpp` 中包含第三方库头（仅 `.cpp` 层暴露）★
- 禁止在子模块中修改全局 CMake 状态（如 `set(CMAKE_CXX_STANDARD ...)`）
- 禁止猜测尚未实现的模块的 API；只依赖 `docs/TASK_STATUS.yaml` 中标记为 `completed` 的文件
- 禁止跳过验证步骤直接声称完成

---

## 二、架构模式

所有模块遵循 **接口/实现分离** 模式。详见 `docs/patterns/service_registry.md`，核心模板如下：

### 2.1 文件组织
```
include/okn/<module>/api/     ← 纯虚接口 / 纯头文件 API（无实现，无三方依赖）
include/okn/<module>/impl/    ← 可选，默认实现的工厂函数声明
src/defaults/                 ← 默认实现
src/                          ← 其他实现
tests/                        ← doctest 单测
samples/                      ← 可运行示例
```

### 2.2 接口模板
```cpp
// include/okn/<module>/api/<name>.hpp
#pragma once
#include <okn/core/api/types.hpp>

namespace okn::<module> {

class IExample {
public:
    virtual ~IExample() = default;
    virtual auto do_something() -> okn::result<void> = 0;
};

} // namespace okn::<module>
```

### 2.3 默认实现模板
```cpp
// src/defaults/<name>_default.hpp
#pragma once
#include <okn/<module>/api/<name>.hpp>

namespace okn::<module> {

class ExampleDefault : public IExample {
public:
    explicit ExampleDefault();
    ~ExampleDefault() override;
    auto do_something() -> okn::result<void> override;
};

} // namespace okn::<module>
```

### 2.4 工厂函数模板
```cpp
// include/okn/<module>/impl/<module>_factory.hpp
#pragma once
#include <memory>
#include <okn/<module>/api/<name>.hpp>

namespace okn::<module> {

[[nodiscard]] auto make_default_example() -> std::unique_ptr<IExample>;

} // namespace okn::<module>
```

### 2.5 测试模板
```cpp
// tests/test_<name>.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <okn/<module>/api/<name>.hpp>
#include <src/defaults/<name>_default.hpp>  // 仅测试时包含

TEST_CASE("okn::<module>::<name> - basic operation") {
    auto obj = okn::<module>::make_default_<name>();
    auto result = obj->do_something();
    CHECK(result.is_ok());
}
```

---

## 三、模块依赖关系

```
Layer 0  okn-core ───── okn-math ───── okn-memory
              \              |              /
Layer 1       okn-platform
                    |              |
Layer 2       okn-ecs ────── okn-asset
              /    |    \     /    |    \
Layer 3   render network physics audio script
              \    \    /    /   /
Layer 4    okn-ui ── okn-editor ── tools/
```

**当前仅 P0 层的 API 为可靠状态**，其余模块所有文件仍为占位符。

---

## 四、Agent 开发工作流

```
① 读取本宪法 (AGENTS.md)
② 读取 Task Card (YAML)
③ 读取 depends_on 列出的文件（已实现的 API）
④ 如涉及 API 接口 → 先写纯虚头文件（契约先行）
⑤ 实现 .cpp
⑥ 编写 tests/
⑦ 运行验证（格式 → 编译 → 测试）
⑧ 子模块 git commit
⑨ 根仓库更新 submodule pointer
⑩ 交由验证 Agent 做最终验证
```

### 4.1 Task Card 格式

详见 `docs/task_template.yaml`。每个 Task Card 包含：
- `task_id`：唯一标识
- `module`：目标模块
- `layer`：所属阶段
- `depends_on`：上游已实现文件列表
- `api_contract`：接口契约（函数签名 + 行为说明）
- `files`：要写/修改的文件列表
- `checklist`：验证清单

### 4.2 验证标准（每个 Task 必须通过）
1. `scripts/lint.ps1` 通过（clang-format + clang-tidy）
2. `scripts/build.ps1 -Module <module> -Config Debug` 通过
3. `scripts/run_tests.ps1 -Module <module>` 全部通过
4. 编译警告零容忍（MSVC `/W4 /WX`，Clang-cl `-Wall -Wextra -Werror`）

### 4.3 验证失败时
- 验证 Agent 会将错误信息打回给原开发 Agent
- 原 Agent 必须修正，不得另起新 Agent
- 修正后重新走完整的验证循环

---

## 五、子模块提交规范

- Agent 在子模块内独立 commit（如 `modules/okn-math`）
- Commit 消息格式：`feat(<module>): <description>`
- 完成后在根仓库 `git add modules/<module>` 更新 submodule pointer
- 根仓库 commit：`chore: update <module> submodule`

```
示例：
# 在 modules/okn-math 内：
git add include/okn/math/algebra/vec3.hpp src/algebra/vec3.cpp tests/test_vec3.cpp
git commit -m "feat(math): implement vec3 with arithmetic, dot, cross, normalize"

# 回到根仓库：
git add modules/okn-math
git commit -m "chore: update okn-math submodule (vec3 impl)"
```

---

## 六、构建环境

| 项目 | 值 |
|------|-----|
| IDE | Visual Studio 18 2026 Community |
| MSVC | 19.12.25835 |
| CMake | 4.3.2+ |
| vcpkg | `D:\vcpkg`，清单模式 |
| Ninja | 推荐，备用 MSBuild |

初始构建命令：
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

---

## 七、详细规范索引

| 主题 | 文件 |
|------|------|
| 接口/实现分离代码模板 | `docs/patterns/service_registry.md` |
| 标准 Task Card 格式 | `docs/task_template.yaml` |
| 全模块文件级状态追踪 | `docs/TASK_STATUS.yaml` |
| 已完成模块的 API 清单 | `docs/api/<module>.api.md` |
| 完整开发大纲 | `docs/OKN_DEVELOPMENT_GUIDE.md` |

---

## 八、行为约束

- 收到 Task 后先查 `docs/TASK_STATUS.yaml` 确认依赖项 `completed`
- 依赖项是 `pending` → 不得假定其 API，须等待或先实现依赖
- 写 API 接口时 → 注释必须包含线程安全、生命周期、错误模型说明
- 禁止任何 Agent 修改本文件（AGENTS.md），只能由验证 Agent 在验证通过后修改
