# OmniKillerNexus — Games

The `games/` tree holds **complete, playable demo games built on the engine**.
They are the engine's real acceptance tests: each one is a *forcing function*
that drove (or stresses) specific engine features. Together they prove the
render / input / audio stack is genre-agnostic and that physics is opt-in.

> Every game is registered in the root `CMakeLists.txt` (`add_subdirectory(games/<name>)`)
> and builds to `build-phys/bin/<name>.exe`. See [BUILD.md](BUILD.md) to build
> and run them.

| Game | Genre | Engine paths it exercises | Physics |
|---|---|---|---|
| **flappy** | arcade | 2D GPU sprite path, audio (procedural WAV) | none (plain C++) |
| **knockdown** | physics puzzle | Jolt **contact events**, 2D GPU path, runtime textures | Jolt |
| **platformer** | 2D platformer | 2D char controller, input action-map, **disk PNG** sprites, binary save | Jolt |
| **mario** | side-scroller | **multi-texture** batching, contact-event gameplay (stomp/hurt) | Jolt |
| **mario3d** | 3D platformer | the **3D mesh path**, joints, kinematic platforms, orbit camera | Jolt (3D) |
| **harvest** | grid farming / RPG-sim | 2D sprite path **with no physics**, free-move + AABB tiles, deep sim systems | none (pure AABB) |
| **voidborne** | management sim | **UniGUI / Dear ImGui** draw-list world + panels, rapidjson data | none |

---

## flappy — "Flappy Bird"
`games/flappy/` — the first proof the engine builds a real game. Uses the **2D
GPU path** (`GpuSpriteRenderer` + `SpriteBatch` + `Camera2D`, plus a 3×5-pixel
bitmap font for the score) and **okn-audio** (procedural WAV flap/score/hit
beeps), in a sokol_app D3D11 window. Gameplay (gravity / scroll / AABB collision
/ score) is **plain C++** — Flappy needs no physics engine. Establishes the new-
game template: link `okn-math` + `okn-audio`, reuse the GPU renderer + sokol TU,
work in +Y-up world units with a `Camera2D`, embed the PerMonitorV2 DPI manifest.

## knockdown — slingshot physics
`games/knockdown/` — an Angry-Birds-style game that **showcases the contact-
events API**. Drag a Jolt dynamic sphere into a 6-crate pyramid (dynamic boxes,
Jolt stacking); `drain_contacts()` each frame → a debounced impact thump (gated
by a settle timer so the resting tower is quiet). Score = crates displaced from
spawn; crates render rotated via `get_body(id)->rotation.angle_z()`; the ball is
a procedural circle `Image` uploaded with `GpuSpriteRenderer::upload_texture`.
Links `okn-math` + `okn-audio` + `okn-physics`.

## platformer — character controller + disk assets
`games/platformer/` — forces three roadmap phases at once. A Jolt **2D character
controller** (velocity move + jump gated on a downward ground-ray, rotation
locked upright each frame), an **input action-map** (actions → keys, held/edge),
a sprite that lives **on disk** (written as RGBA **PNG** via `stb_image_write`,
loaded back via `stb_image` — BMP would lose alpha), a **binary progress save**,
and 3 levels + flagpole goal with fall-respawn. Headless check:
`OKN_PLAT_AUTODEMO` traces the trajectory (clears L1, advances to L2, writes
`save.dat`). The game includes stb itself; it does **not** link okn-asset.

## mario — "Super OmniKiller" (2D)
`games/mario/` — the broadest single 2D test. **Multi-texture** disk sprites
(mario/coin/goomba PNGs as tex 1/2/3 → proves multi-texture batching). The
headline is the **contact-events API**: a `body→Kind` map decides STOMP (player
above goomba → destroy + bounce + score) vs HURT (else → lose a life + i-frames
+ knockback) from each `drain_contacts()` pair. Adds Super-Mario feel — variable
jump (jump-cut), crouch, crouch-jump, and a **multi-ray ground check** (3 down-
rays so standing on a ledge edge still lets you jump). Headless:
`OKN_MARIO_AUTODEMO` + a `mario_result.txt` win marker.

