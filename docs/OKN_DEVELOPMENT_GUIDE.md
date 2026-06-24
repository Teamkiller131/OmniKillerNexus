# OmniKillerNexus — Developer Guide (开发指南)

The entry point for working on the engine: where things live, the conventions,
and how to add a module or a game.

> This document used to be a ~470-line aspirational build-out plan (hand-written
> D3D12, Qt/QML editor, Lua+QuickJS+CPython, "only `service_registry` has real
> code", every phase "not started"). **That plan is obsolete** — it was largely
> executed and then *pivoted* (sokol over D3D12, Dear ImGui over Qt, sol2-only
> over the three-runtime plan; see [DECISIONS.md](DECISIONS.md)). It has been
> replaced by the focused doc set below. **For the real current state, read
> [ARCHITECTURE.md](ARCHITECTURE.md).**

---

## 1. The honesty principle

The repo was mass-scaffolded: many modules present a complete-looking file tree
where a large fraction of `src/*.cpp` are comment-only stubs. **Trust the build
and the test gate, not status labels.** `docs/TASK_STATUS.yaml`'s `completed`
markers mean "a file exists", not "it works". The audited truth is
[ARCHITECTURE.md](ARCHITECTURE.md); the gate is `scripts/run_tests_all.ps1` (13
suites, all green). Every change keeps the gate green and keeps docs matching
reality.

## 2. Where to find what

| You want… | Read |
|---|---|
| What each module really is, the render routes, deps | **[ARCHITECTURE.md](ARCHITECTURE.md)** |
| Build, run the test gate, run a game, screenshots | **[BUILD.md](BUILD.md)** |
| The demo games + the reusable game patterns | **[GAMES.md](GAMES.md)** |
| The forward plan (v3, phases P10+) + buy-vs-build ledger | **[ROADMAP.md](ROADMAP.md)** |
| Cross-cutting decisions (standard, naming, pivots) | **[DECISIONS.md](DECISIONS.md)** |
| The interface/implementation pattern | [patterns/service_registry.md](patterns/service_registry.md) |
| Contributor constitution (conventions + workflow) | `AGENTS.md` (root) |

## 3. Conventions (summary — authority is `AGENTS.md` / [DECISIONS.md](DECISIONS.md))

- **Standard:** C++26, no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`). On
  MSVC this is `/std:c++latest`; the literal `CMAKE_CXX_STANDARD 23` is only the
  CMake→MSVC mapping, **not** a downgrade ([ADR-0001](DECISIONS.md)).
- **Naming:** PascalCase types (`AssetRegistry`, `enum class LogLevel`),
  `I`-prefix interfaces (`ILogger`); snake_case functions / methods / variables /
  members (private members end with `_`); snake_case file names; `okn::<module>`
  namespaces; `#pragma once`.
- **Platform guards:** `#ifdef _WIN32 / #elif __linux__ / #elif __APPLE__`.
- **Format:** clang-format, Google style (`.clang-format` at root).
- **Tests:** doctest. Every real `.cpp` gets a `test_<name>.cpp` covering normal +
  boundary + error paths. No feature lands without a test in the gate.

## 4. Module layout

Two layouts exist in the tree; both are acceptable, but **new code in
`okn-core`/`okn-physics` follows the canonical interface/implementation split**:

```
include/okn/<module>/api/      interface headers (pure-virtual / CRTP, no 3rd-party, no impl)
include/okn/<module>/impl/      optional: default-impl factory declarations
src/defaults/                   default implementations
src/                            other implementations
```

```cpp
target_link_libraries(t PUBLIC  okn-core-interfaces)  // interface only
target_link_libraries(t PRIVATE okn-core)             // + default impl
registry.register_service<ILogger>(&my_logger);       // swap an impl
```

The feature modules (`ecs/render/ui/platform/script/...`) currently use a flatter
`<module>_export.hpp` layout instead — see each in [ARCHITECTURE.md](ARCHITECTURE.md).
Don't mutate global CMake state from a submodule (the root governs the standard).

## 5. Adding a module

1. Add the submodule under `modules/okn-<name>/` with its own
   `project(okn-<name> LANGUAGES CXX)` and a `STATIC` target.
2. `add_subdirectory(modules/okn-<name>)` in the root `CMakeLists.txt`.
3. Add it to the relevant SDK target(s) (`okn-client-sdk` etc.).
4. Use unique target names per module prefix (`okn-<name>`, `okn-<name>_tests`)
   to avoid duplicate-target errors.
5. Pull third-party via the root `vcpkg.json` manifest + `find_package`.
6. Land a `okn-<name>_tests` suite and wire it into `scripts/run_tests_all.ps1`.

## 6. Adding a game

Copy an existing `games/*` (see the patterns in [GAMES.md](GAMES.md)):
- **Sokol/sprite game:** link `okn-math` + `okn-audio` (+ `okn-physics` if it
  needs Jolt); reuse `okn-render`'s `gpu/gpu_sprite_renderer.cpp` +
  `gpu/sokol_impl_app.cpp` by path; compile stb yourself for disk sprites; embed
  a PerMonitorV2 `.manifest`; `add_subdirectory(games/<name>)` in the root.
- **UniGUI app:** link `unigui::unigui`, build only inside the root build.
- **Make it CI-checkable:** factor every player verb as a standalone callable
  function so an env-gated `--autodemo` can drive the real logic with no
  synthetic keys and write a `<game>_result.txt` marker. Assert on the marker.

## 7. Build & test

See [BUILD.md](BUILD.md). In short: source `vcvars64`, configure into a fresh
`build-phys/` with the vcpkg toolchain, `cmake --build build-phys`, then
`pwsh scripts/run_tests_all.ps1`.
