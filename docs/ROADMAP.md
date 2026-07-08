# OmniKillerNexus — Long-Term Development Plan (v5, 2026-06-28)

**Thesis (unchanged, validated): buy undifferentiated infrastructure, build only the
game glue.** Jolt, sokol_gfx/app, miniaudio, sol2, stb/assimp, Dear ImGui/UniGUI are the
load-bearing third-party; OKN writes the glue, the engine-owned gameplay primitives, and
the games. v4 supersedes v3 (2026-06-24): since then the **P12 gameplay-primitive phase
closed**, the **P13 content/scheduler phase landed its headless core**, and the **P14
scripting bridge + audio bus model** shipped — all gate-verified, the riskiest pieces
adversarially reviewed. What remains is mostly **windowed** (needs a display/audio device)
and the **cross-platform structural unlock (P10)**, still the single biggest gap.

**v5 adds §12–§13: the untapped-potential dig.** A 14-agent code-grounded sweep
(7 lenses × mine→adversarially-verify) found that the dominant lever is no longer building
new capability — it is that *a large fraction of what's already built and tested has no game
consumer*. The bus mixer, spatializer, ECS Serializer, the entire netcode stack, the Rollback
ring, SimdVec4, okn-ui, okn-memory, and the contact Stay/Exit phases are all real, tested, and
**unconsumed**. §12 is the verified backlog of those unlocks (plus one genuinely new reach
surface — a browser/WASM build that free-rides the P10 GLSL); §13 sequences the cheap ones.
Every item below was checked against the actual files; effort/leverage are the post-verification
numbers, and rejected/over-stated claims were dropped (e.g. a macOS/Metal "free rider" — false,
sokol needs hand-authored MSL, no source reuse).

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
- **Placeholder halos** — okn-network's are pruned and the **stub-guard `-Strict` is now wired
  into the gate** (a baseline ratchet: no new halos can appear); the bigger ones (okn-editor 92,
  okn-audio 41, okn-script 36, …) remain in the frozen baseline to prune over time. `okn-memory`
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
The prerequisite for the north star's "Linux" half. The live render path is D3D11-only, there
is no sokol `GLCORE` TU, and no Linux build. ✅ **`D:/vcpkg` purge:** the hard-coded toolchain
ref in the GitHub CI workflow + the `doctest_DIR` fallbacks in 4 module CMakeLists now drive
off `$VCPKG_ROOT` (configure/run_tests already did; okn-platform's CMakeLists is left as the
one remaining — it carries a protected pre-existing `BUILD_SAMPLES` edit, so its line can't be
committed without that edit). **Remaining (needs the GL backend / a Linux runner):** add a
`sokol_impl_gl.cpp` (`SOKOL_GLCORE`) and the GLSL sprite/mesh shader variants **together**, so
the GLSL is actually exercised (adding unexercised GLSL strings on the D3D11-only path now
would be a placeholder, against the no-capability-without-a-consumer rule); a `linux-clang`
preset; a headless VOIDBORNE `--autodemo` on Linux; hosted matrix CI.
**Acceptance:** green CI on Windows AND Linux; VOIDBORNE `--autodemo` passes on Linux.

### P11 — The great prune II — ◑ mostly done
Fork 1 pruned (125 files). ✅ **okn-network's 46 placeholder stub TUs deleted** (symbol-neutral;
116/916 still green), and ✅ **`check_no_stub_tus.ps1 -Strict` is wired into the gate** as a
**baseline ratchet** — `scripts/stub_baseline.txt` freezes today's 283 known stubs (legit
header-first empties + the remaining halos) and the gate fails on any NEW stub, so the debt
can't grow back. **Remaining:** prune the bigger halos still in the baseline (okn-editor 92,
okn-audio 41, okn-script 36, okn-asset 31, okn-ui 31) over time + delete the committed
`okn-editor` Qt build artifacts; each prune shrinks the baseline.

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