## mario3d — "Super OmniKiller 3D"
`games/mario3d/` — the engine's first **3D** game, on the **3D mesh path**
(`Camera3D` + `MeshRenderer`). Jolt in full 3D (X-Z move + Y jump), a controllable
**orbit camera** with **camera-relative** WASD, voxel models built from many lit
boxes (Mario/goombas/coins), bounce-pad **springs**, a **kinematic swinging
platform** and a **chained suspension bridge** (Jolt `kHinge` joints + a manual
rider damp/clamp so the solver can't fling you). Headless: `OKN_MARIO3D_*` env
hooks + a `mario3d_result.txt` win marker. This module is also where the 3D
renderer (`okn-render/mesh3d/`) and Jolt joints/kinematic bodies were proven.

## harvest — "OmniHarvest"
`games/harvest/` — a Stardew-Valley-like top-down farming sim that deliberately
carries a **whole game with NO physics**: it links **only `okn-math` +
`okn-audio` + the 2D GPU sprite path**. Free continuous movement with axis-
separated **AABB-vs-tilemap** collision (the engine's top-down free-move
pattern — no Jolt). On top of the grid farm sits a deep RPG/sim layer (a pure
`harvest_systems.hpp` data model): inventory, economy/shop with a per-day
market, skills/XP/perks, 12 achievements, NPC relationships/gifting, energy/
stamina gating, an A–Z bitmap font, a one-panel modal UI. Headless:
`OKN_HARVEST_AUTODEMO` drives the entire RPG loop through the real action
functions (zero synthetic keys) → `harvest_result.txt`; `OKN_HARVEST_ACHDEMO`
force-feeds counters to exercise all 12 achievement predicates.

## voidborne — "孤舟 · 青鸟号"
`games/voidborne/` — a generation-ship management sim **ported from a Godot 4.6/
C# original** onto the engine. Unlike the others, it runs on **TeamkillerUniGUI
/ Dear ImGui**: *both* the management panels **and** the 2D ship world render
through ImGui draw lists (a pixel-art renderer over `GetBackgroundDrawList`),
with the original JSON content loaded via **rapidjson**. Content: a 6-deck
explorable spaceship + elevator, an ecology bay, a 61-event modal system, crew,
four departments, a star-map / elections / captain track, a hidden "Void Seed"
darkline, 7 endings, and save/load. The renderer received a **6-phase visual
overhaul** — walls with height (2.5D faces + autotiled corners), animated airlock
doors + crew collision, organic non-square deck layouts, a detail & lighting pass
(reactor core, grow-lights, hull-window starfields), richer crew (variety /
facing / walk / wandering), and atmosphere (vignette / dust / fades). Headless:
`--selftest` (data load) and `--autodemo` (drives every system → "VOIDBORNE
M0-M7 OK"); `OKN_VB_SHOW=<bay|event|...|reactor|doors>` parks the game in a state
for a screenshot. See the "build a game on UniGUI" recipe below.

---

## The reusable game patterns

**Sokol/sprite game** (flappy, knockdown, platformer, mario, mario3d, harvest):
- CMake links `okn-math` + `okn-audio` (+ `okn-physics` if it uses Jolt) and
  **reuses `okn-render`'s `gpu/gpu_sprite_renderer.cpp` + `gpu/sokol_impl_app.cpp`
  by path** (these wrap sokol; you don't relink the dead okn-render lib).
- Games that load disk sprites compile **stb themselves**
  (`STB_IMAGE_IMPLEMENTATION` + `_WRITE_`); they do not link okn-asset.
- Work in **+Y-up** world units with a `Camera2D`; `WIN32` target; embed a
  **PerMonitorV2 `.manifest`** + `high_dpi = true` (sokol windowed apps render
  into a fraction of the client without it — [DPI gotcha](ROADMAP.md#8-risks--watch-items)).

**UniGUI app** (voidborne, okn-editor): link `unigui::unigui`, build only inside
the root build (needs the submodule), use `unigui::AppConfig` + `Init`/`Run`, draw
the world on `ImGui::GetBackgroundDrawList()`, and load the pixel font *after*
`Init`. Include `<unigui/app/app.h>` + `<imgui.h>` (not the `<unigui/unigui.h>`
umbrella — it pulls windows.h + GLFW).

**Headless verification (the rule that makes all of the above testable):**
factor **every player verb as a standalone callable function** (not inline in the
key handler). Then an env-gated `--autodemo` / `OKN_*_AUTODEMO` can drive the
*real* game logic with **zero synthetic keyboard input** and write a **result
marker file** (`<game>_result.txt`, e.g. `"OKHARVEST WIN gold=285 ..."`). Assert
on the marker, not on a polled trace (the win frame often falls between samples).
This is how every game above is CI-checkable without a display.
