# Engineering Decisions (ADR log)

Short, authoritative records of cross-cutting decisions. When a governing
document (AGENTS.md, the dev guide, a module README) disagrees with an entry
here, this log reflects the *current* intent; reconcile the other doc to it.

---

## ADR-0001 — Language standard is C++26 (effective)

**Status:** accepted (2026-06-16)

The engine targets **C++26**. On MSVC there is no `/std:c++23` and CMake (4.x)
has no `CXX26` ↔ MSVC mapping, so the root `CMakeLists.txt` injects
`/std:c++latest` directly for MSVC (C++26 preview) and sets `CMAKE_CXX_STANDARD 26`
for other compilers. The literal `set(CMAKE_CXX_STANDARD 23)` seen in the MSVC
branch and in per-module CMake files is **only** the CMake→`/std:c++latest`
mapping value — it is *not* a C++23 target. Do not "fix" it to 23-as-final or
read it as a downgrade. Verified: all foundation modules build clean under
`/std:c++latest`.

Follow-up (not blocking): per-submodule `set(CMAKE_CXX_STANDARD 23)` should be
removed so the root setting governs (AGENTS.md §1.2 forbids submodules mutating
global CMake state). Deferred to avoid 13-submodule churn; effective standard is
already C++26.

## ADR-0002 — Naming: PascalCase types, snake_case functions

**Status:** accepted (authoritative = AGENTS.md §1.1)

Classes / structs / enums are **PascalCase** (`AssetRegistry`, `ProfileSample`,
`enum class LogLevel`); interfaces carry the `I` prefix (`ILogger`). Functions /
methods / variables / members are **snake_case** (private members end with `_`).
The codebase already follows this 100%. `docs/OKN_DEVELOPMENT_GUIDE.md §4.2`
previously stated snake_case classes — that was stale and has been corrected.

## ADR-0003 — Buy undifferentiated infrastructure; build only game glue

**Status:** accepted (2026-06-16); see the long-term roadmap

The Jolt physics pivot is the template: a solo dev does not out-engineer mature
infrastructure libraries. Standing decisions:

| Area | Decision |
|------|----------|
| Physics | **Jolt** (done — the only verified-working L3 module). |
| Render | Adopt **sokol_gfx or bgfx**; do NOT keep hand-writing D3D12/Vulkan/Metal/GL. |
| Audio | Keep **miniaudio**; add `dr_wav`; **delete the custom mixer** (route via `ma_engine`). |
| Script | Adopt **sol2** over the existing Lua; build ONE runtime. **JS/QuickJS + CPython deleted** (were `return true` stubs). |
| Network | Do NOT buy enet — finish the ~80%-done reliability core over **asio** (fix the null-`impl_` `UdpSocket` ctor). Defer the module until single-player ships. |
| Editor | Drop **Qt**; use **Dear ImGui** inside the engine's own renderer. |

If the JS/Python/QUIC paths stay deleted, also remove `quickjs-ng` and `msquic`
from `vcpkg.json` so they stop pulling heavy deps into every build.

## ADR-0005 — 2D sprite path: engine-facing layer + software reference, sokol GPU backend

**Status:** accepted (2026-06-16); Phase 1

The 2D render path is a backend-agnostic CPU layer plus a swappable backend:
`Camera2D` (orthographic) → `SpriteBatch` (sprites → world-space quads grouped by
texture) → backend. It depends ONLY on okn-math and lives header-only under
`okn-render/include/okn/render/sprite2d/`, deliberately NOT linked against the
okn-render static lib (which has pre-existing duplicate-symbol issues in its
D3D12 stubs — `queue.cpp` vs `command_queue.cpp`; cleaning that is its own task).

Two backends now exist, both consuming the same `DrawGroup`s:
- **Software rasterizer** (`SoftwareRenderer`, barycentric fill + nearest texture
  + alpha blend): the headless reference, asserted pixel-by-pixel
  (`okn-render2d_tests`, 9 cases).
- **GPU backend — sokol_gfx** (`GpuSpriteRenderer`, ADR-0003): shader/pipeline/
  dynamic buffers/image/sampler, one draw per texture group. Verified headlessly
  on the sokol **dummy backend** through its validation layer (`okn-render2d_gpu_tests`,
  2 cases), and shipped as a real **D3D11 windowed sample** (`okn-sprite2d_app`,
  via sokol_app) that compiles, links, and runs a live animated sprite on
  hardware. All sokol headers are confined to `okn-render/gpu/*.cpp`; the targets
  link okn-math only (not the okn-render lib). sokol is a vcpkg dep.

