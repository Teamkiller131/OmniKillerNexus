# OmniKillerNexus

A modular **C++26 game-engine framework** organized as a root aggregator repo
plus 13 `okn-*` module submodules, the `third_party/TeamkillerUniGUI` editor
toolkit, and a `games/` tree of **complete, playable demo games** that double as
the engine's acceptance tests.

Its one validated design principle: **buy undifferentiated infrastructure, build
only game glue.** Jolt (physics), sokol (GPU + windowing), miniaudio (audio),
sol2/Lua (scripting), stb/assimp (assets) and Dear ImGui (editor) are the
load-bearing third-party; the engine is the glue and the games.

> **Status — honest.** The **2D vertical slice is closed end-to-end** and several
> small games ship on it. It's *demo-grade*: real where the games exercise it,
> stub or dead where they don't. It builds small 2D (and some 3D) games **today**;
> it can't yet *ship* a polished, complete one — see the [roadmap](docs/ROADMAP.md).
> **Trust the test gate, not status labels** (`docs/TASK_STATUS.yaml`'s `completed`
> markers are aspirational scaffolding — the audited truth is
> [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)).

The closed line:

```
input → ECS → Jolt physics (+ contact events) → 2D sprite render (software + sokol/D3D11 GPU)
      → audio (miniaudio + WAV + bus mixer) → Lua scripting (sol2, hot-reload)
      → ImGui/UniGUI editor → native binary save/load
```

---

## Games (the engine's real acceptance tests)

7 complete games across 5 genres prove the render/input/audio stack is
genre-agnostic and physics is opt-in. Full details + how to run: **[docs/GAMES.md](docs/GAMES.md)**.

| Game | Genre | What it proves | Physics |
|---|---|---|---|
| **flappy** | arcade | the 2D GPU sprite path builds a real game | none |
| **knockdown** | physics puzzle | Jolt **contact events** as gameplay triggers | Jolt |
| **platformer** | 2D platformer | character controller + input map + **disk PNG** assets + save | Jolt |
| **mario** | side-scroller | **multi-texture** batching + stomp/hurt via contacts | Jolt |
| **mario3d** | 3D platformer | the **3D mesh path** + joints + kinematic platforms | Jolt (3D) |
| **harvest** | grid farming / RPG-sim | a whole game with **no physics** (free-move + AABB) + deep sim systems | none |
| **voidborne** | management sim | a **Dear ImGui / UniGUI** draw-list world + panels, JSON data | none |

---

## Quick start

```bat
git clone <repo-url> OmniKillerNexus && cd OmniKillerNexus
git submodule update --init --recursive

:: load MSVC (adjust to your VS install), then configure into build-phys
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build-phys -G Ninja -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build-phys -j                  :: build everything
pwsh scripts/run_tests_all.ps1               :: run the 13-suite test gate (all green)

cmake --build build-phys --target flappy     :: build + play a game
build-phys\bin\flappy.exe
```

Full build / test / run / screenshot instructions: **[docs/BUILD.md](docs/BUILD.md)**.
(Use a fresh `build-phys/` dir — a stale `build/` here pins a deleted MSVC toolset.)

---

## Repository layout

```
OmniKillerNexus/
├── modules/            13 okn-* engine module submodules (see below)
├── games/              flappy · knockdown · platformer · mario · mario3d · harvest · voidborne
├── third_party/
│   └── TeamkillerUniGUI   Dear ImGui app/widget toolkit (powers okn-editor + voidborne)
├── tools/              CLI tools (asset/shader/script/…)
├── docs/               the documentation set (below)
├── scripts/            run_tests_all.ps1 (the CI gate)
├── CMakeLists.txt      root aggregator: SDK targets + add_subdirectory(modules/games)
└── vcpkg.json          third-party manifest
```

---

## The modules

13 submodules in four layers. **Authoritative per-module state, the render
routes, and the dependency map: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).**

| Layer | Modules | Headline state |
|---|---|---|
| **0 — foundation** | `okn-core` · `okn-math` · `okn-memory` | substantial, fully tested |
| **1 — platform** | `okn-platform` | substantial (OS threads/fs/input/crash) |
| **2 — services** | `okn-ecs` · `okn-asset` | partial (sparse-set World + serialization; import spine) |
| **3 — features** | `okn-render` · `okn-physics` · `okn-audio` · `okn-script` · `okn-network` | physics **verified** (Jolt); render's 2D/3D/slice paths real; audio/script partial; network deferred |
| **4 — integration** | `okn-ui` · `okn-editor` · `tools/` | UI widgets real (mouse-only); editor on Dear ImGui |

The real rendering lives in **three header-first paths inside `okn-render` that
bypass its (dead) GPU lib**: the **2D sprite path**, the **3D mesh path**, and
the **vertical slice** (ECS+physics+audio+Lua glue). See ARCHITECTURE.md §6.

---

## Documentation

| Doc | What it covers |
|---|---|
| **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** | Authoritative: every module's real state, the render routes, dependency map, SDK targets, dead code. **Start here for "how it works."** |
| **[docs/BUILD.md](docs/BUILD.md)** | Build, the 13-suite test gate, running games, the screenshot harness, CI. |
| **[docs/GAMES.md](docs/GAMES.md)** | The 7 demo games + the reusable game patterns. |
| **[docs/ROADMAP.md](docs/ROADMAP.md)** | The authoritative forward plan (P5–P9) + the buy-vs-build ledger. |
| **[docs/DECISIONS.md](docs/DECISIONS.md)** | ADR log — language standard, naming, the Jolt/sokol/sol2/ImGui pivots. |
| **[docs/patterns/service_registry.md](docs/patterns/service_registry.md)** | The interface/implementation-separation pattern (okn-core). |
| **[docs/gitea_runner_setup.md](docs/gitea_runner_setup.md)** | CI runner setup. |
| `AGENTS.md` | Contributor constitution (conventions + workflow). |

---

## Conventions (summary)

C++26, no compiler extensions. **PascalCase** types (`AssetRegistry`), `I`-prefix
interfaces (`ILogger`), **snake_case** functions/members (trailing `_` on private
members), snake_case file names, `okn::<module>` namespaces, `#pragma once`. Full
rules in `AGENTS.md`; the authoritative decisions in
[docs/DECISIONS.md](docs/DECISIONS.md).
