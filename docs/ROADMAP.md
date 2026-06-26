# OmniKillerNexus — Long-Term Development Plan (v4, 2026-06-26)

**Thesis (unchanged, validated): buy undifferentiated infrastructure, build only the
game glue.** Jolt, sokol_gfx/app, miniaudio, sol2, stb/assimp, Dear ImGui/UniGUI are the
load-bearing third-party; OKN writes the glue, the engine-owned gameplay primitives, and
the games. v4 supersedes v3 (2026-06-24): since then the **P12 gameplay-primitive phase
closed**, the **P13 content/scheduler phase landed its headless core**, and the **P14
scripting bridge + audio bus model** shipped — all gate-verified, the riskiest pieces
adversarially reviewed. What remains is mostly **windowed** (needs a display/audio device)
and the **cross-platform structural unlock (P10)**, still the single biggest gap.

---

## 1. What changed since v3 (the headless build-out)

Every item below is in the gate (`scripts/run_tests_all.ps1`) and pushed.

**P12 — Gameplay primitives in the engine — ✅ COMPLETE**
- **`CharacterController`** (`okn-physics`, Jolt `CharacterVirtual`; Capsule+Box, `plane_2d`
  lock, collide-and-slide + auto-step + grounded). Platformer (2D) + mario3d (3D) deleted
  their hand-rolled raycast controllers and run on it.
- **Collision layers/masks honored** (32-group mutual-mask test in the Jolt `GroupFilter`,
  ObjectLayer scheme + CharacterController filter untouched) + **sensor/trigger volumes**
  (`is_sensor`, no impulse) + **contact `Enter`/`Stay`/`Exit`** (Stay/Exit gated to sensors
  so collision consumers aren't flooded). The branded "Jolt-contacts-as-triggers" edge is
  now real.
- **`okn-input`** — a new module (#14): backend-agnostic `ActionMap<Action>` (bind / rebind
  / `held`/`just`/`just_released` / save-load). Platformer + mario3d deleted their
  copy-pasted `InputMap`.

**P13 — Content pipeline + scheduler in anger — ◑ headless core done**
- **Scheduler frontier complete**: per-frame O(n²) re-levelization → **cached levels**;
  busy spin-wait → **CV-backed `wait_all()`**; query hot path → **cached `ComponentStore`
  pointers** (was a hash-map lookup per component per entity); and the ECS scheduler is
  **unified onto `okn-platform`'s Chase-Lev work-stealing pool** (its duplicate `IJobSystem`
  + `ThreadPoolJobSystem` deleted).
- **Scene pipeline**: **entity-ref remapping on load** (refs survive save/restart/reload),
  **`Serializer` save/load to disk**, a real **`okn-asset` `SceneImporter`** (was a stub —
  validates the EKO1 header, ECS-agnostic), and **scene-through-`PackWriter` bundling**
  (scene → `.oknp` as a `kScene` entry → unpack by type → re-import).
- **Atlas**: **`build_atlas()`** (shelf-pack N images → one atlas + uv rects → one DrawGroup)
  and the usable **`TextureAtlas`** named layer (`make_sprite(name)`).

**P14 — Audio + scripting become real — ◑ scripting done, audio core done**
- **Lua ↔ ECS bridge**: `ScriptingBridge` gained a type-erased **read/write/query-by-name**
  surface (+ `World::component_data_by_id`/`add_component_by_id`/`entities_with`), and
  `okn-script`'s **`bind_ecs()`** exposes it to sol2 so a **Lua script drives the live
  World**. **Hot-reloadable Lua rules** demonstrated (an `on_tick` rules file, reloaded to
  swap rules while the World persists).
- **Audio bus model**: `AudioMixer` buses now form a **hierarchy** (gain chains SFX/Music →
  Master → root) with **ducking** (`set_duck` dips a bus while another has audio) — proven
  by mixed-amplitude tests, backward-compatible with flat buses.

---

## 2. Where we are now (grounded)

The slice is **`input → okn-input action map → sparse-set ECS (parallel scheduler on a
work-stealing pool) → Jolt physics (CharacterController + layers/masks + sensors + contact
phases) → 2D sprite render (software ref + sokol/D3D11 GPU, runtime atlas) → audio (decode +
playback + a hierarchical ducking bus model) → Lua drives the live ECS (sol2) → ImGui/UniGUI
editor shell → EKO1 scene save/load + SceneImporter + pack bundling`**.

