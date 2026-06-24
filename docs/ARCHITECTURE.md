# OmniKillerNexus — Architecture & Module Reference

> **This document is the authoritative description of what actually exists.**
> It is grounded in the test gate and a code audit, **not** in status labels.
> The per-module `README.md` files (inside the module submodules) and
> `docs/TASK_STATUS.yaml` describe the *aspirational scaffolding* and routinely
> over-claim — a large fraction of the "complete-looking" file trees are
> comment-only stubs. When a module README and this file disagree, **this file
> wins**. The forward plan is in [ROADMAP.md](ROADMAP.md); the cross-cutting
> decisions (and the pivots away from the original plan) are in
> [DECISIONS.md](DECISIONS.md).

---

## 1. What OmniKillerNexus is

A modular, C++26 game-engine framework built around **one validated principle:
buy undifferentiated infrastructure, build only game glue** (see
[ADR-0003](DECISIONS.md)). It is organized as a root aggregator repo plus 13
`okn-*` module submodules, a `third_party/TeamkillerUniGUI` submodule (the
Dear-ImGui editor toolkit), and a `games/` tree of complete demo games.

**Honest status:** the **2D vertical slice is closed end-to-end** and several
small games ship on it. It is *demo-grade* — real where the games exercise it,
stub or dead where they don't. It can build small 2D (and now some 3D) games
today; it cannot yet *ship* a complete, polished one without the work in the
roadmap.

The closed line:

```
input → ECS → Jolt physics (+ contact events) → 2D sprite render (software + sokol/D3D11 GPU)
      → audio (miniaudio + WAV + bus mixer) → Lua scripting (sol2, hot-reload)
      → ImGui/UniGUI editor → native binary save/load
```

---

## 2. How to read the module table

Each module is rated by **what the test gate and a live build actually prove**:

| Rating | Meaning |
|---|---|
| **Verified** | Real, exercised by tests *and* a running app. |
| **Substantial** | Core is real and tested; some advertised features missing. |
| **Partial** | A real spine exists; a meaningful fraction is stub/fake. |
| **Stub/Dead** | Present in the tree but non-functional or unreferenced. |

The build-phys gate has **15 module test suites, all green** (~800 cases /
~6,100 assertions) and additionally **compiles all 7 games** and asserts
**VOIDBORNE's headless `--selftest`/`--autodemo` markers** (the 6 sokol games need
a GPU/window, so they are compile-gated only). Suites build to
`build-phys/bin/*_tests.exe` and are **run directly** (they are not registered with
ctest — `ctest -N` reports 0). The suites don't map 1:1 to modules: `okn-render`
contributes **four** (`render2d`, `render2d_gpu`, `slice`, `lua_slice`), while
`okn-ui` and `okn-network` have **no** suite in this gate (their tests, where
they exist, live in-submodule). See [BUILD.md](BUILD.md).

---

## 3. Layered dependency map (as built)

```
Layer 0  okn-core      okn-math      okn-memory        (foundation, no deps)
              \           |            /
Layer 1                okn-platform                     (OS abstraction)
              \           |            /
Layer 2          okn-ecs        okn-asset               (core services)
            /    /    |     \         |
Layer 3  render network physics audio script            (feature modules)
              \         \      |     /
Layer 4            okn-ui     okn-editor     tools/      (integration)
```

Real edges that matter in practice:
- The **2D/3D render paths and the slice live in `okn-render` but depend only on
  `okn-math`** (and, for the slice, `okn-ecs` + `okn-physics` + `okn-audio`).
  They deliberately bypass the unlinkable `okn-render` static lib.
- The **UI → screen** edge is `okn-render`'s `hud_bridge` consuming
  `okn-ui` `DrawCommand`s — okn-ui never draws to a device itself.
- `okn-editor` only builds inside the **root** build (it needs
  `unigui::unigui`); standalone it is a stub lib.

---

## 4. Aggregate SDK targets (root `CMakeLists.txt`)

INTERFACE libraries that fan out to the modules:

| Target | Modules |
|---|---|
| `okn-client-sdk` | core, math, memory, render, audio, ui, ecs, script, network, asset, platform, physics |
| `okn-server-sdk` | core, math, memory, network, ecs, script, asset, platform |
| `okn-editor-sdk` | client set + editor |

Executables: `okn-editor-app` (the editor), `okn-cli` / split CLI tools
(`tools/`), the sample apps (`okn-sprite2d_app`, `okn-slice_app`), and every
`games/*` app.

---

## 5. The modules

### okn-core — **Substantial**
The service backbone. **Real:** service registry (compile-time FNV-1a hash +
type-safe wrapper), logger, config, time, timer, memory utils, hash, id/guid,
result, profiler. Follows the canonical `api/` + `impl/` + `src/defaults/`
layout (the reference for the "interface/implementation separation" pattern —
see [patterns/service_registry.md](patterns/service_registry.md)).
**Tests:** `okn-core-tests` (note: **hyphen**, every other suite uses
`_tests`). ~31 cases.

### okn-math — **Substantial**
Pure math, no third-party. **Real & tested:** `Vec2/3/4`, `Mat2/3/4`, `Quat`,
`Transform` (TRS compose/decompose), geometry (`Aabb/Sphere/Plane/Ray/Triangle/
Frustum/Obb` + intersections), `Color` (incl. `from_u8` + named constants),
interpolation/easing, Perlin noise, SSE SIMD. **Matrices are column-major**
(`data[col*N+row]`, GPU convention). `Mat4::perspective` is **GL depth [-1,1]**
— the 3D renderer builds its own D3D/sokol [0,1]-depth projection (see §6.2).
**Tests:** `okn-math_tests`, 127 cases.

### okn-memory — **Substantial**
**Real:** pools, arenas, tracking, intrusive containers, handle tables.
**Gaps:** the mimalloc backend is not wired (commented out); `override_new` and
`guard_page` are stubs. **Tests:** `okn-memory_tests`, ~99 cases.

### okn-platform — **Substantial** *(submodule — carries pre-existing local edits; do not modify)*
**Real:** Win32/Linux/macOS threads, sync, virtual memory, mmap, encoding,
input, crash handling, dynamic-library loading, system info. **Gap:** the
"work-stealing" deque is fake (`steal()` == `pop()`). **Tests:**
`okn-platform_tests`, ~109 cases.

### okn-ecs — **Partial**
**Real public API:** the **sparse-set `World`** (`world.cpp`) — entities,
components, queries — plus **save/load serialization** (`Serializer`/
`Deserializer` snapshot live entities + trivially-copyable component bytes,
keyed by a typeid-hash store id; host-endian, same-build save games).
**Scheduling:** a real parallel `Scheduler` runs systems over the live `World`
(conflict-based level grouping → `ThreadPoolJobSystem` worker threads).
**ScriptingBridge:** a runtime component-reflection surface — name→`ComponentTypeId`
resolution against the live store + a registration callback (no longer a stub).
**Removed (2026-06-23):** the dead archetype/chunk `Storage` second ECS core was
**deleted** — the live sparse-set `World` is the one ECS core ([ADR-0004](DECISIONS.md)).
**Tests:** `okn-ecs_tests`, 55 cases / 2325 assertions (incl. serialization
round-trip + the parallel-scheduler-over-real-pool test).
**Consumed by a game:** **VOIDBORNE** models its 20 crew as ECS entities with a
`CrewStats` component and runs the captain election as a `query<CrewStats>()` over
the live `World`.

### okn-asset — **Partial**
**Real spine:** assimp mesh import, stb texture import (both **guarded** —
compile no-op when the optional ports are absent), **WAV + TrueType importers**,
mesh pipeline (vertex-cache opt / normals / tangents / LOD), pack-file read/write,
LRU cache, registry, dependency graph, **real mtime hot-reload** (`AssetIO::
file_mtime` + `HotReload`), and **streaming queues** (mip/chunk/upload with
dedup + callbacks). **Missing:** basisu compression. **Tests:** `okn-asset_tests`,
113 cases / 394 assertions. **Consumed by a game:** **VOIDBORNE** loads all data
through `AssetIO` and live-hot-reloads `events.json`.

