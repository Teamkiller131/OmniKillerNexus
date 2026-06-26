# OmniKillerNexus — Long-Term Development Plan (v3, 2026-06-24)

> Supersedes **v2 (2026-06-16)**, whose phases P5–P9 are now largely addressed
> (the build-out program in [COMPLETION_PLAN.md](COMPLETION_PLAN.md) — phases A–D
> + the three buy-vs-build forks — and the [EVALUATION.md](EVALUATION.md)
> remediation landed since). This is the authoritative forward plan. It goes
> **deeper**: the engine is no longer trying to close a 2D slice (that is done) —
> it is trying to become a *cross-platform, full-stack, shippable* engine and to
> stop carrying the placeholder halos that re-accrue its honesty debt.
> **Trust the build/test gate, not status labels.** Authoritative module state is
> [ARCHITECTURE.md](ARCHITECTURE.md); the audit is [EVALUATION.md](EVALUATION.md).

---

## 1. What changed since v2 (P5–P9 scorecard, honest)

| v2 phase | Status | Reality |
|---|---|---|
| **P5** Content & persistence foundation | ◑ partial | Disk-PNG import is real (stb importers). The `okn-ecs` binary scene serializer (`EKO1`) round-trips entities + POD components — **but it is wired into no pipeline and no game**, the editor still can't save/reload a scene across a restart, and there is **no atlas packing**. `scene_importer/scene_pipeline/serialize/deserialize` in okn-asset are 4-line stubs. |
| **P6** Gameplay systems | ◑ partial | Most P12 primitives now landed: `okn-ui` keyboard/text input, the **`CharacterController`** (Capsule+Box; platformer + mario3d deleted their raycast controllers), **collision layers/masks honored** + **trigger/sensor volumes** with enter/stay/exit, and an **`okn-input` action-map module** (platformer + mario3d deleted their copy-pasted `InputMap`). Still open: the audio bus/stream/positional classes are wired into nothing audible, and `okn-ui` has no reusable settings/title menu yet. |
| **P7** Forcing-function game | ◑ partial | The **platformer is a complete sprite game** on the `sprite2d + okn-physics + okn-audio` slice, and **VOIDBORNE has the full shell** (title/settings/key-rebind/save) and now runs on `okn-ecs/asset/math`. But **no single game meets the full north-star** *on the engine stack*: the platformer has no title/settings menu and links no ECS/script; VOIDBORNE's shell is UniGUI, not the sprite/physics slice. |
| **P8** Editor as real content tool | ✗ not started | The editor viewport renders **nothing** — `viewport_panel.cpp` is a 4-line stub and `render_bridge.cpp` returns `nullptr`. (Earlier docs said "rasterizes via ImDrawList"; that overstates it — there is no working viewport renderer at all.) No tilemap, no play-in-editor. |
| **P9** Cross-platform, CI, the great prune | ◑ partial | **Prune:** the dead archetype/chunk ECS was **deleted** (15 files) ✓; the fake TCP/UDP transports are now **real over asio** and QUIC honestly reports unavailable ✓; but the **native render lib was partly finished, not deleted** (clear+triangle), leaving ~82 placeholder files. **CI:** the gate now compile-gates all 7 games + asserts VOIDBORNE's autodemo ✓ — but it is **still Windows-only**; **no sokol GL backend**, **no Linux build**, and whether the self-hosted gitea runner actually fires per push is **unverified**. The **determinism ADR is still unwritten.** |

**Net:** the leaf modules got genuinely more real (memory, platform deque, audio
decoders, asset I/O + hot-reload, network transports, the ECS scheduler, the
D3D12 clear+triangle), the dead second ECS is gone, and the code got more honest.
The two structural frontiers v2 named — **cross-platform** and **a game that
composes the core** — are still open, and a new debt surfaced: **placeholder
halos** around small real cores. okn-render's (~113 files) is now **pruned**
(Fork 1); okn-network (~47 `.cpp`) and okn-editor (92) remain.

---

## 2. Where we are now (grounded)

The **2D vertical slice is closed and shipped on**, and a **real 3D mesh path**
exists alongside it:

`input → sparse-set ECS → Jolt physics (+ contact-add events) → 2D sprite render (software ref + sokol/D3D11 GPU) → audio (miniaudio + WAV/MP3/FLAC/OGG decode + playback) → Lua (sol2, runs chunks) → ImGui/UniGUI editor shell → native binary save/load`

Evidence: **7 buildable demo games** across 5 genres (flappy, knockdown,
platformer, mario, mario3d, harvest, voidborne); **~800 test cases / 6109
assertions, `--no-skip` clean**, across 16 module suites + 7 game compiles +
VOIDBORNE's `--selftest`/`--autodemo`, all green in the gate
(`scripts/run_tests_all.ps1`); a real 3D game (mario3d) on the depth-tested
mesh path with Jolt joints/kinematic platforms; VOIDBORNE running on the engine
core (crew = `okn-ecs` entities, data + live `events.json` hot-reload via
`okn-asset`, camera = `okn-math` `Vec2`).

But the honest framing still holds, sharpened:

- **It is Windows/D3D11-only in practice.** The live renderer hard-defines
  `SOKOL_D3D11` (`gpu/sokol_impl_app.cpp`), the game targets are `if(... AND
  WIN32)`, the editor forces `UNIGUI_BACKEND_DX11`, and `D:/vcpkg` is hard-coded
  across scripts and CMake. `okn-platform` *is* coded for POSIX/Apple — but has
  never been **built or CI'd** off Windows.
- **Capability without a consumer is unproven.** The parallel ECS scheduler, the
  `ScriptingBridge` (Lua↔ECS reflection), the audio bus/spatializer/streamer, and
  the `EKO1` scene serializer are all **real but driven by no game** — they are
  unit tests, not capabilities.
- **The placeholder halos.** okn-render's native lib is now pruned (89→3 near-empty
  `.cpp`); ~47 remain in okn-network and 92 in okn-editor. `okn-memory` is real but
  an island (nothing includes it; mimalloc/override default OFF). The
  `check_no_stub_tus.ps1` inventory tracks all of this.

### Known dead / placeholder code (the refreshed prune list — see P11)
- **`okn-render` native lib** — **pruned (Fork 1 done):** the ~113 placeholder
  subsystem `.cpp` (materials/lighting/culling/RT/post-fx/graph/passes/… that did
  no GPU work) + their dead tests were deleted (125 files). The lib now compiles
  only the D3D12 backend (`src/backend/`) — a labeled clear+triangle experiment,
  **not the default path**. Residual: the Vulkan/Metal/GL backend skeletons + the
  orphaned subsystem *headers* (a follow-up header prune).
- **`okn-network`** — ~85 placeholder files (mux/qos/flow/session/routing/9
  integration bridges) around a real-but-**naive** reliability core (in-order
  only, `ack_bitfield` never used, no congestion control) + real asio TCP/UDP;
  QUIC honestly unavailable.
- **`okn-physics`** — a dead native integrator (`RigidBody::integrate`) the Jolt
  world never calls (Jolt does *all* dynamics).
- **`okn-ecs`** — a clean typed `SparseSet<T>` that is **unused** (the live World
  uses slower type-erased blob stores).
- **`okn-audio`** — `AudioMixer`, `AudioSpatializer`, `StreamPlayer` are real
  classes wired into **nothing audible** (`AudioPlayback` sets
  `MA_SOUND_FLAG_NO_SPATIALIZATION`).
- **`okn-asset`** — `compress()` is a no-op passthrough; the scene-pipeline TUs
  are 4-line stubs.

---

## 3. Strategy (unchanged thesis, two new rules)

The thesis is validated and unchanged: **buy undifferentiated infrastructure,
build only game glue.** Jolt, sokol_gfx/app, miniaudio, sol2, stb/assimp, Dear
ImGui/UniGUI are the load-bearing third-party; OKN writes the glue and the games.
Two rules are **added** from the lessons of the A–D build-out:

1. **No placeholder halos.** "Finish, don't delete" produced real capability —
   but also left ~167 placeholder files around small real cores, re-accruing the
   exact honesty debt the project brands against. New rule: **a module may not
   carry a near-empty `.cpp` halo unless every file is in a build target with a
   committed consumer.** Delete or gate. (Enforced by a stub-guard in the gate.)
