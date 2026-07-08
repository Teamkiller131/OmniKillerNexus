# OmniKillerNexus — Long-Term Development Plan (v6, 2026-07-08)

**Thesis: v5's job is done — now ship the star.** v5's insight was that most built capability
had no consumer; its §13 executed completely (~24 gate-verified deliverables) and that vein is
mined out. v6's two-front thesis: **(A) the second platform** — Linux CI on the public mirror,
then widening the one-line platform gate — and **(B) the finishable game** — the content/polish
pass that turns the north-star acceptance ("a stranger downloads and FINISHES it") from
aspiration into fact. Supporting fronts: honesty consolidation (the measured great prune III +
ONE status surface instead of four), the editor-as-level-tool arc, the netcode ladder to a 1v1,
and closing the perf track on corrected numbers.

v6 supersedes v5 (2026-06-28). It was produced the same way: a multi-agent code-grounded
survey (6 lenses × mine→adversarially-verify) whose every claim cites the tree; corrected
facts below carry their evidence. **Structural change: v5 kept four independently-drifting
status surfaces (§2 framing, §6 phases, §9 ledger, §13 markers) — within days of each other
they contradicted the tree AND each other. v6 keeps ONE scorecard (§2) and ONE sequenced
plan (§4–§5); when something lands, exactly one row changes.**

---

## 1. Where the engine actually is (post-v5, verified)

**The gate** (`scripts/run_tests_all.ps1`, all green): 17 module suites (~877 TEST_CASEs) +
**8 game compile targets** (flappy, knockdown, platformer, **platformer-gl**, mario, mario3d,
harvest, voidborne) + **three headless behavioral games** — voidborne (selftest + autodemo
M0-M7 + crewSave), swarm (20k-entity ECS scale + scheduling-independent state hashes), netbox
(state-sync under loss + bit-exact record/replay) — + CPack bundle assertion + editor selftest
(serialize, undo, reflection, EKO1 round-trip) + the stub-guard `-Strict` ratchet (284 baselined).

**What v5's execution changed, in one paragraph:** every former capability-island now has a
gate-verified consumer — sensors/TriggerVolume (platformer goal), positional audio (mario3d),
the audible bus mixer `MixerPlayback` (harvest Master▸Music▸SFX + ducking), the ECS Serializer
(voidborne crew save + live `crew.<id>.<field>` event effects), okn-ui (the platformer's
title/settings menu with persisted volume + key rebind, drawn via `hud_bridge` kText +
`bitmap_text`), okn-input (4-of-4 keyboard games + the rebind UI), ScriptingBridge reflection
(generic editor inspector + per-field descriptors + Lua `get_field`/`set_field`), the netcode
stack (netbox: Snapshot deltas over `ReliabilityLayer` on the testkit `FaultyLink`, verified by
blob + `state_hash` equality, `replay=1`), the GLSL sprite path (platformer-gl's full autodemo
on desktop OpenGL), CPack download-and-play ZIPs (proven by the extract-and-run stranger test),
and EKO1 scene authoring (editor Save → SceneImporter → a fresh world that simulates).

**Distribution:** all 14 repos mirror to `github.com/Teamkiller131/*` (recursive clone
resolves; relative `.gitmodules`); gitea on the NAS stays primary. GitHub **Actions is proven
to run** on the mirror (the okn-editor bundle bootstrap) but **zero standing workflows exist**
— and the root repo's `.github/workflows/quick-check.yml` actively spawns doomed runs on every
mirror push (`runs-on: fnos-shell`, no `if: false`). *Fix immediately (§5.1).*

---

## 2. Scorecard (the ONE status surface — every row cites the gate or a file)

