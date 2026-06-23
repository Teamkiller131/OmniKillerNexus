# OmniKillerNexus — Completion Plan (finish the scaffolding)

> **Owner directive: FINISH the scaffolded modules, do not delete them.** This
> supersedes the prune-first framing in [EVALUATION.md](EVALUATION.md) Part B.
> Grounded in a per-module code audit of the actual stub files (2026-06-23).
> Default = **finish everything**. Three of the largest finish-it targets
> *duplicate already-bought infrastructure*; they are flagged as explicit **forks**
> with their real cost so the owner can choose finish-now / finish-later per item.

## Working rules

1. **Finish, not delete** — turn each stub/fake/dead piece into real, tested code.
2. **Foundation leaves first** (no conflict, fully testable today), then the
   bigger/contentious modules.
3. **Gate discipline** — every change keeps the existing test suites green; each
   finished feature lands with **doctest cases**, and modules currently missing
   from the gate (`okn-network`, `okn-ui`) get wired into `run_tests_all.ps1` as
   they're finished.
4. **Prefer wiring a declared dep over hand-writing** where it doesn't conflict
   with the directive (miniaudio bundles dr_wav/dr_flac/dr_mp3 + stb_vorbis;
   mimalloc is already in `vcpkg.json` and CMake-ready; stb_truetype for fonts).
5. **The 3 forks** (native render, archetype ECS, QUIC) duplicate bought infra —
   each is specced **both ways**; the owner decides. Until then, finish the
   non-fork work, which is the bulk of the value and unblocks the north-star game.

## Phases

| Phase | Scope | Forks? | Effort |
|---|---|---|---|
| **A — Unblock & foundation** | render ODR fix (required either way), `UdpSocket` null-`impl_` bugfix, **okn-memory** (mimalloc + guard_page + override_new), okn-platform Chase-Lev deque, okn-ui keyboard/text input, okn-ecs ScriptingBridge, okn-asset audio+font importers; wire okn-network/okn-ui suites into the gate | none | M (~1–2 wk) |
| **B — Subsystem decoders & pipelines** | okn-audio mp3/flac/ogg via `ma_decoder`; okn-asset real mtime hot-reload + mip/chunk/upload streaming + basisu; okn-network **TCP/UDP over ASIO** (non-fork transport) | none | M (~2 wk) |
| **C — The three forks** | (C1) native render backends, (C2) archetype/chunk ECS + scheduler, (C3) QUIC via msquic — *only the ones the owner elects* | **all 3** | L–XL each |
| **D — North-star game** | one complete game on the finished stack (title → levels → win/lose → save → settings/rebind) — the forcing function ([ROADMAP P7](ROADMAP.md)) | none | M |

**Effort:** the non-fork program (A + B + D) is **~5–7 weeks** of focused solo
work. Finishing **all three forks** (C) adds **~7–9 weeks** (native render alone is
XL ≈ 3–4 wk; archetype ECS ≈ L 2–3 wk; QUIC ≈ L 1–2 wk) → a **~3–4 month** total,
and leaves the engine with *two render paths and two ECS cores*.

## The three forks (owner decides per item)