### okn-render — **Partial** *(the GPU-backend lib is Stub/Dead; the 2D/3D/slice paths are Real)*
The most nuanced module. The native **D3D12/Vulkan static lib is dead** —
duplicate-symbol stubs (`queue.cpp` vs `command_queue.cpp`) make it
**unlinkable**; nothing ships against it. **The live, tested rendering is three
header-first paths that bypass that lib and link only `okn-math`** (plus ECS/
physics/audio for the slice). These are the engine's real graphics — see §6.

### okn-network — **Partial**
**Real:** the transport-agnostic reliability core — `ReliabilityLayer`
(seq/ACK/retransmit), `Mux`, `RingBuffer`, `FlowControl` (token bucket), `QoS`
priority (~80% of the over-asio design; tested in-submodule, not in the
build-phys gate). **Fake:** the QUIC/TCP/UDP transports (`connect()` → `true`
no-ops; `UdpSocket` has a null-`impl_` construction bug; msquic never wired).
Deferred until single-player ships ([ROADMAP P17](ROADMAP.md)).

### okn-physics — **Verified** *(the one fully-real Layer-3 module)*
**Jolt-backed**, the proven foundation that gameplay is built on. **Real:**
rigid bodies (box/sphere/capsule; cylinder/cone/heightfield silently fall back
to a 0.5 sphere — warned at creation), raycast, **contact events**
(`drain_contacts()` returns body pairs that began touching this step — the
gameplay-trigger primitive), **joints** (ball/distance/hinge), **kinematic
bodies** (moving platforms). **Gaps:** no built-in character controller (games
roll their own from raycasts), single-threaded, no trigger/sensor events
(contact-*added* only), `collision_mask` ignored. **Tests:** `okn-physics_tests`,
16 behavioral cases. **Determinism:** Jolt is deterministic run-to-run on a
fixed platform/build (enough for input-replay netcode) and offers
`JPH_CROSS_PLATFORM_DETERMINISTIC` to make ARM/x86 match (at a perf cost — it
disables FMA). OKN does **not** enable that flag, so OKN's build isn't
cross-platform deterministic as configured — which gates *lockstep* netcode
unless the flag is turned on (see [ROADMAP §10](ROADMAP.md)).

### okn-audio — **Partial**
**Real:** miniaudio engine + playback, DSP (RBJ biquad EQ, Freeverb,
compressor), stereo pan, a real **WAV (RIFF/PCM 8/16-bit) decoder**, and a real
**bus mixer** (sums routed sounds scaled by bus×master, clamps). **Fake:**
mp3/flac/ogg decoders return empty; platform backends are bool-flip stubs.
**Tests:** `okn-audio_tests`, ~106 cases.

### okn-script — **Partial**
**sol2 over Lua 5.4 is the only runtime** (JS/QuickJS + CPython were deleted —
they were `return true` fakes; [ADR-0007](DECISIONS.md)). **Real:** `LuaContext`
is sol2-backed so `load_string`/`load_file` actually *run* chunks; `EngineBindings`
are real & callable (`vec3()`/`vec3_*`, `okn.log`/`okn.version`, `okn_input.*`,
`entity(id)`) — implemented as Lua tables/functions (not `new_usertype`, which
crashes at `lua_close` with this sol2/Lua build). **Thin:** sandbox, debugger,
binding-gen. **Tests:** `okn-script_tests`, ~11 cases.

### okn-ui — **Partial** *(submodule — carries pre-existing local edits; do not modify)*
**Real & tested:** widget tree, 11 widgets emitting `DrawCommand`s, 3 layout
engines (incl. flexbox), animation/tweens, mouse input routing + hit-test, theme.
**Mouse-only** — no keyboard/text input yet. **The render path is not in okn-ui**:
okn-ui emits `DrawCommand`s; `okn-render`'s `hud_bridge` turns them into sprite
quads (correct dependency direction). Its test suite is not built in the
build-phys gate; standalone the lib links only core + math.

