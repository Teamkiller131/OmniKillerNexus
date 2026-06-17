# OmniKillerNexus — Long-Term Development Plan (v2, 2026-06-16)

> Supersedes the v1 roadmap (which lived in agent memory and is now largely
> **done**: P0 stabilize → P1 math+2D render → P2 close-the-slice → P3 Lua →
> editor P0–P4 → two demo games → physics contact events → ECS save/load →
> foundation-stub hardening). This document is the authoritative forward plan.
> Trust the build/test gate, not status labels.

---

## 1. Where we are (grounded, not aspirational)

The **2D vertical slice is closed end-to-end** and proven:

`input → ECS → Jolt physics (+ contact events) → 2D sprite render (software + sokol/D3D11 GPU) → audio (miniaudio + WAV + real bus mixer) → Lua scripting (sol2, hot-reload) → ImGui/UniGUI editor → native binary save/load`

Evidence: **two playable games** (Flappy Bird, and the physics game *Knockdown*) built _on_ the engine; a content editor with gizmos/inspector/assets/undo; ~**620+ unit tests** across 13 modules, full gate green; DPI-correct windowing.

But honesty matters: **everything is demo-grade thin**, and meaningful dead/fake
scaffolding remains. The engine can build small 2D games today; it cannot yet
*ship* a complete one without the work below.

### Known dead / fake code (prune or finish — see P9)
- `okn-ecs` archetype/chunk `Storage` — a second, unused ECS core (dead).
- `okn-render` native D3D12/Vulkan lib — duplicate-symbol stubs; unlinkable (the live path is the 2D sprite renderer, which bypasses it).
- `okn-network` QUIC/TCP/UDP transports — `connect()→true` no-ops; `UdpSocket` null-`impl_` bug. Only the transport-agnostic reliability core (~80%) is real.
- `okn-audio` mp3/flac/ogg decoders — return empty; platform backends are bool-flip stubs.
- `okn-asset` — no streaming / hot-reload / basisu.

---

## 2. Strategy (unchanged — validated by the Jolt/sokol/sol2 pivots)

1. **Buy undifferentiated infra, build only game glue.** Jolt, sokol_gfx/app, miniaudio, sol2, stb/assimp, Dear ImGui/UniGUI are the load-bearing third-party. We write the *glue and the game*, not another physics solver or D3D12 backend.
2. **Vertical slice over horizontal breadth.** 5 subsystems at 100% along one playable line beats 13 at 30%.
3. **Game-as-forcing-function.** The next game drives the next engine features — the way *Knockdown* drove contact events. Build the game; let it expose the gaps.
4. **Honesty as a feature.** Prune scaffolding that makes the tree look complete; gate every change with tests; keep docs matching reality.

---

## 3. North star

**Ship one small but _complete_ 2D game on the engine** — title screen → several
levels (authored in the editor) → win/lose → save/load progress → a settings
screen (audio + key bindings) — runnable on at least two platforms. _That game is
the acceptance test for the engine._ Everything in §4 exists to make it possible.

---

## 4. Phased plan (each phase = a milestone with explicit acceptance criteria)

### P5 — Content & persistence foundation
Turn the single-`slice_scene.lua` demo into a real project pipeline.
- Wire the **native binary scene save/load** (ECS serializer, just landed) into the editor *and* the runtime — load a `.oknscene` at startup, not just hot-reloaded Lua text.
- **Disk image loading + atlas packing**: stb is already linked; load real PNG sprites and pack a runtime atlas (games currently use procedural textures only).
- A **project/scene format**: multiple scenes, asset references by id, a project manifest.
- **Acceptance:** the editor saves and reloads a binary scene that survives a restart; a game loads its sprites from disk PNGs through an atlas.

### P6 — Gameplay systems for a real game
The primitives every 2D game needs, none of which exist yet.
- **2D character controller** on Jolt (grounded movement, jump, slope/step handling) — the platformer primitive.
- **Collision layers/masks** (the `collision_mask` field is currently ignored) + **trigger/sensor volumes** (extend the new contact-events path to non-solid overlaps).
- **Input action-mapping** system (bind actions → keys/buttons; replace per-game raw sokol key checks).
- **Audio in anger**: route SFX/music through the new bus mixer; add music **streaming**; 2D positional pan.
- **UI keyboard/text input** (`okn-ui` is mouse-only today) so menus and HUD text fields work.
- **Acceptance:** a controllable character moving through a level with trigger zones, fully remappable input, and a working pause menu.