Evidence: **14 modules**, **17 module test suites + 7 game compiles + VOIDBORNE
`--selftest`/`--autodemo` + okn-editor `--selftest`**, all green in the gate (e.g. okn-ecs
60/3168, okn-asset 117/406, okn-audio 49/465, okn-platform 113/182, okn-render2d 15/99,
okn-physics 23/82, okn-input 8/30, okn-script 16/56). 7 buildable demo games across 5 genres
(flappy, knockdown, platformer, mario, mario3d, harvest, voidborne); VOIDBORNE runs on the
engine core.

**The honest framing that still holds:**
- **Windows/D3D11-only in practice.** The live renderer hard-defines `SOKOL_D3D11`; game
  targets are `if(... AND WIN32)`; the editor forces `UNIGUI_BACKEND_DX11`. `okn-platform` is
  coded for POSIX/Apple but has **never been built or CI'd off Windows**. *This is P10, the
  biggest remaining gap.*
- **Capability-with-consumer is now much better — but two big ones are still windowed.** The
  audio bus model and the spatializer are tested but **not wired to `ma_sound_group`/`ma_sound`
  (nothing audible yet)**; no single game yet meets the full north-star *on the engine stack*.
- **Placeholder halos remain** in `okn-network` (~45 files) and `okn-editor` (its viewport is
  a stub); `check_no_stub_tus.ps1` exists but isn't wired `-Strict` into the gate. `okn-memory`
  is still an island.

---

## 3. Strategy (unchanged thesis, the two standing rules)

1. **No placeholder halos.** A module may not carry a near-empty `.cpp` halo unless every
   file is in a build target with a committed consumer. Delete or gate. (Wire the stub-guard.)
2. **Every capability has a game consumer in the gate, or it isn't done.** This is now the
   dominant lens: the headless cores of P12–P14 are real, but several (audio buses, the Lua
   bridge, the scene pipeline, scheduler parallelism) are still proven by **tests, not a game**.
   P15 is what turns them into capabilities.

Standing discipline: vertical slice over horizontal breadth; game-as-forcing-function;
honesty as a feature; gate everything you claim.

---

## 4. The open forks (status)

- **Fork 1 — Native render backend. ✅ RESOLVED (pruned).** D3D12 clear+triangle kept as a
  labeled experiment; ~113 placeholder subsystem `.cpp` deleted. Revisit only for a committed
  D3D12-first title.
- **Fork 2 — Netcode transport. ✅ DECIDED: build.** Reliable-over-loss/reorder + adaptive RTO
  + AIMD congestion + keepalive + ECS state snapshot/delta, all reviewed + gated. Remaining: a
  demo game wiring it to the ECS, the determinism ADR, and ~45 placeholder files to prune.
- **Fork 3 — ECS storage. ✅ HOT PATH ADDRESSED.** v3 said "first fix the hot path (cached
  dense iteration) + benchmark before any archetype work." The **query hot path is now cached**
  (store pointers resolved once per `query()`); archetype stays dead. Remaining (optional): a
  microbenchmark to lock the gain, and adopt the typed `SparseSet<T>` only if data demands it.

---

## 5. North star (unchanged — the acceptance test for the engine)

> **One complete game that composes the FULL engine stack — `sprite2d + okn-ecs +
> okn-physics + okn-audio + okn-script + okn-ui + okn-input` — with title → authored levels →
> win/lose → save/load → settings (audio + key rebind), built and distributable on Windows
> AND Linux.**

Nearly every primitive that game needs now **exists and is tested**. What it still needs:
the cross-platform half (P10), audible audio + a settings menu (P14/P16 tails), and the
integration itself (P15).

---

## 6. Phased plan (forward — current status + acceptance)

### P10 — Cross-platform foundation — ✗ the biggest remaining gap *(headless-partial)*
The prerequisite for the north star's "Linux" half. Largely untouched: the live render path
is D3D11-only, there is no sokol `GLCORE` TU, no Linux build, and `D:/vcpkg` is still
hard-coded across scripts/CMake (13 files). **First slices (verifiable on Windows):** purge
`D:/vcpkg` → `$VCPKG_ROOT`; cross-compile the sprite/mesh shader pairs to GLSL (validate on
D3D11/dummy); add a `sokol_impl_gl.cpp` (`SOKOL_GLCORE`). **Then (needs a Linux runner):** a
`linux-clang` preset, a headless VOIDBORNE `--autodemo` on Linux, hosted matrix CI.
**Acceptance:** green CI on Windows AND Linux; VOIDBORNE `--autodemo` passes on Linux.