### okn-editor — **Substantial** *(builds only inside the root build)*
Rebuilt on **TeamkillerUniGUI** (Dear ImGui, DX11); **Qt was dropped**
([ADR-0003](DECISIONS.md)). `okn-editor-app` is a real content tool: DockSpace +
Hierarchy / Inspector / Viewport / Assets / Console, with **live bridges to the
real modules** — the "scene" is a `SliceWorld`/`LuaSlice` (ECS + Jolt + the 2D
render path + audio + Lua). Drag-gizmos, inspector edits (write component **and**
Jolt body), color edit, hot-reload of `slice_scene.lua`, save back to Lua, assets
panel, Ctrl+Z undo. `--selftest` verifies a save/serialize round-trip headlessly.
**Workaround:** the viewport rasterizes the SpriteBatch with `ImDrawList` rather
than the engine's `GpuSpriteRenderer` (UniGUI doesn't expose its D3D11 device —
[ROADMAP P16](ROADMAP.md)). Needs `unigui::unigui`; otherwise a stub lib.

### tools/ — CLI
A mono `okn-cli` or split sub-tools (asset/shader/script/net/audio/ui), gated by
`OKN_BUILD_CLI`. Largely scaffolding; the per-tool `main.cpp`s are entry stubs.

---

## 6. The render routes (the engine's real graphics)

All three live under `modules/okn-render/` but are **header-first and bypass the
dead `okn-render` lib**. This is where almost all real rendering happens.

### 6.1 2D sprite path — `include/okn/render/sprite2d/` ([ADR-0005](DECISIONS.md))
Backend-agnostic CPU layer + swappable backend, deps **okn-math only**:

```
Camera2D (orthographic) → SpriteBatch (sprites → world quads, grouped by texture) → backend
```

- **`SoftwareRenderer`** — barycentric raster + nearest texture + alpha blend;
  the headless reference, asserted pixel-by-pixel (`okn-render2d_tests`, 9 cases).
- **`GpuSpriteRenderer`** — sokol_gfx; `begin_frame()`/`add(cam, groups)`/
  `end_frame()`, multi-camera (world + screen HUD), one draw per texture group,
  one buffer update/frame. All sokol confined to `gpu/*.cpp`. Verified on the
  sokol **dummy** backend (`okn-render2d_gpu_tests`) and live on **D3D11**
  (`okn-sprite2d_app` via sokol_app). `upload_texture(id, Image)` for runtime
  textures; texture 0 = built-in white.

This is the path **every 2D game uses** (flappy, knockdown, platformer, mario,
harvest, voidborne).

### 6.2 3D mesh path — `include/okn/render/mesh3d/`
The engine's first working 3D renderer (sokol_gfx, depth-tested, lit):

```
Camera3D (perspective, D3D [0,1]-depth — built here, NOT Mat4::perspective) → MeshRenderer
```

`MeshRenderer` draws colored unit-cube boxes with per-face normals + Lambert
lighting; `draw_box` has yaw-only and full-`Quat` orientation overloads (the VS
recomputes the world normal from the model matrix so rotated boxes light
correctly). Voxel models are built from many lit boxes. Used by `games/mario3d`.

### 6.3 The vertical slice — `include/okn/render/slice/` + `slice/` ([ADR-0006](DECISIONS.md), [ADR-0007](DECISIONS.md))
The glue that wires gameplay together:

- **`SliceWorld`** — ties `okn-ecs` (Transform2D/SpriteComp/BodyComp/PlayerTag)
  to `okn-physics` (Jolt). `update()` steps physics + syncs body transforms into
  the ECS; `build_sprites()` → SpriteBatch; `move_player`/`jump` + landing
  detection → `on_land`.
- **`LuaSlice`** (sol2) — a Lua script authors the scene + `on_update(dt)`/
  `on_land()`; `check_hot_reload()` re-runs on file change. `slice_scene.lua` is
  the live content.