| Module | State | Suite | Game consumer(s) | Halo* | Notes |
|---|---|---|---|---|---|
| okn-core | solid | 31 | all (transitively) | 0 | **WASM blocker**: `platform.hpp` `#error` on unknown platform + 64-bit `static_assert` — no `__EMSCRIPTEN__` branch |
| okn-math | solid | 139 | all | 17 | `SimdVec4` still zero consumers (disposition due, §4F); fnv1a/state-hash oracle live |
| okn-memory | **island** | 116 | none | 11 | unchanged: zero external includes — wire-or-scope decision still open |
| okn-platform | solid | 113 | scheduler/swarm | 11 | Chase-Lev steal is REAL (task_queue.cpp:83-102); Linux impls (mmap/dlopen/signal) real but **never compiled** — 131 unexercised branch sites |
| okn-ecs | solid | 61 | voidborne, swarm, netbox | 9 | Serializer+refs, state_hash, reflection+FieldDescs, snapshot/Rollback (ring still untested) |
| okn-asset | solid | 117 | voidborne, editor (EKO1) | 31 | SceneImporter consumed; still NO project manifest, no auto-atlas, no streaming |
| okn-audio | solid | 57 | harvest (mixer), mario3d (3D), all (SFX) | 41 | bus tree + duck + positional AUDIBLE; mp3 decode real (dr_mp3); **no streamed BGM anywhere** (`MA_SOUND_FLAG_STREAM`: 0 hits) |
| okn-script | solid | 17 | platformer (levels), voidborne-adjacent | 36 | bind_ecs + hot-reload + generic get/set_field; no game runs LOGIC in Lua yet |
| okn-physics | reference | 24 | platformer, knockdown, mario, mario3d | 1 | CharacterController, layers, sensors+TriggerSet, joints; single-threaded |
| okn-input | solid | 8 | 4-of-4 keyboard games | 0 | ActionMap + rebind UI + persistence |
| okn-network | solid | 120 | **netbox** | **0** | reliability+RTO+AIMD+session+snapshot/delta+testkit(FaultyLink/recorder/replayer); QUIC honest stub; **no input msg type, Session can't address clients** (§4E) |
| okn-ui | solid | 29 | platformer (menu) | 31 | widgets+layout+keyboard/text+router; focus_manager still a stub (no tab-nav) |
| okn-render 2D | solid | 15+3+8+3 | 6 games | 4 | sw ref + GPU; **HLSL/GLSL-330/GLES3 selected by backend**; atlas+TextureAtlas+bitmap_text+hud_bridge(kText) |
| okn-render 3D | partial | 16 | mario3d | — | mesh3d **HLSL-only**; single Lambert light, no textures |
| okn-editor | growing | selftest | (is the tool) | **92** | UniGUI shell; reflection inspector; EKO1 authoring; ImDrawList viewport (real content, not a device target); **dead Qt path + 143MB committed build/ DLLs** (§4C) |

\* Halo = entries in `scripts/stub_baseline.txt` (284 total; measured: **221 of 284 are pure
halo** — empty .cpp with empty/absent header pairs).

**Corrected findings the old docs got wrong** (each was verified against the tree):
audio IS audible (only streamed BGM remains of P14); okn-ui HAS a game menu consumer; the
serializer/scheduler HAVE consumers; okn-network has ZERO baseline stubs (not "~45 left");
the editor viewport DOES render (ImDrawList over real SpriteBatch); the work-stealing deque
is NOT fake; okn-ui is NOT mouse-only; mp3 decode is NOT empty; P16 is STARTED (inspector +
EKO1); the 2D renderer is NOT D3D11-only. Still true: okn-memory island, SimdVec4 unconsumed,
no asset manifest, mesh3d HLSL-only, QUIC stub, determinism ADR missing.

**The perf reversal (§4F):** the gate's recorded 9.44ms/20k-entities is a **Debug** number.
A Release build in-tree measures **~0.69ms** — and a *worse* parallel ratio (0.73x). Profiling
attribution: ~90% of even the Release frame is engine-controlled overhead (a fresh 20k-entry
matched vector allocated per query per frame; sparse-indirect `get()` per component per
entity). 20k entities ≈ 1000× the biggest real game (voidborne: 20 crew). The §8 "benchmark
demands it" clause triggered — and what it demands first is *correct numbers*, not chunking.