### P11 — The great prune II — ◑ partial
Fork 1 pruned (125 files). **Remaining:** delete `okn-network`'s ~45 placeholder files + the
committed `okn-editor` Qt build artifacts; wire `check_no_stub_tus.ps1 -Strict` (baseline-diffed)
into the gate so halos can't regress.

### P12 — Gameplay primitives — ✅ COMPLETE
CharacterController · layers/masks · sensors · contact phases · `okn-input`. *(Soft leftover:
a reusable `okn-ui` settings/title menu — folds into P16/P15.)*

### P13 — Content pipeline + scheduler — ◑ core done
Scheduler frontier complete; scene pipeline (ref-remap, disk save/load, SceneImporter, pack
bundling) + atlas (packing + TextureAtlas) done. **Remaining:** a **project manifest** (scenes
+ asset-refs-by-id), an **asset-load step that auto-atlases** a game's loose PNGs, the
editor's **save→restart→reload** test wired onto the engine format, and (optional) a **query
microbenchmark** + a non-flaky parallel-speedup signal.

### P14 — Audio + scripting real — ◑ scripting done, audio core done
**Done:** Lua drives the live ECS + hot-reloadable rules; the bus hierarchy + ducking model;
the **spatializer math** (listener-relative pan via `cross(forward,up)` + miniaudio-style
inverse distance attenuation, headless-tested).
**Remaining (windowed):** route each bus through a `ma_sound_group` and drop
`NO_SPATIALIZATION` so the bus model + spatializer drive a real `ma_sound`; stream BGM with
`MA_SOUND_FLAG_STREAM`; **prove it in a game.** **Acceptance:** one game ships positional ambient SFX + a streamed
music bed + a settings-driven SFX/Music split; one game's rules live in a hot-reloaded Lua file.

### P15 — The north-star game — ✗ not started (the next big push)
Promote the platformer (or a new small title) to the full loop on the full stack, shipping
cross-platform as a packaged artifact (`install` + asset staging + **CPack**; CI publishes the
bundle on a tag). This is where P10–P14 become **capabilities** and the next gap list is born.
**Acceptance:** a stranger downloads and finishes it on Windows AND Linux; progress persists;
audio/keys configurable; it links the full stack; CI emits the per-platform bundle.

### P16 — Editor as the real content tool — ✗ not started
Replace the viewport `nullptr` stub with the engine's own renderer into an **offscreen sokol
target** (first settle the ImGui-vs-Qt fork). Tilemap / level editing, play-in-editor,
offscreen thumbnails. **Acceptance:** every P15 level is authored in-editor, WYSIWYG against
the real renderer.

### P17 — Networking — gated (only after P15 + the determinism ADR)
Write the **determinism ADR** (default server-authoritative state-sync; lockstep is opt-in
behind `JPH_CROSS_PLATFORM_DETERMINISTIC`). Build the snapshot/delta replication glue on the
sparse-set World (the `ScriptingBridge` reflection is finally a consumer). A 2-player
server-authoritative demo. **Acceptance:** ADR merged; 1v1 demo runs with client interpolation;
a bit-exact input-replay test in the gate.

---

## 7. Continuous tracks
- **No placeholder halos** — every phase removes more dead surface than it adds; wire the
  stub-guard.
- **Every capability has a game consumer** — the now-dominant rule; P15 is the forcing function.
- **Determinism/perf harness** — record/replay + state-hash + microbenchmarks, built once and
  reused for scheduler/physics/netcode.
- **Docs-vs-reality** — keep ARCHITECTURE matching the gate; correct stale claims as found.

---

## 8. Explicit non-goals (do not start yet)
PBR/GI/raytracing/shadows-heavy 3D · a finished native D3D12/Vulkan/Metal backend (sokol is
the renderer) · lockstep multiplayer (state-sync first) · resurrecting the archetype ECS
(sparse-set stays; chunked iteration only if a benchmark demands it) · mobile/console.

---

## 9. Buy-vs-build ledger (refreshed)