| Fork | Finish-it path (what "finish" costs) | Trade-off | Recommendation |
|---|---|---|---|
| **C1 — native render lib** (D3D12/Vulkan/Metal/GL + render graph, ~98 dead `.cpp`) | Make `IBackendFactory`, device/queue/swapchain, HLSL→PSO + descriptor heaps, and a real render-graph DAG real against D3D12 (then Vulkan). **XL, 3–4+ wk.** | Duplicates the **already-working sokol** 2D/3D/slice paths; hand-written D3D12 doesn't give cross-platform (still need GL/Metal); ships on no game; contradicts [ADR-0003/0005](DECISIONS.md). | The ODR fix is required **either way**. Beyond that, finishing the native stack is only worth it if you commit to a **D3D12-first title** and accept multi-backend upkeep. Otherwise keep sokol and let the native files be a *clearly-labeled future backend*, not the default. |
| **C2 — archetype/chunk ECS** (~800–900 lines: storage/archetype/chunk/scheduler/query) | Wire the `Scheduler` into a real parallel game loop over a thread-pool, add component reflection, expose to Lua. **L, 2–3 wk.** | A **second** ECS beside the live 125-line sparse-set `World` every game uses; maintaining two cores is the anti-pattern [ADR-0004](DECISIONS.md) rejects. | If you want parallelism, the higher-value move is to **add a scheduler to the LIVE `World`** rather than resurrect the dead archetype path. Finish *that* instead of two cores. |
| **C3 — QUIC via msquic** | Wire msquic into `QuicStream` (connection/stream/ALPN/send/recv/close). **L, 1–2 wk.** Heavy dep. | [ROADMAP §6](ROADMAP.md) defers *all* networking until single-player ships + a determinism ADR exists. | **Finish TCP/UDP over ASIO now** (Phase B, non-fork — lets the real reliability core be tested over sockets); keep **QUIC as an opt-in stretch** you elect after the north-star game. |

> These recommendations are advisory — you said *finish, not delete*, so the
> default is to finish; the table just makes the cost explicit before you spend
> weeks on a duplicate. We can revisit each when Phase C is reached.

## First module — okn-memory ✅ DONE (2026-06-23)

A Layer-0 leaf everything depends on, already ~80% real, **no buy-vs-build
conflict**, fully testable today. The three stubs-behind-flags are now finished
and verified **both ways** — default gate (flags off): **107 cases / 1009
assertions** green; flag-on build (mimalloc + override_new + guard_page):
**108 / 1014** green with `mimalloc.dll` linking and loading:

1. ✅ **mimalloc** (`OKN_MEMORY_USE_MIMALLOC`): `MiBackend` already forwarded to
   `mi_malloc_aligned`/`mi_realloc_aligned`/`mi_usable_size` — the gap was CMake
   *defining the macro without linking* (would fail to build). Now
   `find_package(mimalloc)` + link `mimalloc(-static)`, macro defined only once
   linked, graceful system-backend fallback otherwise. New `test_backend.cpp`
   covers the Sys/Mi backend contract (alignment, `usable_size`, realloc
   preserves data) in both builds.
2. ✅ **guard_page** (`OKN_MEMORY_ENABLE_GUARD_PAGE`): real
   `VirtualProtect`(`PAGE_NOACCESS`↔`PAGE_READWRITE`) / `mprotect`(`PROT_NONE`↔`RW`),
   false on null/failure. Test: protect → write faults (`__try/__except`) →
   unprotect → writable again. *(submodule `6455fc3`)*
3. ✅ **override_new** (`OKN_MEMORY_OVERRIDE_NEW`): an always-compiled routing
   core (`global_alloc`/`global_free`/`new_delete_stats`) backs the full set of
   throwing/nothrow/aligned/sized `operator new[]/delete[]` replacements. The core
   is testable in the gate; the flag-on build also exercises the real operators.
   *(submodule `5612452`)*
4. ✅ **Audit**: `sys_backend.cpp`/`mi_backend.cpp` were empty placeholders — the
   real implementations live in `backend.cpp`; confirmed no remaining
   return-true/comment-only stubs in backend, and the full suite stays green with
   all feature flags on.

**Next module — okn-platform** (Phase A): the Chase-Lev work-stealing deque +
the `UdpSocket` null-`impl_` bugfix. *(Note: okn-platform carries unrelated local
edits; that work lands separately.)*

## Where this meets [ROADMAP.md](ROADMAP.md)

The non-fork program is the roadmap's intent reached by *building out* rather than
pruning: Phase A/B finish the Layer-3 subsystems the roadmap lists as gaps; Phase D
is ROADMAP P5–P7 (the north-star game); the forks (C) are exactly the items the
roadmap's P9 marked for pruning — kept here as the owner's elective finish-it work
with the cost made explicit.