2. **Every capability has a game consumer in the gate, or it isn't done.** The
   scheduler, the scripting bridge, the audio buses, and the scene serializer are
   all real and all unproven because **no game drives them**. New rule: a core
   feature is "done" only when a **game in the gate exercises it**.

And the standing discipline: vertical slice over horizontal breadth;
game-as-forcing-function; honesty as a feature; gate everything you claim.

---

## 4. The open forks (decide these — they shape the phases)

Three buy-vs-build decisions are live and the roadmap must resolve them, not
inherit them silently.

- **Fork 1 — Native render backend. ✅ RESOLVED (pruned).** Hand-written D3D12
  bought OKN nothing sokol doesn't already give for 2D/3D/slice, and didn't buy
  cross-platform (you'd still owe GL+Metal). **Done:** the ~113 placeholder
  subsystem `.cpp` + their dead tests were deleted (125 files; okn-render
  near-empty `.cpp` 89→3), keeping only the D3D12 clear+triangle as a labeled
  experiment. Revisit only if a specific **D3D12-first title** is committed and
  needs a capability sokol can't express (bindless / GPU-driven culling via
  `ExecuteIndirect` / DXR).
- **Fork 2 — Netcode transport. ✅ DECIDED: build.** The recommendation was to
  *buy* GameNetworkingSockets/ENet, but the **owner chose to build** — and the
  reliability layer was brought up to a usable floor: reorder buffer, 32-bit
  selective-ack bitfield, adaptive RTO (RFC 6298 + Karn), AIMD congestion control,
  and keepalive/liveness, plus the differentiated **ECS state snapshot/delta**
  codec — all adversarially reviewed (10 bugs fixed) and gate-tested. Remaining: a
  real multiplayer demo game wiring it to the ECS, and the determinism ADR before
  any lockstep. (QUIC stays an honest stub; ~45 placeholder subsystem files still
  to prune.)
- **Fork 3 — ECS storage.** Keep the sparse-set World; the no-archetype decision
  is defensible but was made **without a benchmark**. **Recommendation:** first
  fix the hot path (cached dense iteration; adopt the unused typed `SparseSet<T>`)
  and add a microbenchmark; only add archetype-style **chunked iteration as an
  opt-in fast path** if data proves it — never a second whole ECS core. Archetype
  stays dead.

---

## 5. North star (deeper than v2's)

v2's north star — *ship one small complete 2D game* — is effectively within
reach. The deeper one:

> **One complete game that composes the FULL engine stack — `sprite2d + okn-ecs +
> okn-physics + okn-audio + okn-script + okn-ui + okn-input` — with title →
> authored levels → win/lose → save/load → settings (audio + key rebind), built
> and distributable on Windows AND Linux.**

That single goal is the forcing function for nearly everything below: it requires
cross-platform (P10), a clean tree (P11), engine-owned gameplay primitives (P12),
a content pipeline + the scheduler in anger (P13), real audio + scripting (P14),
and a CI that produces per-platform artifacts. *That game is the acceptance test
for the engine.*

---

## 6. Phased plan (P10+ — each phase = a milestone with acceptance criteria)

### P10 — Cross-platform foundation (the structural unlock)
The single biggest gap, and the prerequisite for the north star's "Linux" half.
The render code is already backend-agnostic; only `sokol_impl_*.cpp` picks the
define, and glfw3+glad are already vcpkg deps — this is wiring + CI, not new
graphics code.
- **`CMakePresets.json`** as the one cross-platform entrypoint (`windows-msvc`,
  `linux-clang`, later `macos-clang`); toolchain via `$env{VCPKG_ROOT}`. **Delete
  every hard-coded `D:/vcpkg`** (scripts + `okn-platform/CMakeLists.txt`).
- **`ctest` entrypoint** for the suites (replace the bespoke PowerShell target
  mapping so a Linux runner can `ctest --preset` identically).
- **sokol `GLCORE` backend** TU for the sprite2d/mesh3d/slice paths; drop the
  `AND WIN32` guard on game targets.
- **Public GitHub mirror** of the root repo + all 12 NAS-only submodules
  (bus-factor insurance *and* the precondition for hosted Linux CI to fetch
  submodules).