---

## 3. North star (unchanged) + what's actually left

> One complete game composing the FULL stack — title → authored levels → win/lose →
> save/load → settings — built and distributable on Windows AND Linux, that a stranger
> downloads and FINISHES.

Verified distance: **Windows-side, the machinery exists end-to-end** (menu/settings/rebind ✓,
Lua levels ✓, save ✓, CPack ZIP proven by extract-and-run ✓). What's left is honest content
("finishes" — §4B) and the second platform (§4A). The Linux half is no longer a research
problem: the headless spine (netbox/swarm/network/ecs/asset/ui/script sources) has **zero**
`_WIN32`/`windows.h`; UniGUI already builds green on ubuntu-latest in its own Actions (with a
copy-pasteable apt list); voidborne has NO WIN32 gate; the game gate is ONE line
(`cmake/OknGame.cmake:39`); the enumerated hazards are shallow (okn-audio's unguarded MSVC
pragmas under `-Werror`; `renderer.hpp`'s unguarded `<d3d12.h>` include chain; the gate
script's vcvars/`D:/vcpkg`/`.exe` Windows-isms).

---

## 4. The v6 arcs

### A. The second platform (P10 close-out) — Linux CI on the mirror
Ladder, each rung gate-verifiable:
1. **`linux.yml` headless spine** `[L]` — ubuntu-latest on the mirror: vcpkg (GHA binary
   cache), build okn-core→platform→ecs→network(+asset/ui/script), run the suites + **swarm +
   netbox + voidborne/editor selftests** (all headless-safe; swarm's budget is a loose
   tripwire by design). First-ever off-Windows compile of okn-platform's 131 branch sites —
   expect the enumerated shallow breaks (audio pragmas, d3d12 include) and fix them as found.
2. **Widen the ONE game gate** `[M]` — `WIN32` → `WIN32 OR LINUX` + the X11/GL link glue in
   `okn_add_sokol_game`; platformer-gl builds as an ELF and runs its **full autodemo under
   xvfb** (maxLvl=3 + MENU OK markers already exist for this).
3. **windows-latest gate job** `[M]` — the full `run_tests_all.ps1` on hosted Windows (needs
   the script's portability fixes: vcvars discovery, `VCPKG_INSTALLATION_ROOT`, exe naming).
   Kills the single-NAS-runner bus factor.
4. **Linux bundle** `[S]` — the CPack component on the Linux job; the stranger test, on the
   second platform.

### B. The finishable game (the star's content half)
- **Platformer finishing kit** `[L]` — the honest assessment: today it's a 2-minute tech demo
  (3 levels, no fail state, a win "screen" of 3 yellow rects, 2 beeps). The kit: level schema
  v2 (kill zones, collectibles in `platformer_lua`), ~6 more levels with a difficulty curve,
  death counter + timer HUD, a real ending screen (stats + Continue from `g.best`), streamed
  music bed + Music/SFX split driven from the existing settings screen (`MA_SOUND_FLAG_STREAM`
  — the last P14 tail), save moved onto the Serializer/EKO path. Autodemo extended per feature.
- **voidborne finish-quality** `[S]` — the only game a stranger could finish TODAY (7 endings,
  bilingual, title/save/settings) has **no sound**. Ambient loop + day-tick/event/harvest/ending
  SFX on Master/Music/SFX buses + a volume slider in its existing settings, autodemo-asserted.
  Keep it the behavior-gated flagship alongside the platformer.

### C. Honesty III (the measured prune) + one status surface
- **Stage 1: the editor purge** `[M]` — delete the dead uncompiled Qt world (`editor_app_qt.cpp`,
  `editor_engine.cpp`, the fake `ecs_bridge.cpp` returning `"{}"`, QML, ~40 uncompiled src/
  subdirectories), `git rm -r` the **143MB** of committed Qt build artifacts + `.gitignore` it.
  Baseline 284 → ~192. (This is also mirror hygiene — those DLLs are why okn-editor's mirror
  needed a bundle bootstrap.) Split the 924-line single-TU `editor.cpp` into 3-4 files while
  the tree is open.
- **Stage 2: the empty-pair halos** `[M]` — okn-audio 38/41, okn-script 35/36, okn-ui 29/31,
  okn-asset 27/31 measured deletable → baseline lands ≈ 63 legit header-first entries.
- **Docs consolidation** `[S]` — this document's §2 is now the only scorecard; ARCHITECTURE's
  stale rows corrected (done with v6); the gate script's own stale comments fixed.

### D. Editor as the level tool (P16, replanned on evidence)
1. **The level bridge** `[M]` — the P16 acceptance is about *platformer* levels, but the editor
   authors *SliceWorld* scenes — different documents. Bridge: `GoalTag`/`SpawnPoint` markers +
   a `platformer_levels.lua` exporter; selftest authors a level via editor ops, exports, parses
   back with `plat::load_levels_from_lua`, asserts value-identity. (The game side is DONE — it
   hot-reloads the file by mtime.)
2. **plat_sim extraction** `[L]` — the editor's Play steps SliceWorld physics, which cannot
   reproduce the game's kinematic controller. Extract the platformer's sim into a `plat_sim`
   lib consumed by the game (gate unchanged) → play-in-editor becomes honest.
3. **Offscreen sokol viewport** `[M]` — **the v5 blocker is stale**: `DX11Renderer`'s device
   members are already public; only a ~10-line app-layer accessor is missing, in UniGUI, which
   the owner controls. First milestone is engine-side and headless: offscreen D3D11 device →
   `sg_setup` with injected device → GpuSpriteRenderer draws into an offscreen target →
   readback asserted in the selftest. The UniGUI patch + pin bump follows.

### E. Netcode (P17, un-gated by evidence)
The ladder, in order:
1. **The determinism ADR** `[S]` — it now documents *proven facts*: server-authoritative
   state-sync under loss (gate), same-build bit-exact replay (gate), lockstep + cross-platform
   determinism explicitly out (JPH flag stays off), interpolation-only for the 1v1 (no
   prediction/lag-comp), and the **measured scale limits** with named upgrade triggers
   (apply_delta is O(changed×n) sorted-insert; reliable-ordered streaming has head-of-line
   blocking; ~24KB/tick at 1000 moving entities — LAN-fine; the `base_tick` wire field is the
   hook for the Quake-style unreliable-delta upgrade).
2. **netbox-duplex** `[S]` — client input → server (an `InputCommand{tick, buttons}` message;
   the transport is already symmetric), server applies at tick boundaries (not recv-time —
   the determinism hazard), replicates back; input-journal replay asserted in the gate
   (`NETBOX DUPLEX OK`).
3. **Session identity + 2-process TCP** `[M]` — `send_to`/`recv_from` with client ids (today
   send() broadcasts and recv() is anonymous — a 1v1 cannot exist); then the gate spawns
   server + client as separate processes on real loopback TCP.
4. **The 1v1** `[L]` — pong/box-pusher on the duplex stack + a headless interpolation-buffer
   test (buffer K ticks, query fractional render time, never extrapolate), then the windowed
   client on the platformer's shell with `--autodemo`. This IS the P17 acceptance.

### F. Perf close-out (measure, correct, then decide)
1. **Release perf lane + phase decomposition** `[S]` — the gate's swarm line sourced from a
   Release build; three phase timers (query build / view iterate / raw-array ceiling). The
   recorded finding gets corrected (Debug 9.44 → Release ~0.69ms, ratio 0.73x, overhead-dominated).
2. **Only if the phases demand it** `[M]` — cached/reusable Views (a World structural-version
   counter) + a dense-walk fast path for n==1 queries; then `par_for_each` (chunked
   intra-system, joined on a `std::latch` — NOT pool `wait_all`, a real deadlock hazard) gated
   at the scale where it pays.
3. **SimdVec4 disposition** `[S]` — name its consumer or scope the claim; the standing-rule
   violation doesn't get to live in the tree indefinitely.

### G. Reach (opportunistic)
- **flappy.html / WASM** `[M]` — the GLES3 shaders are staged and shared-bodied with the
  exercised GL330 path; flappy is the verified ideal target (no file IO, no Jolt, WebAudio-safe
  procedural audio). Blockers: emsdk (not installed) + exactly two lines in okn-core's
  `platform.hpp` (`#error` + the 64-bit `static_assert`). Publish via GitHub Pages on the mirror.
- **mesh3d GL** `[M]` — GLSL for the box shader unlocks mario3d off-Windows; only worth it
  with arc-A rung 2 landed.

---

## 5. Sequenced next (S-first, each names its verification)

1. ✅ *(with v6)* **Kill the mirror-hazard workflow** — `quick-check.yml` gets `if: false` (it
   targets the gitea runner label; on GitHub it spawns a doomed run per push). Docs corrected.
2. **Determinism ADR** `[S]` — §4E.1; a document, but one that un-gates P17 formally.
3. **netbox-duplex** `[S]` — §4E.2; `NETBOX DUPLEX OK` in the gate.
4. **Release perf lane** `[S]` — §4F.1; the corrected swarm line in the gate.
5. **voidborne sound** `[S]` — §4B; autodemo asserts the mixer path.
6. **Prune III stage 1 (editor)** `[M]` — §4C; baseline ≤ 192, gate green, mirror slimmed.
7. **linux.yml headless spine** `[L]` — §4A.1; the first off-Windows green.
8. **Platformer finishing kit** `[L]` — §4B; the star's content half, feature-by-feature.
9. **Editor level bridge** `[M]` — §4D.1; then plat_sim, then the offscreen viewport.
10. **Session identity → 1v1** `[M→L]` — §4E.3-4, after duplex + the ADR.

---

## 6. Non-goals (carried + new)
PBR/GI/shadow-heavy 3D · native D3D12/Vulkan/Metal (sokol IS the renderer; the dead D3D12
experiment stays labeled) · lockstep + cross-platform bit-determinism (ADR: out) · client
prediction/lag-compensation for the 1v1 (interpolation only) · interest management before its
ADR-named trigger · archetype ECS resurrection · chunked iteration before §4F.1's numbers
demand it · mobile/console · SDK install/export until modules are EXPORT-tagged
(`OKN_ENABLE_INSTALL` stays OFF; `OKN_PACKAGE_GAMES` is the shipping path).

## 7. Risks & watch-items
- **The proxy/NAS**: GitHub traffic rides an unstable fake-IP proxy that kills sustained
  uploads and long responses (it killed 9 survey agents while *writing this plan*); gitea
  flakes intermittently. The mirror + hosted CI (§4A) is the mitigation; keep pushes small.
- **okn-platform's Linux branches have never compiled** — treat rung A.1 as discovery, not
  checkbox; budget for unknown unknowns beyond the enumerated hazards.
- **Two flagship games** (platformer star-carrier + voidborne flagship) split the content
  budget — voidborne's `[S]` sound pass is cheap, but don't let B's platformer kit sprawl.
- **The editor purge touches a submodule with pre-existing local edits discipline** — stage
  paths explicitly, never `git add -A` (okn-platform/okn-ui carry protected local edits).
- **Windowed verification ceiling** shrinks but remains: the offscreen-viewport milestone
  (D.3) is deliberately headless-first to keep it in the gate.