### P15 — The north-star game — ◑ started (platformer promoted; Lua-authored levels)
The platformer is the chosen title; its levels are now authored + hot-reloaded from `okn-script`
Lua (`platformer_levels.lua`, sol2 in a confined `OKN_PLAT_HAS_LUA` TU; built-in fallback).
Next: title/settings (okn-ui + okn-input rebind + audio bus split), SceneImporter levels.
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
- **Bus factor / NAS single point of failure** — ◑ largely discharged: a public GitHub mirror
  (github.com/Teamkiller131/*) now carries the root + 12 of 13 okn-* submodules, and
  `.gitmodules`' relative urls make a GitHub clone self-contained. Remaining: okn-editor's
  mirror (blocked by its committed-Qt-DLL history × an unstable local proxy — push it from
  the NAS as a gitea push-mirror), and the fnos runner still bootstraps `act_runner` from
  the NAS.
- **Jolt cross-platform determinism is opt-in** → lockstep needs it on; state-sync/input-replay
  don't. Single-player unaffected.

---

## 11. Immediate next (actionable now)
1. ✅ **P14 audio — the spatializer math** landed (`okn-audio` `spatializer.hpp`):
   listener-relative pan + inverse distance attenuation, headless-tested. The remaining
   `ma_sound`/`ma_sound_group` wiring is windowed.
2. ◑ **P10 cheap slice** — ✅ the `D:/vcpkg` purge landed (CI workflow + 4 module CMakeLists
   → `$VCPKG_ROOT`; okn-platform's left, protected). The GLSL shader variants are deferred to
   land **with** the `SOKOL_GLCORE` backend (so they're exercised, not a placeholder) — the
   bigger P10 step that needs a GL/Linux runner.
3. ✅ **P11 honesty** — `check_no_stub_tus.ps1 -Strict` (baseline ratchet) is wired into the
   gate, and okn-network's 46 placeholder stubs are pruned. Remaining: prune the bigger halos
   in the baseline (okn-editor 92, okn-audio 41, …) + the `okn-editor` Qt artifacts over time.
4. ◑ **P15 kickoff (the big one)** — ✅ the platformer is now the north-star title and its
   levels are **authored + hot-reloaded from Lua** (`platformer_levels.lua` via a sol2 TU,
   `OKN_PLAT_HAS_LUA`), composing `okn-script` onto the existing CharacterController + `okn-input`
   + save/load stack. Proven headlessly: the autodemo is driven by the Lua level count (a
   1-level Lua file caps the run at `lvl=1` where the 3 built-in levels reach `lvl=3`), and a
   missing/invalid file falls back to the built-ins. Remaining: title screen → settings
   (okn-ui + okn-input rebind + the audio bus split) → SceneImporter-authored levels in-editor.

---

## 12. Untapped potential (code-grounded deep-dig, 2026-06-28)

Every item is anchored to real files and was adversarially verified; `[leverage/effort]` are the
post-verification numbers (effort S/M/L). The governing insight: **the project's own rule #2
("every capability needs a game consumer") is now the biggest backlog generator** — most of these
turn an already-built, already-tested island into a shipped feature, not new tech.

### A. The "capability with no consumer" wave — turn tested islands into shipped features
The highest-density theme: each is a real, gate-tested capability with **zero game consumers**.

- **TriggerVolume primitive** `[high/S]` — sensor Enter/**Stay/Exit** is fully built + tested
  (`physics_world_jolt.cpp:144-166`, `test_jolt.cpp:534-565`) but **no game sets `is_sensor`**;
  every game polls `drain_contacts()` for Enter only. A header-only `on_enter/stay/exit` helper
  unlocks checkpoints, damage zones, pickups, level-exit gates. *Consumer:* one platformer
  trigger. *Caveat:* the kinematic CharacterController isn't a contact body — the trigger needs a
  dynamic proxy or an overlap query for the player.
- **Make the audio bus mixer + ducking audible** `[high/M]` — `AudioMixer` is a complete bus-tree +
  ducking core with 13 passing tests but its `process()` output is never handed to miniaudio
  (`playback.cpp:57-101` routes each sound through its own `ma_sound`). Feed `process()` through
  **one** `ma_data_source`/`ma_sound`; submit per-sound PCM via `submit_samples()`. *Consumer:*
  harvest (26 play sites) with a Master▸Music▸SFX tree + `set_duck`. The single most-expected
  audio feature, math already proven.
- **mario3d positional audio** `[high/S]` — `SpatializerNode` math is tested
  (`spatializer.cpp:16-53`) but `playback.cpp:80` hard-sets `NO_SPATIALIZATION`; mario3d plays 6
  world-positioned events flat. Drop the flag, add `play_at(buf, vec3)` → `ma_sound_set_position`
  and `set_listener()` ← Camera3D. Keep `SpatializerNode` as the headless test oracle. *The
  spatializer's only 3D-game consumer.*
- **ECS Serializer as the one save path** `[high/M]` — `serialize.cpp` is a complete EKO1 World
  save/load **with entity-ref remapping**, tested, **zero game consumers**; meanwhile voidborne
  hand-writes 30+ rapidjson pairs (`main.cpp:1775-1828`) yet already runs crew on a real
  `crewWorld` it *ignores on save*, and platformer writes a raw 8-byte `.dat`. Route voidborne's
  crew through `Serializer::save_to_file`. *Caveat:* EKO1 is host-endian/same-build (dev saves);
  `CrewStats` is POD-safe, but crew text lives in a parallel side-table (needs a string table or
  JSON sidecar). Directly satisfies the P15 "progress persists" clause.
- **Finish the okn-input migration** `[medium/S]` — okn-input shipped to kill the copy-pasted
  `InputMap`, but mario (`mario_app.cpp:84-92`) + harvest (`:84-91`) still hand-roll it. Swap to
  `using InputMap = okn::input::ActionMap<Action>` (mechanical for mario; the call sites already
  match). Takes okn-input to 4-of-4 keyboard games and gives both **free rebindable+persistable
  keys** that feed the settings menu.
- **SimdVec4 first consumer** `[medium/M]` — a complete SSE4.1 path with a scalar fallback,
  **zero production consumers** (a standing-rule violation). Wire a Vec4-packed/SoA batch transform
  (the swarm bench in §D) with a scalar-equality + timing doctest. *Caveat:* a real win needs SoA,
  not wrapping the existing AoS `Vec3`.
- **okn-memory off its island** `[low/S→L]` — confirmed island (zero external includes). Cheapest
  honest wiring is a load-scoped arena behind the Deserializer's scratch buffers `[low/S]`. The
  hot-path version (per-frame query/scheduler scratch) is `[L]` and **conditional**: `LinearArena`
  doesn't derive `IAllocator` and isn't thread-safe, so it needs an `IAllocator` arena adapter +
  per-worker arenas, and only pays off if the §D swarm bench shows query-vector allocs are hot.

### B. One reflection to rule them all (the structural unlock)
- **A thin per-field descriptor layer** `[high/M]` — `ComponentInfo` carries no field
  offsets/names (`component.hpp:18-27`); `ecs_binding.hpp:9-12` says verbatim that a generic
  by-name field surface "would need per-field reflection the ECS deliberately doesn't carry." That
  one missing `{name,offset,type}` table per component is consumed **two ways at once**: (1) a
  generic, labeled **editor inspector** (today hard-codes 3 component types, `editor.cpp:319-353`);
  (2) **okn-script** generic `component.field` get/set instead of per-game `new_usertype`. (A third
  use — field-granular netcode delta — is *future*: the delta encoder is whole-entity today, so it
  needs a rewrite, not just the descriptor.) *Land it WITH a consumer (the inspector) in the same
  change.* The cheap pre-step: **wire the editor's fake `EcsBridge` onto a real World +
  ScriptingBridge** `[high/M]` — `ecs_bridge.cpp:96` returns literal `"{}"`; the live
  reflection + the working `CommandManager` undo already exist. Read-only generic discovery is
  reachable now; labeled editing waits on the descriptor. *This is the cheapest first real P16 step,
  independent of the viewport renderer.*

### C. New reach surface + distribution (the genuinely-new direction)
- **Browser/WASM build riding the P10 GLSL** `[high/M]` — the renderer's only backend-specific code
  is two HLSL shader pairs; **no GLSL exists**. The GLSL the roadmap already commits to writing for
  Linux/`SOKOL_GLCORE` is byte-identical to what sokol's `SOKOL_GLES3` (emscripten) needs. flappy is
  the ideal first target: canonical `sokol_main`, **zero file IO** (procedural audio via
  `make_beep`), no Jolt, audio via `ma_engine` (WebAudio under emscripten). A zero-install,
  link-shareable `flappy.html` is the single biggest distribution multiplier for a solo author —
  for the marginal cost of a GLES3 impl TU on top of planned P10 work. *"wasm/emscripten/browser"
  appears nowhere in v4.* (harvest is **not** a first target — it `stbi_load`s a PNG.)
- **Public Git mirror** `[high/S]` — discharges the §10 bus-factor risk (everything resolves only
  from one NAS, which flaked mid-session) **and** is the hard precondition for hosted Linux CI /
  any outside runner. *Honest caveat (verified):* it unblocks the precondition, not instant-green
  Linux game builds — okn-platform has never been built off Windows, and the runner currently
  bootstraps `act_runner` *from* the NAS, so the mirror must rehost that too.
- **CPack + `install()` rules** `[medium/M]` — `OKN_ENABLE_INSTALL` is OFF; no game/SDK has install
  rules and no CPack exists (an `install()` pattern already lives in `tools/CMakeLists.txt:54` to
  copy). The voidborne post-build data-staging is 90% of an `install()` rule; CPack ZIP is the lid.
  Turns "clone + build with vcvars" into "download + double-click" — the literal north-star clause.
- **Not worth it (rejected):** a sokol **Metal/macOS** "free rider" off the GLSL — false. sokol does
  not cross-compile hand-written shader source at runtime; Metal needs hand-authored MSL strings +
  Apple hardware to verify. No source reuse; defer until a Mac box/runner exists.

### D. Determinism + perf harness — made concrete (the §7 track that was vaporware)
- **State-hash determinism harness** `[high/M]` — the hard part already exists: `serialize.cpp`
  emits a **byte-deterministic** World snapshot (entities in index order, components sorted by type
  id — verified). But `okn-math/hash.cpp` is an empty include (`hash_combine` declared, never
  defined) and the network `recorder/replayer.hpp` are stubs. Implement `fnv1a` + `World::state_hash()
  = hash(Snapshot::capture())`; first consumer = a save/load hash-equality doctest in the gate. The
  one oracle reused by save/load, scheduler, physics, and future netcode.
- **10k-entity swarm stress demo = the ECS perf gate** `[high/M]` — nothing in `games/` exceeds
  ~1000 entities; the cached query hot path (`world.hpp:191-232`) was built for scale it never
  sees. A headless swarm (N boids: Position+Velocity under the parallel scheduler) at 10k–50k with
  a loose per-frame budget stress-tests sparse-set growth + cached query + the thread pool at once,
  and is **the benchmark §8 conditions chunked-iteration on** ("only if a benchmark demands it").
- **Scheduler speedup microbench** `[medium/S]` — the parallel test asserts only *correctness*; the
  claimed speedup is measured nowhere. Extend the existing 1000-entity disjoint-write fixture to
  time `set_job_system(&pool)` vs sequential; assert a loose floor gated on `worker_count()>=2` to
  stay non-flaky.
- **Record/replay** `[medium/M]` — capture the seeded fault/tick stream + per-tick `state_hash`,
  replay, assert identical. *Gated on a real feeder* (the netbox/voidborne state-sync below), else
  another island. *Note (verified):* voidborne is a **weak** determinism host — its sim mutates
  plain `g.*` structs, not the ECS, so `state_hash(crewWorld)` is ~constant; hash the `g.*` fields
  or prefer the swarm/save-load tests.

### E. Engine-as-product / DX (cheap consolidation, no new capability)
- **`okn_add_sokol_game()` CMake helper + `games/_template`** `[high/S]` — all 6 sokol games repeat
  the same ~10-line recipe (WIN32 gate, two render-TU paths, DPI manifest, `/WX-`) with **no
  helper anywhere**. One function makes the **WIN32→GL gate a single edit** (the precondition for
  the P10 backend swap) and "new game" a copy-one-dir op. *Needs an `EXTRA_SRC`/`RENDER_TU` hook*
  (mario3d uses `mesh_renderer.cpp`; platformer has the optional Lua block).
- **Promote the 3×5 bitmap font into okn-render** `[high/S]` — the identical `kFont[10][5]` glyph
  table is copy-pasted in 4 games (+ harvest's A–Z); the sprite path is header-only so a
  `bitmap_text.hpp` drops in with zero link changes. It also **unblocks `hud_bridge`'s skipped
  `kText`** — i.e. the precondition for any readable in-game okn-ui menu.
- **okn-ui's first consumer: a title/settings menu** `[medium/M]` — already a named P15 item, but
  the *actual blocker* is the **empty `renderer_bridge.hpp` stub** `[medium/M]`: implement the one
  adapter that walks `DrawCommand`s → SpriteBatch quads (`kRect`/`kImage` ≈ 1:1), then a
  `SettingsPanel` (audio sliders + `ActionMap::rebind` rows) closes okn-ui + audio-bus + input-
  persistence gaps in one screen. *Sequence after bitmap_text (`kText`) + note `focus_manager.cpp`
  is a 4-line stub, so tab-nav needs filling; mouse sliders work now.*
- **Editor authors EKO1** `[medium/M]` — the editor saves text Lua; okn-asset has a real binary
  EKO1 `SceneImporter` whose magic **matches** the okn-ecs Serializer (verified) — but the editor
  uses neither, and levels live in a *third* place (`platformer_levels.lua`). Emit EKO1 from the
  editor's live `SliceWorld` + load it in one game → "every P15 level authored in-editor" without
  the offscreen-sokol rewrite (which stays blocked on UniGUI not exposing its D3D11 device).
- **Per-game stb → `TextureImporter` (+ atlas for mario)** `[medium/S]` — 3 games re-`#define
  STB_IMAGE_IMPLEMENTATION`, duplicating the guarded `TextureImporter`. Gives the importer its
  first consumer; the atlas (3 draws→1) win applies **only to mario** (the others load one sprite).
  *Caveat:* `TextureData`→`Image` needs a per-pixel conversion (not a drop-in); the write-impl stays.
- **Project manifest + auto-atlas** `[medium/M]` — compose `PackWriter` + `TextureImporter` +
  `TextureAtlas` + `AssetRegistry` (all real, none composed) into the engine's first content
  pipeline, one game + textures only. *Avoid the name collision with the Win32 `.manifest` files.*
- **mesh3d textured material** `[medium/L]` — colored-boxes-only; borrow sprite2d's existing
  sampler setup to add UVs + a textured `draw_box(tex_id)`. First textured 3D. Worth it only
  because mario3d is a committed consumer (rule #1).

### F. Networking's first real consumer (sequence the unexploited stack)
The entire reliable-transport + snapshot/delta stack (116 gate cases) has **zero consumers**. The
cheapest path is **headless, loopback, server-authoritative — needs no determinism ADR** (state-sync
is unaffected by Jolt's opt-in determinism, per §10).
- **Lift `FaultyLink` into the public testkit** `[high/S]` — a complete deterministic in-memory
  loss/reorder `ITransport` pair is trapped in an anonymous namespace inside a test
  (`test_reliability.cpp:176-235`); the `testkit/recorder.hpp`/`replayer.hpp` homes are empty stubs.
  Lift it out + add a seed param to `FaultInjector` (clock-seeded today). The substrate P17's
  in-the-gate replay test depends on.
- **voidborne crewWorld / a `games/netbox` as the first state-sync consumer** `[high/M]` — snapshot
  `entities_with(CrewStats)` → `encode_delta` over a seeded `FaultyLink` → `apply_delta` on a 2nd
  World → assert byte-equality, as a headless `--netdemo` marker in the gate. A dedicated
  **`games/netbox`** (links only ecs+network+math, no sokol) is the cleaner first *networked game* —
  a genre the engine has zero of. *POD components only.* Runs **ahead of** the v4 P17 sequencing but
  satisfies rule #2 immediately and stays CI-safe.
- **`Session` is already the headless server** `[medium/M]` — `session.cpp:18-95` already
  binds/listens, accepts clients with per-client `ReliabilityLayer`, broadcasts, keepalives, and
  drops dead clients; `loopback_test.cpp` spins a real localhost TCP pair. Only a per-tick snapshot
  pump + a host `main()` are missing. *(The `okn-server-sdk` interface lib is a dead halo, but
  voidborne is **not** the cheap headless host the lens first assumed — it's a windowed UniGUI app
  whose `--autodemo` returns before the window loop; a true headless server wants netbox, not a
  de-ImGui'd voidborne.)*

---

## 13. Sequenced quick wins (dependency-ordered; do the S's first)

The cheap, high-leverage front of §12, ordered so prerequisites land first. Each is independently
gate-verifiable and respects rule #2 (names its consumer).

1. ✅ **TriggerVolume → platformer** (TriggerSet in okn-physics detection/; the goal flag is a
   sensor via a z-offset kinematic probe — CharacterVirtual ignores body masks, found by autodemo)
   · ✅ **mario3d positional audio** (play_at + AudioEngine::set_listener; hybrid third-person
   listener) · ✅ **finish okn-input (mario/harvest)** — okn-input is 4-of-4.
2. ✅ **`okn_add_sokol_game()` helper** (cmake/OknGame.cmake — the platform gate is now ONE edit
   for P10; games/_template still open) and ✅ **`bitmap_text.hpp`** (5 glyph tables deleted;
   `hud_bridge` `kText` unblock still open).
3. ◑ **Public Git mirror** — ✅ github.com/Teamkiller131/{OmniKillerNexus + 12 okn-*} live;
   `.gitmodules` now uses RELATIVE urls so one clone command resolves from either host
   (verified by a real recursive clone from GitHub — 13/14 submodules OK). **Gap: okn-editor**
   — its history carries ~125MB of committed Qt DLLs and the local network path to GitHub
   rides an unstable fake-IP proxy (198.18.x) that kills sustained uploads; every strategy
   (HTTPS/SSH, chunked, blob-seeded, negotiated, no-delta) died mid-pack. Fix from the NAS
   side: a gitea **push-mirror** for okn-editor (server-side push, no local proxy), or push
   once from a stable network; the P11 Qt-artifact prune would also shrink the problem.
   ✅ **`FaultyLink` lifted into the testkit** (public `loopback_link.hpp`; `FaultInjector`
   gains a reproducible seed, gate-tested) — the netcode/replay substrate is in place.
4. **State-hash harness** `[M]` (`hash.cpp` + `World::state_hash()` + a save/load doctest) → then
   **the 10k swarm perf gate** `[M]` → then **SimdVec4** wired into the swarm and the **scheduler
   speedup microbench** `[S]` ride on top.
5. **ECS Serializer as voidborne's save** `[M]` · **bus mixer audible in harvest** `[M]` · the
   **editor `EcsBridge`→real World** `[M]` — the three biggest "island → shipped" conversions.
6. **renderer_bridge** `[M]` → **platformer title/settings menu** `[M]` (the P15 acceptance item) →
   the **per-field descriptor layer** `[M]` generalizes the inspector that the menu work touches.
7. **`games/netbox` state-sync demo** `[M]` — the netcode's first consumer, once `FaultyLink` (3)
   and the state-hash oracle (4) exist. Then **record/replay** `[M]` has a real feeder.

**Browser/WASM** `[M]`, **CPack** `[M]`, **editor→EKO1** `[M]`, **manifest/auto-atlas** `[M]`, and
**mesh3d textures** `[L]` are the next tier — land them as their consumers arrive, not speculatively.