- **Matrix CI** (windows-latest + ubuntu-latest) that builds the foundation
  (core/math/memory/platform/ecs/asset) + VOIDBORNE and runs `ctest --preset`.
- **Acceptance:** green CI on **Windows AND Linux**; a fresh clone configures with
  only `VCPKG_ROOT` set (no path edits); **VOIDBORNE `--autodemo` passes on
  Linux**; one windowed game renders via sokol GL.

### P11 — The great prune II (honesty reset)
Cheap, high-signal, do it early. Resolve the halos so the tree stops over-stating.
- **Fork 1:** delete the native-render placeholder ~82 `.cpp` + the Vulkan/Metal/GL
  skeletons + the GPU-less render graph; keep the D3D12 clear+triangle as a labeled
  `experiments/` artifact. (Or, if a D3D12 title is committed, write the ADR and
  scope it — but don't carry the halo undecided.)
- **Network halo:** delete the ~85 unbuilt placeholder files; keep
  reliability + transports + their tests until Fork 2 is executed.
- Delete the dead native physics integrator; **adopt or delete** the unused typed
  `SparseSet<T>` (decided in P13/Fork 3).
- **Fix the silent sprite-batcher frame-drop** (`gpu_sprite_renderer.cpp` `return;`
  on >8192 sprites) → grow-or-flush + a warn hook.
- Add **`scripts/check_no_stub_tus.ps1`** to the gate (flag any compiled
  comment/BOM-only `.cpp`) so halos can't creep back.
- **Acceptance:** near-empty `.cpp` ratio drops to the legitimate header-first
  set; the stub-guard is green; no module ships a placeholder halo without a
  committed consumer; a busy scene never silently renders nothing.

### P12 — Gameplay primitives in the engine (not per-game)
Move the duplicated game-side hacks into the modules, so the north-star game (and
the next one) compose them instead of reinventing them.
- ✅ **`okn-physics` `CharacterController`** (Jolt `CharacterVirtual`: grounded /
  slope / step / collide-and-slide; **Capsule + Box** shapes, `plane_2d` lock for 2D
  + full-3D). **Done** — `CharacterDesc` + `create_character`/`character_move`/
  `character_is_grounded`/… on `IPhysicsWorld`; 19 `okn-physics_tests` cases.
  platformer (2D Box) and mario3d (3D Box) **deleted their hand-rolled raycast
  controllers** and re-point at it: platformer now clears L0+L1 (was stuck on L1);
  mario3d autodemo WINs + swingtest rides the hinge-bridge. mario3d reimplements its
  stomp as a manual overlap since a `CharacterVirtual` isn't a contact body.
- ✅ **Collision layers/masks honored** (32-layer `collision_group`/`collision_mask`,
  mutual-mask test in the Jolt `GroupFilter`) + **trigger/sensor volumes** (`is_sensor`
  → Jolt `mIsSensor`, no collision response) with **enter/stay/exit** (`ContactEvent.phase`;
  `OnContactPersisted`/`OnContactRemoved`, Stay/Exit gated to sensors so collision
  consumers aren't flooded). **Done** — makes "Jolt contacts-as-triggers" *fully* real;
  gated by a trigger enter/stay/exit headless test (+4 tests, suite 23/83). The
  ObjectLayer scheme + CharacterController filter were left untouched (no regression).
- ✅ **`okn-input` action-mapping module** — `ActionMap<Action>` (header-only template,
  backend-agnostic `KeyCode=uint32_t`): `bind`/`rebind`/`key_of`, `on_key`,
  `held`/`just`/`just_released`, `end_frame`, `clear_state`, and binary `save`/`load`.
  **Done** — platformer + mario3d deleted their copy-pasted `InputMap` struct and run on
  it (the same `using InputMap = ActionMap<Action>;` drives both); 8 headless tests in
  the gate (suite #17). harvest/mario can adopt it next.
- **`okn-ui` menus** — now that text input exists, build a reusable settings/title
  menu (sliders, key-capture rows) on the real widget set.
- **Acceptance:** platformer + mario3d both move via the module controller (their
  bespoke ones deleted); a **trigger-volume enter/exit test** is in the gate; one
  action-map config drives ≥2 games; a reusable menu renders on `okn-ui`.

### P13 — Content pipeline + the scheduler in anger
Turn the disconnected serializers and the untested scheduler into real, exercised
capabilities.
- **Project/scene pipeline:** ✅ **entity-ref remapping on load** + ✅ **scene save/load
  to disk**. The EKO1 loader is two-pass (build a saved-id→new-id remap, then patch
  component fields declared via `register_entity_ref_fields<T>`), and `Serializer` now has
  `save_to_file`/`load_from_file`, so a scene **survives a process restart** with its
  cross-entity references intact — proven by a disk round-trip test that reloads a
  hero→sword link into a fresh World. *Still to do:* promote the serializer into an
  `okn-asset` `SceneAsset` + project manifest (scenes + asset-references-by-id) routed
  through `AssetIO`/`PackWriter`, and wire the editor's restart test onto it.
- ✅ **Runtime atlas packing** landed — `build_atlas()` shelf-packs source images into
  one atlas Image + per-source normalized uv_rects, so atlased sprites share one
  texture_id and the batcher emits one DrawGroup instead of N (header-only, deterministic,
  4 headless tests incl. a 3-textures→1-draw assertion). *Still to do:* hook it into the
  asset/sprite-load path so disk PNGs are atlased automatically.
- **Scheduler in anger:** ✅ the **per-frame O(n²) re-levelization** is fixed (conflict
  levels are now cached + rebuilt only on `invalidate_order()`; test asserts one
  levelization across many frames) and the **busy spin-wait** barrier is replaced by the
  job system's CV-backed `wait_all()`. ✅ The **query hot path** now caches each
  component's store pointer once per `query()` (was a hash-map `find()` per component per
  entity) and derefs through the cached sparse-set stores — verified by a 3-lens
  adversarial review + a 600-entity intersection test. ✅ The scheduler is **unified onto
  okn-platform's Chase-Lev `WorkStealingThreadPool`** (its duplicate `IJobSystem` +
  `ThreadPoolJobSystem` deleted), whose `wait_all()` is now CV-backed instead of a
  yield-spin — also adversarially reviewed (lost-wakeup/deadlock-free; the exclusive-pool
  per-level-join invariant is documented). **Scheduler frontier complete.**
- **Acceptance:** a scene survives a restart through the engine format; one game
  runs **N real systems through the scheduler** with a **parallel-speedup
  assertion in the gate**; a query microbenchmark locks in the iteration gain.

### P14 — Audio and scripting become real features
Wire the dead-but-real classes into the audible/runtime path, each proven by a
game.
- **Audio:** expose `ma_sound_group` buses (SFX / Music / Master) with ducking;
  drop `NO_SPATIALIZATION` and feed `ma_sound` 3D position/listener so the
  spatializer math actually drives gain/pan; stream BGM with
  `MA_SOUND_FLAG_STREAM`.
- **Scripting:** ✅ `ScriptingBridge` → sol2 bridged — `bind_ecs()` lets a Lua script
  create/destroy entities and add/has/query components by name on the **live World**
  (read/write of component fields via per-component sol2 usertypes); the ECS side gained
  type-erased `component_data_by_id`/`add_component_by_id`/`entities_with`. Proven by a
  headless Lua test driving a real `okn-ecs` World. *Still to do:* hot-reload a gameplay
  Lua file via `okn-asset` `HotReload`, and drive a game's rules from it.
- **Acceptance:** one game ships **positional ambient SFX + a streamed music bed +
  a settings-driven SFX/Music split**; one game's rules (spawn/win/lose/tuning)
  live in a **hot-reloadable Lua file** driving the ECS.

### P15 — The north-star game (composes everything)
Take **one** game — promote the platformer, or build a new small title — to the
full loop **on the full stack**: title → editor/data-authored levels → win/lose →
save/load → settings (audio + rebind), on `sprite2d + okn-ecs + okn-physics +
okn-audio + okn-script + okn-ui + okn-input`, shipping **cross-platform**
(Windows + Linux) as a packaged artifact.
- **Packaging:** `install(TARGETS)` + asset staging + **CPack** (ZIP/NSIS on
  Windows, TGZ on Linux); CI publishes the per-platform bundle on a tag.
- This is where P10–P14 are validated and the *next* gap list is generated.
- **Acceptance:** a person who has never seen the code **downloads and finishes it
  on Windows AND Linux**; progress persists across runs; audio/keys are
  configurable; it **links the full stack**; CI emits the per-platform bundle.

### P16 — Editor as the real content tool
- Render the engine's own `GpuSpriteRenderer`/mesh3d into the editor viewport via
  an **offscreen sokol render target** presented in the panel (replace the
  `nullptr` stub). (First settle the editor's own fork: the tree carries Qt build
  artifacts *and* a UniGUI shell — pick one.)
- **Tilemap / level editing**, **play-in-editor**, real asset thumbnails (rendered
  offscreen).
- **Acceptance:** every level in the P15 game is authored **in-editor, WYSIWYG
  against the real renderer**.

### P17 — Networking (gated: only after P15 ships + the determinism ADR)
- **Write the determinism ADR** first: default to **server-authoritative
  state-sync** (works with Jolt's cross-platform determinism *off*); treat
  **lockstep** as an explicit opt-in that requires `JPH_CROSS_PLATFORM_DETERMINISTIC`
  (perf cost, disables FMA) — with measured numbers, not speculation.
- **Execute Fork 2:** buy GameNetworkingSockets/ENet; build the **snapshot/delta
  state-replication** glue on the sparse-set World (using `ScriptingBridge`
  reflection — finally a consumer for it).
- A minimal **2-player server-authoritative demo** (e.g. top-down arena)
  composing `okn-ecs + okn-physics + sprite2d + network`.
- **Acceptance:** the ADR is merged; the 1v1 demo runs server-authoritative with
  client interpolation; a **bit-exact input-replay test** is in the gate.

---

## 7. Continuous tracks (run alongside the phases)
- **No placeholder halos** — every phase removes more dead surface than it adds;
  the stub-guard stays green.
- **Every capability has a game consumer** — no feature is "done" until a gated
  game drives it.
- **Determinism/perf harness** — record/replay + state-hash + microbenchmarks,
  built once (P13) and reused for the scheduler, physics, and netcode.
- **Docs-vs-reality** — keep ARCHITECTURE/EVALUATION matching the gate; correct
  stale claims as they're found (e.g. ARCHITECTURE still says okn-network has no
  suite — it does; the editor is described as ImGui-only but carries Qt artifacts).

---

## 8. Explicit non-goals (deferred — do not start these yet)
- **PBR / GI / raytracing / shadows-heavy 3D** — the mesh path stays honest
  (Lambert + textures at most) unless a 3D title is committed.
- **A finished native D3D12/Vulkan/Metal backend** — sokol is the renderer;
  native is deleted-or-experiment per Fork 1 until a D3D12-first title needs it.
- **Lockstep multiplayer** — state-sync first; lockstep only behind the
  determinism flag and a game that demands it.
- **Resurrecting the archetype ECS** — sparse-set stays; chunked iteration is at
  most an opt-in fast path proven by benchmark (Fork 3).
- **Mobile / console.**

---

## 9. Buy-vs-build ledger (current truth)

| Subsystem | Decision | Status |
|---|---|---|
| Physics | **Buy** Jolt | Real wrapper, the reference module. Gaps: char controller, triggers/sensors, layers, MT, determinism flag. Dead native integrator to prune. |
| 2D render | **Build** glue over **buy** sokol_gfx | Real (sw ref + GPU). Windows/D3D11-only; batcher silently drops >8192 sprites. Needs GL backend. |
| 3D render | **Build** glue over **buy** sokol_gfx | Real depth-tested mesh path (mario3d). Single Lambert light, no textures/materials/shadows. |
| Native GPU backend | **Decided (Fork 1): pruned** | D3D12 clear+triangle kept as a labeled experiment; the ~113 placeholder subsystem .cpp + GPU-less render graph were deleted. Revisit only for a committed D3D12-first title. |
| Windowing/input | **Buy** sokol_app | Real, DPI-aware. Input action-mapping should be an `okn-input` module, not per-game. |
| Audio | **Buy** miniaudio | Decode (WAV/MP3/FLAC/OGG) + playback real (6 games). Bus/spatial/stream classes exist but wired into nothing audible. |
| Scripting | **Buy** sol2/Lua | Runs chunks. `ScriptingBridge`→ECS reflection real but **no game runs Lua**; no live-World bindings yet. |
| ECS | **Build** sparse-set World | Real + scheduler + save/load. Scheduler/serializer have **no game consumer**; query path allocates; typed `SparseSet<T>` unused. Archetype stays dead (Fork 3). |
| Memory | **Build** arenas/pools + **buy** mimalloc | Real but an **island** (nothing includes it; mimalloc/override default OFF). Wire it to the ECS or scope the claim. |
| Threading | **Build** Chase-Lev pool | Real lock-free deque — but the ECS scheduler uses a *separate* slower mutex-queue pool. Bridge them. |
| Assets | **Buy** stb/assimp | Importers + I/O + hot-reload + pack format real. No atlas/compression; scene pipeline is stubs; streaming is single-threaded. |
| UI | **Build** | Widgets + layout + keyboard/text real. **No game uses it for menus yet.** |
| Editor | **Buy** Dear ImGui via UniGUI (Qt artifacts linger) | Shell only; **viewport renders nothing** (stub). Pick ImGui-vs-Qt; wire the real renderer. |
| Networking | **Decided (Fork 2): build** | Reliable-over-loss/reorder + adaptive RTO + AIMD congestion + keepalive/liveness + online Session + ECS state-sync, all reviewed + gated (116/916). QUIC honest stub; ~45 placeholder files left. Needs a demo game + the determinism ADR. |
| Cross-platform | **Build** (presets + GL) over portable sources | `okn-platform` coded for POSIX/Apple but never built off Windows. The blocker is the render/windowing edge + `D:/vcpkg` + no Linux CI. |

---

## 10. Risks & watch-items
- **sokol pinned to the pre-2024 API** — the GL/Metal backends must be validated
  against that pin (behavior differs from current sokol); upgrades are deliberate.
- **The placeholder-halo honesty debt** — left unpruned (P11), the tree keeps
  over-stating; this is the project's signature risk because honesty is its edge.
- **Capability-without-consumer** — the scheduler/scripting/audio/serializer can
  look "done" in tests yet break under a real game; the "game consumer in the
  gate" rule is the mitigation.
- **Bus factor / NAS single point of failure** — root + 12 of 13 submodules
  resolve only from `xbw-nas.iepose.cn`; a public mirror (P10) is both insurance
  and the precondition for hosted Linux CI to fetch submodules.
- **Self-hosted CI may not actually fire** — "live gate on push" is unverified for
  the Windows gitea runner; P10's hosted matrix CI makes the gate observable.
- **Jolt cross-platform determinism is opt-in** (`JPH_CROSS_PLATFORM_DETERMINISTIC`,
  perf cost, disables FMA) → lockstep needs it on; state-sync/input-replay don't.
  Single-player is unaffected.
- **HiDPI** — sokol windowed apps need the PerMonitorV2 manifest + `high_dpi`
  (already applied to the games; bake it into any new windowed target).

---

## 11. Immediate next three (actionable now)
1. **P10 kickoff** — add `CMakePresets.json` and **delete every hard-coded
   `D:/vcpkg`** (scripts + `okn-platform/CMakeLists.txt`); make `configure` a
   one-liner driven by `VCPKG_ROOT`. (Days; unblocks every non-author build.)
2. **P10** — wire a **sokol `GLCORE`** impl TU and stand up a **Linux build of the
   foundation + VOIDBORNE** (`--autodemo`, headless) behind a `linux-clang` preset;
   turn the disabled Linux CI stub green.
3. **P11 ✅ done** — Fork 1 resolved: the native-render placeholder halo was pruned
   (125 files; okn-render near-empty `.cpp` 89→3, gate still green). **Next halos:**
   okn-network's ~47 placeholder files and okn-editor's 92 (its viewport is a stub),
   then wire `check_no_stub_tus.ps1 -Strict` with a baseline into the gate.