- **`hud_bridge.hpp`** — the okn-ui → screen link (DrawCommand → sprite quads).
- **`scene_io.hpp`** — native binary scene save/load (serializes spawn
  *descriptors* so load rebuilds entities **and** Jolt bodies into a fresh world).

Verified by `okn-slice_tests` / `okn-lua_slice_tests` and the live `okn-slice_app`.

---

## 7. Third-party (bought) infrastructure

Via `vcpkg.json` (manifest mode) + the `third_party/` submodule:

| Library | Used for |
|---|---|
| **Jolt** | physics (the proven pivot) |
| **sokol** (gfx/app) | GPU render backend + windowing/input (DPI-aware) |
| **miniaudio** | audio device + playback |
| **sol2 / Lua** | scripting |
| **stb / assimp** | texture / mesh import (guarded) |
| **Dear ImGui / TeamkillerUniGUI** | editor UI |
| **doctest** | unit tests |
| **asio** | network reliability core transport (deferred) |
| **mimalloc, basisu, msquic, quickjs-ng** | declared but **not wired** (msquic/quickjs slated for removal — [ADR-0003](DECISIONS.md)) |

We write the **glue and the games**, not another physics solver, audio engine,
or D3D12 backend.

---

## 8. Known dead / fake code (current — most of the old list was fixed in Phases A–D)

**Partial / placeholder:**
- `okn-render` native D3D12/Vulkan/Metal/GL lib — **the D3D12 backend has a
  verified working draw pipeline**: `render_clear_readback()` clears + reads back
  exact pixels, and `render_triangle_readback()` runs the full programmable path
  (runtime-compiled HLSL → root signature + PSO → vertex buffer → `DrawInstanced`)
  and **rasterizes a triangle**, both verified pixel-exact (gate suite
  `okn-render-native`, WARP fallback → runs headless). Still **not a full backend**
  (no materials/textures/depth; the render graph isn't wired to GPU recording;
  Vulkan/Metal are bring-up skeletons; ~82 src files are empty placeholders) and
  **not** the default path — the engine ships on sokol (the 2D sprite / 3D mesh /
  slice routes). A deliberate deferred fork — see [COMPLETION_PLAN §C1](COMPLETION_PLAN.md).
- `okn-audio` platform backends (`backend_wasapi/xaudio2/coreaudio/alsa/...`) —
  near-empty; miniaudio already provides these internally (slated for pruning).
- `okn-editor` — ~1–2 real files behind ~86 placeholders (Dear ImGui editor).
- `tools/` — entry stubs.

**Fixed since the prior audit (no longer dead):**
- `okn-ecs` archetype/chunk `Storage` — **deleted** (2026-06-23); the live
  sparse-set `World` is the one ECS core.
- `okn-network` QUIC/TCP/UDP — real `UdpSocket`/`TcpAcceptor` + reliability over
  live sockets; QUIC is an honest "unavailable" stub (msquic not wired).
- `okn-audio` mp3/flac/ogg decoders — real (miniaudio + stb_vorbis, verified on
  fixtures).
- `okn-asset` — real mtime hot-reload + streaming queues + WAV/font importers.

---

## 9. See also

- **[README.md](../README.md)** — project front door + quick start.
- **[BUILD.md](BUILD.md)** — build, test gate, run a game, screenshots.
- **[GAMES.md](GAMES.md)** — the 7 demo games and the reusable game pattern.
- **[ROADMAP.md](ROADMAP.md)** — the authoritative forward plan (v3, phases P10+).
- **[DECISIONS.md](DECISIONS.md)** — ADR log (standard, naming, buy-vs-build, the pivots).
- **[gitea_runner_setup.md](gitea_runner_setup.md)** — CI runner setup.
- `AGENTS.md` — contributor constitution (conventions/workflow). *Note: its
  `TASK_STATUS.yaml`-trust instruction predates this audit; trust the test gate
  and this file for true module state.*
