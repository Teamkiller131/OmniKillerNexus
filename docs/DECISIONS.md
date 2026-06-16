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

## ADR-0004 — Vertical slice over horizontal completeness

**Status:** accepted (2026-06-16)

13 modules at ~30% breadth are worth less than 5 at 100% along one playable line.
No off-critical-path work until the first playable 2D slice runs (input → ECS →
physics → sprite → sound → UI). Deferred off-path: networking, scripting, editor,
3D, quaternion `slerp`, the archetype ECS. The true per-module state is recorded
out-of-band (audited 2026-06-16; the `TASK_STATUS.yaml` "completed" labels are
aspirational scaffolding markers — trust the build, not the labels).