### P7 — The forcing-function game (complete, small)
- Pick **one**: a 2D physics platformer **or** a physics puzzle game (leverages the Jolt strength already proven).
- Title → N editor-authored levels → win/lose → **save/load progress** → settings (audio/keys).
- This is where P5 + P6 get validated and the *next* gap list is generated.
- **Acceptance:** a person who has never seen the code can play it start-to-finish; progress persists across runs.

### P8 — Editor as the real content tool
- Render the engine's **own `GpuSpriteRenderer` into the editor viewport** (replace the `ImDrawList` rasterization workaround) — needs a UniGUI D3D11-device accessor patch (the DX11 renderer's `device_` is private upstream).
- **Tilemap / level editing**, play-in-editor, asset browser with real thumbnails.
- **Acceptance:** every level in the P7 game is authored entirely in-editor, WYSIWYG against the real renderer.

### P9 — Cross-platform, CI, and the great prune
- **sokol GL/Metal backends** so the engine isn't effectively Windows/D3D11-only; stand up a Linux and/or macOS build.
- Wire the existing `scripts/run_tests_all.ps1` gate to **gitea CI** (runner setup already documented in `docs/gitea_runner_setup.md`).
- Write a **determinism ADR before any netcode** (Jolt is _not_ cross-platform deterministic — this gates lockstep multiplayer forever).
- **Prune/finish the dead code** from §1: delete the archetype `Storage`, the dup-symbol D3D12/Vulkan lib, and the fake network transports; either delete or finish (dr_wav/dr_mp3) the empty audio decoders.
- **Acceptance:** green CI on ≥2 platforms; a fresh repo audit finds no "complete-looking" comment-only stubs.

---

## 5. Continuous tracks (run alongside the phases)
- **Honesty/pruning** — every phase removes more scaffolding than it adds.
- **Test coverage** — no feature lands without a test in the gate.
- **Docs-vs-reality** — keep `AGENTS.md` / dev guide accurate (e.g. `AGENTS.md:124` "everything is placeholder" is stale; the standard/naming authority lives in `docs/DECISIONS.md`).

---

## 6. Explicit non-goals (deferred — do not start these yet)
- **3D** rendering/physics, PBR/GI.
- **Networking** — finish the reliability core *only* once single-player ships **and** the determinism ADR is written.
- **Archetype ECS** — the sparse-set `World` is sufficient; don't resurrect the dead core.

---

## 7. Buy-vs-build ledger (current truth)

| Subsystem | Decision | Status |
|---|---|---|
| Physics | **Buy** Jolt | Real; +contact events. Gaps: char controller, triggers, layers, MT |
| 2D render | **Build** glue over **buy** sokol_gfx | Real (sw + GPU). Native D3D12/Vulkan lib = dead, prune |
| Windowing/input | **Buy** sokol_app | Real, DPI-aware. Needs action-mapping layer |
| Audio | **Buy** miniaudio | Playback + WAV + bus mixer real. Decoders/streaming TODO |
| Scripting | **Buy** sol2/Lua | Real, hot-reload. ScriptingBridge→ECS still stubbed |
| ECS | **Build** sparse-set World | Real + save/load. Archetype core = dead, prune |
| Assets | **Buy** stb/assimp | Importers guarded. Atlas/streaming/hot-reload TODO |
| UI | **Build** | Widgets+layout real. Mouse-only; needs keyboard/text |
| Editor | **Buy** Dear ImGui via UniGUI | P0–P4 done. Viewport uses ImDrawList, not engine renderer |
| Networking | **Build** reliability over asio | ~80% core; transports fake. Deferred |

---

## 8. Risks & watch-items
- **Jolt is not cross-platform deterministic** → blocks lockstep netcode; design around it (state sync, not lockstep) or accept single-player.
- **sokol pinned to the pre-2024 API**; **UniGUI** submodule coupling (`PROJECT_SOURCE_DIR` font-embed fix, implot 0.17 pin); **vcpkg baseline pinned** — upgrades are deliberate, not incidental.
- **HiDPI**: sokol windowed apps need the PerMonitorV2 manifest + `high_dpi` (already applied to the games/samples; bake this into any new windowed target).

---

## 9. Immediate next three (actionable now)
1. **P5 kickoff** — editor: replace "save to Lua text" with native `.oknscene` binary save/load using the new `okn-ecs` serializer (the capability exists; wire it).
2. **P5** — `okn-asset`: load a PNG from disk via stb and upload it through `GpuSpriteRenderer::upload_texture`; prove a disk sprite in a game.
3. **P6 spike** — a minimal 2D character controller on Jolt (move + jump + ground check) as a new `games/` prototype, to surface the physics/input gaps early.