## ADR-0006 — The 2D vertical slice is wired end-to-end (Phase 2)

**Status:** accepted (2026-06-16); Phase 2 complete

The first playable line is real: input → ECS → physics → sprite → HUD → sound.
Lives in `okn-render/include/okn/render/slice/` (header-only) + `okn-render/slice/`.
- `SliceWorld` ties **okn-ecs** (entities + Transform2D/SpriteComp/BodyComp/PlayerTag
  components) to **okn-physics** (Jolt): each `update()` steps physics and syncs
  body transforms into the ECS, from which `build_sprites()` yields a SpriteBatch.
  Player control (`move_player`/`jump_player`) + velocity-based landing detection
  drive an `on_land` callback. (2D bodies use real Z-depth + CCD so the thin
  z-aligned slabs don't tunnel.)
- The **okn-ui → render link** is `hud_bridge.hpp`: it consumes `okn::ui::DrawCommand`
  (the okn-ui render contract) and turns rect/image commands into sprite quads, so
  the existing sprite/GPU path draws the HUD. No okn-ui lib code is needed to render
  what okn-ui produced; the broken okn-render lib is bypassed entirely.
- **Audio**: `okn::audio::WavDecoder` (now real) decodes a sound; the contact event
  triggers playback (`AudioPlayback` over miniaudio in the app).

Verified two ways: `okn-slice_tests` (5 cases — fall+land+sound, keyboard move,
sprite batching, HUD bridge geometry, software-rendered scene+HUD BMP), and the
interactive **`okn-slice_app`** (D3D11) which runs the full loop live on hardware.
Known workaround: `okn::math::Color::from_u8` and the static Color constants are
unimplemented stubs, so colors are built from the inline float ctor.

## ADR-0007 — Scripting is sol2/Lua; gameplay is hot-reloadable Lua (Phase 3)

**Status:** accepted (2026-06-16); Phase 3

sol2 (vcpkg `sol2`) over Lua 5.4 is the scripting layer (ADR-0003). Two pieces:
- **okn-script** now uses sol2 *internally*: `LuaContext` is backed by a `sol::state`
  (so `load_string`/`load_file` actually RUN the chunk — the raw version only loaded
  it — and `set_global` is real), and `EngineBindings` are real & callable from Lua
  (`vec3()`/`vec3_length`/`vec3_add`/`vec3_dot`, `okn.log`/`okn.version`,
  `okn_input.set_key`/`key_down`, `entity(id)`). Bindings use Lua tables + functions
  (not `new_usertype`, which crashed at `lua_close` with this sol2/Lua build).
- **okn-render/slice/LuaSlice**: a Lua script AUTHORS the scene + gameplay. The game
  API (`set_gravity`/`spawn_ground`/`spawn_player`/`spawn_box`/`move_player`/`jump`/
  `player_x/y`) is bound to a `sol::state`; the script defines `on_update(dt)` and
  `on_land()` (which calls a host-injected `play_sound`). `check_hot_reload()` re-runs
  the file when it changes on disk.

`slice_scene.lua` is the live, hot-reloadable content; `okn-slice_app` loads it and
reloads on edit. Verified: `okn-lua_slice_tests` (3 cases — Lua authors the scene,
the on_land trigger fires on a physics contact, hot-reload picks up file edits) and
`okn-script_tests` (engine bindings callable). **GOTCHA:** a `sol::state_view` over a
state you don't own holds Lua registry refs — destroy it BEFORE closing the state, or
its dtor `luaL_unref`s a freed state (SIGSEGV). The JS/Python runtimes were already
deleted (Phase 0); Lua is the only scripting runtime.

## ADR-0004 — Vertical slice over horizontal completeness

**Status:** accepted (2026-06-16)

13 modules at ~30% breadth are worth less than 5 at 100% along one playable line.
No off-critical-path work until the first playable 2D slice runs (input → ECS →
physics → sprite → sound → UI). Deferred off-path: networking, scripting, editor,
3D, quaternion `slerp`, the archetype ECS. The true per-module state is recorded
out-of-band (audited 2026-06-16; the `TASK_STATUS.yaml` "completed" labels are
aspirational scaffolding markers — trust the build, not the labels).