| Subsystem | Decision | Status |
|---|---|---|
| Physics | **Buy** Jolt | Reference module. **CharacterController + layers/masks + sensors + contact phases now real.** Single-threaded; dead native integrator to prune. |
| 2D render | **Build** glue / **buy** sokol_gfx | Real (sw ref + GPU) + **runtime atlas + TextureAtlas**. Windows/D3D11-only; needs a GL backend (P10). |
| 3D render | **Build** glue / **buy** sokol_gfx | Depth-tested mesh path (mario3d). Single Lambert light; no textures/materials/shadows. |
| Windowing/input | **Buy** sokol_app + **build** `okn-input` | Real. **Action-mapping is now an `okn-input` module** (2 games on it). |
| Audio | **Buy** miniaudio | Decode + playback real. **Bus hierarchy + ducking model + spatializer math (pan + distance) now real + tested** — but not yet wired to `ma_sound_group`/`ma_sound` (nothing audible). |
| Scripting | **Buy** sol2/Lua | **`bind_ecs()` lets Lua drive the live World** (create/destroy/add/read/write/query); hot-reloadable rules demonstrated. Needs a *game* on Lua rules. |
| ECS | **Build** sparse-set World | Real + parallel scheduler (on the work-stealing pool) + save/load + **cached query hot path**. Scheduler/serializer still need a game consumer. Archetype stays dead. |
| Threading | **Build** Chase-Lev pool | **Unified**: the ECS scheduler now runs on `okn-platform`'s work-stealing pool; the duplicate ECS pool was deleted; `wait_all()` is CV-backed. |
| Assets | **Buy** stb/assimp | Importers + I/O + hot-reload + pack real. **SceneImporter real + scene pack bundling proven.** No compression; no project manifest; streaming single-threaded. |
| Memory | **Build** arenas/pools + **buy** mimalloc | Real but an **island** (nothing includes it). Wire it or scope the claim. |
| UI | **Build** | Widgets + layout + keyboard/text real. **Still no game uses it for menus.** |
| Editor | **Buy** ImGui via UniGUI (Qt artifacts linger) | Shell + multi-level undo; **viewport renders nothing** (stub). Pick ImGui-vs-Qt; wire the real renderer (P16). |
| Networking | **Decided: build** | Reliable + adaptive RTO + AIMD + keepalive + ECS state-sync, reviewed + gated. QUIC honest stub; ~45 placeholder files left. Needs a demo game + the ADR. |
| Cross-platform | **Build** (presets + GL) | `okn-platform` POSIX/Apple-coded but never built off Windows. Blocker: render/windowing edge + `D:/vcpkg` + no Linux CI. **Top priority (P10).** |

---

## 10. Risks & watch-items
- **The cross-platform gap (P10)** is now the dominant risk: the north star's "Linux half"
  blocks on the GL backend + a Linux runner, none of which exist yet.
- **Capability-without-a-game** — the audio buses, Lua bridge, scene pipeline, and scheduler
  parallelism are tested but unproven under a real game; P15 is the mitigation.
- **Placeholder-halo honesty debt** — `okn-network`/`okn-editor` halos + the unwired
  stub-guard keep the tree over-stating; honesty is the project's edge.
- **Windowed verification ceiling** — audio audibility, the editor viewport, and the P15 game
  can't be held to the headless-gate bar on a single Windows dev box; they need a display/audio
  device (and ideally the matrix CI from P10).
- **Bus factor / NAS single point of failure** — root + most submodules resolve only from
  `xbw-nas.iepose.cn` (which flaked mid-session); a public mirror is insurance + the precondition
  for hosted Linux CI to fetch submodules.
- **Jolt cross-platform determinism is opt-in** → lockstep needs it on; state-sync/input-replay
  don't. Single-player unaffected.

---

## 11. Immediate next (actionable now)
1. ✅ **P14 audio — the spatializer math** landed (`okn-audio` `spatializer.hpp`):
   listener-relative pan + inverse distance attenuation, headless-tested. The remaining
   `ma_sound`/`ma_sound_group` wiring is windowed.
2. **P10 cheap slice** (fully verifiable on Windows): purge the 13 hard-coded `D:/vcpkg`
   references → `$VCPKG_ROOT`; cross-compile the sprite/mesh shaders to GLSL on the D3D11/dummy
   path. Unblocks every non-author build and de-risks the GL backend.
3. **P11 honesty** — wire `check_no_stub_tus.ps1 -Strict` (baseline-diffed) into the gate;
   prune `okn-network`'s ~45 placeholder files + the `okn-editor` Qt artifacts.
4. **P15 kickoff (the big one)** — pick the north-star title (promote the platformer is the
   shortest path: it already has the CharacterController, `okn-input`, save/load) and start the
   full-stack loop: title → data-authored levels (SceneImporter) → settings (okn-ui + okn-input
   rebind + the audio bus split) → hot-reloaded Lua rules. This is what converts P12–P14's
   headless cores into capabilities.
