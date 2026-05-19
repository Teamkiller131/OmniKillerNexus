# 接口 / 实现分离模式

OmniKillerNexus 的核心架构模式。所有模块必须遵循此模式。

---

## 模式结构

```
include/okn/<module>/api/<name>.hpp          ← 纯虚接口（合约）
src/defaults/<name>_default.hpp              ← 默认实现声明
src/defaults/<name>_default.cpp              ← 默认实现定义
include/okn/<module>/impl/<module>_factory.hpp ← 工厂函数
tests/test_<name>.cpp                           ← doctest 单测
```

---

## 完整示例：logger

### ① 接口头

```cpp
// include/okn/core/api/logger.hpp
#pragma once

#include <string_view>
#include <okn/core/api/types.hpp>
#include <okn/core/api/result.hpp>

namespace okn::core {

enum class log_level : uint8_t {
    trace = 0,
    debug = 1,
    info  = 2,
    warn  = 3,
    error = 4,
    fatal = 5,
};

class ILogger {
public:
    virtual ~ILogger() = default;

    /// @thread_safe 多线程可并发调用
    /// @lifetime    日志消息在调用返回后可能被复制，调用方无需保持引用
    virtual void log(log_level level, std::string_view message) = 0;

    /// @return true 如果给定级别的日志会被记录（可用于跳过昂贵的格式化）
    [[nodiscard]] virtual auto is_enabled(log_level level) const noexcept -> bool = 0;

    /// 设置最低记录级别
    virtual void set_level(log_level level) noexcept = 0;
};

} // namespace okn::core
```

### ② 默认实现声明

```cpp
// src/defaults/logger_default.hpp
#pragma once

#include <okn/core/api/logger.hpp>
#include <mutex>
#include <cstdio>

namespace okn::core {

class logger_default final : public i_logger {
public:
    explicit logger_default(log_level min_level = log_level::info);
    ~logger_default() override;

    void log(log_level level, std::string_view message) override;
    [[nodiscard]] auto is_enabled(log_level level) const noexcept -> bool override;
    void set_level(log_level level) noexcept override;

private:
    log_level min_level_;
    std::mutex mutex_;
    std::FILE* output_;
};

} // namespace okn::core
```

### ③ 默认实现定义

```cpp
// src/defaults/logger_default.cpp
#include "logger_default.hpp"

namespace okn::core {

logger_default::logger_default(log_level min_level)
    : min_level_(min_level), output_(stdout) {}

logger_default::~logger_default() = default;

void logger_default::log(log_level level, std::string_view message) {
    if (!is_enabled(level)) return;
    std::lock_guard lock(mutex_);
    fmt::print(output_, "[{}] {}\n", static_cast<int>(level), message);
}

auto logger_default::is_enabled(log_level level) const noexcept -> bool {
    return level >= min_level_;
}

void logger_default::set_level(log_level level) noexcept {
    min_level_ = level;
}

} // namespace okn::core
```

### ④ 工厂函数

```cpp
// include/okn/core/impl/core_factory.hpp
#pragma once

#include <memory>
#include <okn/core/api/logger.hpp>

namespace okn::core {

[[nodiscard]] auto make_default_logger(log_level min_level = log_level::info)
    -> std::unique_ptr<i_logger>;

} // namespace okn::core
```

### ⑤ 单测

```cpp
// tests/test_logger.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <okn/core/api/logger.hpp>
#include <src/defaults/logger_default.hpp>

TEST_CASE("okn::core::logger - default level filtering") {
    auto logger = okn::core::logger_default(okn::core::log_level::warn);
    CHECK(logger.is_enabled(okn::core::log_level::error));
    CHECK(logger.is_enabled(okn::core::log_level::warn));
    CHECK(!logger.is_enabled(okn::core::log_level::info));
    CHECK(!logger.is_enabled(okn::core::log_level::debug));
}

TEST_CASE("okn::core::logger - set_level") {
    auto logger = okn::core::logger_default(okn::core::log_level::warn);
    logger.set_level(okn::core::log_level::debug);
    CHECK(logger.is_enabled(okn::core::log_level::debug));
}
```

---

## 关键规则

| 规则 | 说明 |
|------|------|
| 接口类名 | `i_<name>` 前缀（如 `i_logger`、`i_memory_allocator`） |
| 默认实现类名 | `<name>_default`（如 `logger_default`） |
| 工厂函数名 | `make_default_<name>()` |
| 接口头 | 不包含三方库，不含实现代码，只声明 |
| 默认实现 | 可在 `.cpp` 中引用三方库（如 miniaudio、mimalloc） |
| 生命周期注释 | 每个接口方法必须注释线程安全性、参数生命周期要求 |
| `[[nodiscard]]` | 所有返回值有意义的方法加此属性 |
| `noexcept` | 不抛异常的方法标注 |

---

## 上层模块如何依赖下层接口

```cpp
// 仅需要接口：
target_link_libraries(<t> PUBLIC okn-core-interfaces)

// 需要默认实现（必须在 .cpp 中包含 impl header）：
target_link_libraries(<t> PRIVATE okn-core)
```

---

## 参考实现

- `modules/okn-core/include/okn/core/api/service_registry.hpp` — 唯一的已完成接口实现
- `modules/okn-core/src/defaults/service_registry_default.hpp`
- `modules/okn-core/src/defaults/service_registry_default.cpp`
