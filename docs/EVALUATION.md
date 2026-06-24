# OmniKillerNexus — Evaluation & Remediation Plan

> **Snapshot, 2026-06-23.** Produced by a multi-agent code audit (six assessors
> reading the actual source across architecture, engineering, and games/process,
> plus three peer-comparison researchers) followed by an **adversarial
> verification pass on every claim** — one strength was refuted and several were
> tempered; those corrections are folded in here. It is deliberately honest, in
> keeping with the project's own ethos. For the authoritative module state see
> [ARCHITECTURE.md](ARCHITECTURE.md); for the forward feature plan see
> [ROADMAP.md](ROADMAP.md); for the decisions behind the strategy see
> [DECISIONS.md](DECISIONS.md). **Part A** is the assessment; **Part B** is a
> prioritized plan to close the weaknesses.

---

# Re-evaluation — 2026-06-23 (post Phase A–D)

> A second multi-agent audit after the Phase A–D build-out. Assessors **re-ran
> every test binary**, ran the suites with `--no-skip`, read the CMakeLists/CI
> config, and inspected the save file; claims went through an adversarial
> verification pass (**8 confirmed, 1 tempered, 0 refuted**). This section
> supersedes the snapshot below where they differ. Part A/B are retained as the
> pre-build-out baseline.

**Headline:** the build-out is *real but inward-facing*. The leaf modules got
genuinely more real and the code got more honest; the two highest-leverage
*structural* risks — a no-op CI and a north-star game that doesn't touch the
engine — are unchanged. OKN remains a single-author learning/portfolio/reference
artifact whose real product is engineering discipline, **not** a shipping tool.

## Verified strengths (re-checked against code + live runs)

- **The 15-suite gate is genuinely green** — independently re-run: **800 cases /
  6109 assertions, all `rc=0`**, per-suite counts matching the docs to within
  rounding. The docs are not inflating the gate.
- **The Chase-Lev deque is correct**, not a label — faithful fence-based
  Lê/Pop/Cohen/Nardelli (PPoPP 2013) ordering, `atomic<Task*>` payloads, owner-only
  push honoured by the pool (injector + owner-side refill + drain-on-destroy),
  backed by a 50k-op adversarial stress.
- **Audio decoders verified on real fixtures** (committed ffmpeg-generated
  mp3/flac/ogg), correct memory ownership; **live UDP/TCP + reliability over real
  sockets**; **QUIC honestly reports unavailable** (tested, not faked).
- **VOIDBORNE ships a complete game shell** end-to-end (verified live — the save
  JSON literally contains the rebound keys), and **autodemo passes** (`M0–M7 OK`).
- **Buy-vs-build choices are the right industry picks** — Jolt, sokol, miniaudio,
  sol2/Lua are exactly what a knowledgeable dev reaches for at this scope, and the
  code now backs the docs.

## Verified weaknesses (sharpest first)

1. **CI is still effectively a no-op (critical).** `.github/full-build.yml` has
   `if: false` on every compiling job; the only push gate is a self-skipping
   clang-format lint; the gitea gate runs **module suites only** (no games, no
   editor) and its step still says "13 module suites" though the gate is 15. A
   commit that breaks a game/editor compile or regresses an autodemo marker passes
   every automated check. "All 15 green" is a **manual/local** claim. **◑ Partially
   addressed (2026-06-23):** the gate (`run_tests_all.ps1`) now **compile-gates all
   7 games** and asserts **VOIDBORNE's `--selftest`/`--autodemo` markers**, and the
   gitea CI label was corrected (15 suites + games). The `.github/` workflows are
   vestigial (the repo is gitea-hosted) and are now banner-marked inactive. Residual
   gap is infra-only: whether the self-hosted gitea runner actually fires per push.
2. **The north-star game bypasses the engine (critical).** `games/voidborne`
   links **only `unigui::unigui`** — zero okn-* modules. Phase D added a real
   *shell*, but the showcase still showcases a bought ImGui toolkit, not OKN's
   core. **No game (of 7) links okn-ecs / okn-script / okn-ui / okn-network**, so
   the freshly-finished features have **zero game consumers**. **◑ Addressed where
   it genuinely fits (2026-06-23):** VOIDBORNE now links and *uses* **okn-ecs** (the
   20 crew are real ECS entities + `CrewStats` components; the captain election
   queries the crew `World`), **okn-asset** (all data files load through `AssetIO`;
   `events.json` is live-hot-reloaded via the mtime watcher), and **okn-math** (the
   smoothed camera is an engine `Vec2`) — plus okn-core/okn-platform transitively.
   The gate builds it + asserts its autodemo (which drives the ECS election), so
   the flagship runs on the engine core in CI. Deliberately **not** forced:
   okn-render (ImGui *is* its renderer), okn-physics (tile-grid, not rigid bodies),
   okn-ui (ImGui *is* the UI), okn-network (single-player) — wiring those would be
   the cosmetic theater this finding warns against.
3. **"All green" has an asterisk — `doctest::skip()` hides real failures.** Run
   with `--no-skip`, okn-memory shows **1 failed case / 2 failed assertions**
   (`StackArena - overflow`: an 8-byte per-push header means 64 bytes never fit a
   64-byte arena; `LinearArena - basic` also skipped). The skips were introduced
   by a commit titled *"resolve … doctest test failures"* — i.e. **skipped, not
   fixed** — and noted nowhere. For an honesty-branded project this is the most
   pointed finding. **✅ Resolved (2026-06-23):** all four stale skips (2 in
   okn-memory `f140f0f`, 2 in okn-script `efa3f82`) un-skipped — the StackArena
   overflow test was corrected to respect the per-push header (the arena was
   right, the test was wrong); the other three passed as-is. `--no-skip` is now
   **0 skipped / 0 failed** across the gate.
4. **Dead code carried *and tested*.** The unused archetype/chunk ECS still
   compiles into `okn-ecs.lib` and its tests run in the gate (part of okn-ecs's
   2447 assertions exercise code no game/slice uses); the native render lib is
   ~82/120 near-empty placeholder `.cpp`. "Defer, don't delete" relabeled dead
   surface rather than removing it. **◑ Partly resolved (2026-06-23):** the dead
   **archetype/chunk ECS core was deleted** (15 files / 1090 lines; okn-ecs
   2447→2325 assertions — the removed ~122 all tested dead code). The native
   render backend was **partly finished, not deleted**: the D3D12 backend now has
   a gate-verified working draw pipeline — `render_clear_readback` (clear → exact
   readback) and `render_triangle_readback` (runtime HLSL → root sig + PSO → vertex
   buffer → `DrawInstanced` → **rasterized triangle**, pixel-exact), WARP fallback,
   suite `okn-render-native`. Still not a full backend (no materials/textures/depth,
   render-graph not GPU-wired, ~82 placeholder files) and not the default path.
5. **Single-author bus factor + private-NAS hosting** — all 13 submodules resolve
   only from one personal host; **Windows-only in practice** (hard-coded
   `D:/vcpkg` in several CMakeLists; the Linux CI job is the disabled stub).
6. **Manifest/doc drift** — `vcpkg.json` still pulls `msquic`/`quickjs-ng`/`basisu`
   (unwired); `ARCHITECTURE.md §2` still says "13 suites / ~620 cases"; the
   "Jolt is not cross-platform deterministic" claim **overstates** the constraint
   (Jolt is per-platform deterministic and offers `JPH_CROSS_PLATFORM_DETERMINISTIC`).
   **✅ Resolved (2026-06-23):** removed the 3 unwired deps from `vcpkg.json`
   (configure re-verified); updated `ARCHITECTURE.md` to 15 suites + games; and
   corrected the Jolt-determinism claim in `ARCHITECTURE.md` + `ROADMAP.md`.
7. **Thin where it counts** — okn-physics (the one "Verified" module) is 16 cases
   / 59 asserts and gained nothing this cycle (still single-threaded, no character
   controller, no trigger events, `collision_mask` ignored); okn-script core
   round-trips are skipped; the GPU sprite batcher silently drops a frame on
   overflow (8192-sprite fixed cap).

## Comparison (refreshed, web-grounded)

OKN's thesis — *buy undifferentiated infra, build only game glue* — is sound and
matches the industry. Where it **drifts off its own line**: it hand-rolls the one
core it should buy (ECS) and carries a dead second ECS + a placeholder native GPU
stack + a hand-written reliability layer that duplicate EnTT/flecs, Diligent/bgfx,
and GameNetworkingSockets/ENet.

| Peer | Verdict | Why |
|---|---|---|
| **Godot 4.4** (free, MIT) | **behind — the decisive comparison** | The engine a dev in OKN's niche actually picks. Mature editor, 2D+3D, UI/text/save, cross-platform export — and **4.4 (Mar 2025) shipped native Jolt**, eroding OKN's signature physics edge (though Godot's Jolt is still *experimental/opt-in*, so the erosion is directional, not complete). OKN's only honest edge: you can read its whole 2D slice in an afternoon — a *learning* edge, not a shipping one. |
| **Bevy 0.16** | behind | Purpose-built parallel archetypal ECS, used to ship. OKN built a real scheduler over its *live* World (good call, avoided two cores) but it's a 125-line sparse-set store with a bolt-on scheduler — and **no OKN game links it**. |
| **Cocos2d-x** | behind (closest fair peer) | Open-source 2D C++ + Lua, multi-platform, thousands of shipped titles. OKN's real edge is first-class Jolt contact-events-as-triggers — but Cocos *ships and exports*; OKN is the one to *read*. |
| **Unity 6.5 / Unreal 5.6** | different-niche | Category mismatch; OKN isn't competing and says so. OKN's 3D is unlit Lambert cubes; the native D3D12/Vulkan backend is an admitted placeholder. |
| **Hazel / raylib / bgfx-hobby engines** | comparable→ahead of the median | OKN matches the shipped-slice + self-honesty of solo engines and is ahead of the median hobby engine (test gate, real physics, 7 games), but trails on portability/maturity (raylib: cross-platform, zero dead code). |
| **Library picks** (Jolt/sokol/miniaudio/sol2) | comparable / well-chosen | The defensible heart of the project; all the libraries a knowledgeable dev would reach for at this scope. |

**Bottom line (unchanged from the prior eval, now with more evidence):** OKN is
**not** a credible alternative to any mainstream engine for shipping a game.
It is **for**: learning how a clean buy-vs-build slice is wired, and a portfolio
piece demonstrating disciplined C++26 + a headless-test culture + unusually honest
self-auditing docs. To become credible even for its narrow niche it needs, in
priority order: **(1)** one real game composing okn-ecs + okn-script + okn-render +
okn-physics (not a UniGUI bypass); **(2)** a CI gate that builds games+editor and
asserts the autodemo markers; **(3)** a cross-platform path (sokol GL + a Linux
build); **(4)** fix the skipped tests and bring the render/module docs down to what
ships. Fittingly, the project's own docs already reach this conclusion — its single
most durable strength is that **it tells the truth about itself** (which this audit
extends: even the green gate, it turns out, has an honest asterisk).

---

# Part A — Assessment

## 1. What it is

A **single-author, C++26 modular game-engine framework** whose real product is a
working **2D vertical slice** — `input → sparse-set ECS → Jolt physics (with real
contact events) → sokol sprite render → audio → Lua hot-reload → ImGui/UniGUI
editor → save/load` — plus **7 buildable demo games** on top, all under a green
**13-suite / ~620-case** test gate. Its defining trait is **documented honesty**:
ARCHITECTURE.md tells you to distrust the repo's own "completed" labels and names
its own dead code.

The reality is a **small, well-built modern-C++ core wearing a large aspirational
costume** — roughly **61% of module `src/*.cpp` files are near-empty**, and
several Layer-3 modules are stubbed, dead, or unlinkable. It is best understood as
a **legible learning / portfolio engine and an engineering-discipline showcase**,
not a production engine — a framing its own docs make correctly.

## 2. Strengths

1. **Buy-vs-build is real, documented, and well-executed where the code is live.**
   [ADR-0003](DECISIONS.md) codifies "buy undifferentiated infrastructure, build
   only game glue"; DECISIONS.md records every pivot (D3D12→sokol, QuickJS/Python→
   sol2, Qt→ImGui). The reference is **`okn-physics`** — a careful ~518-line Jolt
   wrapper that confines all Jolt headers to one TU, orders a `JoltInitGuard`
   before the allocator to dodge a real lifetime trap, implements broad-phase
   layers + a per-pair joint filter + a contact-listener mapping Jolt IDs back to
   engine IDs, and *warns loudly* on unsupported shapes rather than miscolliding.
   (Physics still has no character controller, is single-threaded, and isn't
   cross-platform deterministic.)
2. **A genuinely closed 2D slice, proven by ~4 games + a real integration-test
   suite.** The sprite2d path, `SliceWorld`+`LuaSlice`, and the `okn-ui →
   hud_bridge` edge are real and handle actual ownership concerns. *(Verified
   correction: only ~4 games — flappy/knockdown/platformer/mario — exercise the
   2D-sprite+Jolt+contact slice; voidborne is a standalone UniGUI app, harvest
   uses no physics, mario3d uses the 3D path, and no shipped game instantiates
   `SliceWorld` itself.)*
3. **Clean, modern, well-tested foundations** (`okn-core`, `okn-math`):
   `result<T> = std::expected<…>`, a `consteval` FNV-1a service registry,
   constexpr vector math with a divide-by-zero guard, **real SSE SIMD** and Perlin
   noise, ~19 math test files / ~137 cases. *(Tempered: vec3 is "mostly
   constexpr," not fully; `std::source_location` is aliased but not yet wired in.)*
4. **The honest, self-auditing docs/test culture is its signature strength.**
   ARCHITECTURE.md rates each module Verified/Substantial/Partial/Stub and
   *enumerates its own dead code*; ROADMAP.md carries a buy-vs-build ledger and
   corrects its own over-claims; the test script documents its own quirks. This
   candor is what separates OKN from the over-claiming hobby-engine archetype.
5. **Disciplined `/W4 /WX`** with every relaxation localized to a third-party-
   pulling target and commented, so warnings never infect engine code; games are
   single, testable TUs.
6. **Game-as-forcing-function with headless verification — but only partially.**
   *(The one strength the adversarial pass refuted as overstated.)* It's genuinely
   real for **voidborne and harvest** (every player verb is a shared function an
   `--autodemo` drives to a real `OK`/`WIN` marker). But mario/mario3d only write
   a marker via injected movement intent, and the **platformer is trace-only with
   no marker** — the exact counterexample to the stated "assert on the marker"
   method.

## 3. Weaknesses

| # | Severity | Weakness |
|---|---|---|
| W1 | **Major** | **Mass-scaffolding:** ~61% (≈383/630) of module `src/*.cpp` are near-empty — a mix of legit header-first empties, *unlinkable* dead code, and pure BOM+filename placeholders. `okn-editor` compiles 1 real file while ~80 panel/tool stubs exist only to look complete. Real vs. theatrical surface is indistinguishable. |
| W2 | **Major** | **The native GPU render lib is dead code:** `queue.cpp` and `command_queue.cpp` both define the same `D3D12CommandQueue` symbols → an ODR duplicate that makes the static lib **unlinkable as committed** (trivially fixable). ~30+ real-looking D3D12/Vulkan/render-graph files belong to a lib nothing can build. |
| W3 | **Major** | **CI is effectively a no-op.** GitHub jobs are hard-disabled (`if: false`); the only live gate is a self-hosted runner the docs treat as not-yet-standard, and it builds **only the 13 module suites — no game, no editor**. A commit that breaks a game's compile passes CI; the autodemo markers are never asserted. |
| W4 | **Major** | **Layer-3 stubs behind real-looking APIs:** network transports are no-ops (with a null-`impl_` `UdpSocket` bug that bites when **ASIO is present**); audio decodes only WAV (mp3/flac/ogg/opus return empty); assets have no streaming/hot-reload/basisu; `okn-memory`'s mimalloc backend is unwired and `guard_page` is a no-op; `okn-platform`'s work-stealing deque is fake (`steal()==pop()`). |
| W5 | **Major** | **The engine's own `result<T>` error model is declared but unused** — returned by no module; real error handling is ad-hoc (`id 0`/`nullptr`/`fprintf`, ~1 `throw` in the whole tree). The "canonical api/impl/defaults pattern" is fully fleshed out only in `okn-core`. |
| W6 | **Major** | **No game meets the engine's own north-star** (title → levels → win/lose → save → settings/rebind) — none has a title or settings screen — and the two most-developed titles **bypass the engine stack** (voidborne links only UniGUI; harvest uses no physics; no game links `okn-ecs`/`okn-script`). The showcases don't showcase the core. |
| W7 | Minor | 3D path is unlit Lambert cubes (no materials/textures/PBR/shadows/skinning); the editor rasterizes via `ImDrawList` not the engine's renderer; `okn-ui` is **mouse-only** (no keyboard/text → no menus). |
| W8 | Minor | A **second, dead archetype/chunk ECS** (~800 lines + scheduler/query) sits unused beside the live ~125-line sparse-set World (which has no scheduler/parallelism). |
| W9 | Minor | **Windows/D3D11-only** in practice (`if(WIN32)` gates, Linux CI disabled); cross-platform is aspirational. |
| W10 | Minor | **Edge over-claims:** bilingual i18n is thin (1 of 24 voidborne data files has Chinese; the `en/zh.json` tables are loaded by nothing); flappy/knockdown have no autodemo; FNV-1a/type-alias duplication; a `const_cast` in `slice_world.hpp`. |

## 4. Comparison with similar projects

| Project | Verdict |
|---|---|
| **Godot 4** | Not a competitor and doesn't pretend to be. OKN's only honest edge is **legibility** (read its slice in an afternoon). Ship with Godot; *read* OKN to learn how a slice is wired. |
| **Bevy** | Sharpest architectural foil — OKN **loses on its nominal centerpiece**: Bevy is a fast parallel archetypal ECS + scheduler + wgpu graph; OKN's real ECS is a 125-line sparse-set store with no scheduler, and its archetype core is dead. |
| **Cocos2d-x** | Fairest, closest comparison (2D C++ + Lua). OKN's real differentiator is **first-class Jolt physics with contact-events-as-triggers** vs Cocos's peripheral Box2D — but Cocos is shipped, multi-platform, full-featured with thousands of titles. |
| **raylib** | Not really competitors. raylib is finished, cross-platform, batteries-included, zero dead code, 70+ bindings. OKN is more *architecturally ambitious* and has real physics + an editor, but raylib wins on everything that makes a tool **usable by others**. |
| **O3DE / Stride / Flax** | Category mismatch on 3D/tooling. The comparison mostly highlights that OKN **correctly refuses** to build everything in-house. |
| **TheCherno Hazel/Walnut, Handmade Hero** | Vs Hazel: OKN is **more shipped** (a closed slice + 7 games) and **far more self-honest**, but lags on polish/pedagogy/community. Vs Handmade Hero: the philosophical opposite (buy-glue vs build-everything); reaches games faster, but its hand-written parts include literal stubs. |
| **olcPixelGameEngine / Magnum** | olcPGE wins on instant-on simplicity; OKN wins on scope + headless-test discipline. **Magnum is the grown-up version of what OKN gestures at** and OKN trails it on maturity/portability/adoption. |

## 5. Fit

**Good for:** a solo learning/portfolio artifact; a reference for clean
**buy-vs-build glue** (especially the Jolt wrapper); studying **disciplined
process** (game-as-forcing-function, headless autodemos, honest docs); small
**physics-forward 2D demos on Windows** inside the recipe; demonstrating
**modern C++23/26 foundations**.

**Not for:** shipping a commercial/cross-platform game; any real **3D**;
**networked** games; **rich audio** (WAV only); **UI/menu-heavy** games;
a **drop-in dependency** for others; teams **expecting CI to protect them**.

## 6. Bottom line

A **small, real, well-built 2D vertical slice and an unusually honest
engineering-discipline showcase wearing a large aspirational costume.** Where the
code is live — `okn-physics`, `okn-math`, `okn-core`, the sokol-backed
render/slice glue — it is idiomatic, modern, constexpr-correct C++ with a coherent
buy-vs-build strategy, a green test gate, and buildable demo games as living
acceptance tests. Its single biggest strength is **candor**. But the gaps are
large and structural — mass-empty source, an unlinkable render lib, stubbed
subsystems, a dead second ECS, an unused error model, no-op CI, and no game that
meets its own north-star. Against mature engines it lags by two to three orders of
magnitude in 3D, tooling, platform reach, and adoption — **and it openly says so.**
The largest debts are the *volume of misleading dead/empty surface area* and a
*process (CI, a finished game)* that still lags the quality of its real core.

---

# Part B — Remediation Plan

> **Update:** the owner chose to **finish the scaffolding rather than delete it.**
> The build-out program that supersedes this prune-first plan is
> **[COMPLETION_PLAN.md](COMPLETION_PLAN.md)** (phases A–D + the three buy-vs-build
> forks). The deletion-oriented plan below is kept for reference / the alternative
> path. *(Progress: **`okn-memory` finished** — real guard pages, the global
> `operator new` override, and the mimalloc backend link; verified both flags-off
> (107 cases / 1009 asserts) and flags-on (108 / 1014).)*

## Guiding principles

1. **Subtract before you add.** The biggest debts (W1, W2, W8) are *dead/empty
   surface area*. Deleting it is cheap, low-risk, and raises the codebase's
   signal-to-noise more than any feature. Do it first.
2. **Honesty is the bar, not the ceiling.** The docs already tell the truth; the
   work is to make the *code* match the docs — either finish a thing or delete it,
   so "Partial/Stub" shrinks toward "Verified/(absent)".
3. **One forcing function.** A single complete game on the real engine stack
   (the north-star) drives more fixes than tackling subsystems in isolation —
   it forces UI text input, input rebinding, settings persistence, and exercises
   ecs+physics+render+audio+lua together.
4. **Gate everything you claim.** Every result-marker the design produces should
   be asserted in CI, or the claim should be dropped.
5. **Stay on the buy-vs-build line.** Don't resurrect dead in-house render/ECS
   cores; lean harder on Jolt/sokol/miniaudio/sol2/dr_libs.

## Prioritization

Ordered by **(impact on the codebase's honesty/usability) ÷ (effort)**. R0 is
days of mostly-deletion; R1–R3 are the substance; R4 is longer/optional.

## Phase R0 — Quick wins: delete dead surface, fix the obvious (a few days)

Cheap, high-signal, low-risk. Mostly deletion + small fixes.

- **Fix the unlinkable render lib (W2, first cut).** Delete one of the duplicate
  `D3D12CommandQueue` TUs (`queue.cpp` *or* `device/command_queue.cpp`) so the
  `okn-render` static lib at least links. (Full excision is R2.)
- **Prune comment-only stub TUs (W1).** Remove the ~80 `okn-editor` panel/tool
  placeholders and the equivalent BOM-plus-filename files elsewhere that are added
  to **no target**. Keep only files that are in a build target or carry real code.
  Add `scripts/check_no_stub_tus.ps1` to the repo (flag any `src/*.cpp` that is
  comment/BOM-only *and* compiled into a target) so they can't creep back.
- **Fix the `UdpSocket`/`TcpStream` null-`impl_` bug (W4).** The ASIO-*present*
  ctor leaves `impl_` null; allocate it (mirror the `#else` branch). One-line fix
  to a real crash.
- **Clean `vcpkg.json` (W4/strength-1 nit).** Remove the **unwired** deps
  (`msquic`, `quickjs-ng`, `basisu`, `mimalloc`) per ADR-0003, or wire them behind
  their flags. Stops pulling dead weight into every build.
- **Soften/finish the doc over-claims (W10).** Either translate the voidborne data
  files and load the `en/zh.json` tables, or delete the dead `en/zh.json` and
  scope the "bilingual" claim to what ships; reconcile GAMES.md's
  "every game is CI-checkable" with flappy/knockdown (add markers or qualify).
- **Acceptance:** `okn-render` links; the near-empty `src/*.cpp` ratio drops to the
  legitimate header-first set; the stub-guard script is green; the UdpSocket crash
  is gone; vcpkg manifest carries only wired deps; docs match reality.

## Phase R1 — Make CI a real safety net (1–2 weeks)

Turn the headless-verification design into an actual gate.

- **Re-enable real CI (W3).** Make one of these the canonical gate and remove
  `if: false`: either stand up the gitea self-hosted Windows runner *or* a GitHub
  Windows runner with vcpkg. The gate must **build all modules + all games +
  the editor** and run `run_tests_all.ps1` (the 13 suites).
- **Assert on autodemo markers (W3, W6-adjacent).** Add a CI step that runs each
  game's `--autodemo`/`OKN_*_AUTODEMO` and greps its result marker for `OK`/`WIN`.
  Give the **platformer a real marker** (it's currently trace-only — the
  counterexample to the project's own method).
- **Decide the error model (W5).** Either adopt `result<T>` on the public
  boundaries of `okn-physics`/`okn-asset` (the two most error-prone), or write an
  ADR formally deprecating it and documenting the real convention (`id 0`/
  `nullptr` + logged warnings). Make the docs and code agree.
- **Acceptance:** a push that breaks any module/game/editor compile, or regresses
  a game's autodemo marker, **fails CI**; an ADR states the live error policy and
  ≥2 modules' public APIs match it (or `result<T>` is removed).

## Phase R2 — Excise dead subsystems / finish-or-delete the stubs (2–4 weeks)

Collapse the aspirational costume into what's real.

- **Delete the native render lib (W2, full).** The real renderer is the three
  header-first sokol paths. Remove `backend_d3d12/vulkan/metal/gl`, `queue`,
  `device`, `render_graph`, and the dead native tests; reduce `okn-render` to the
  sprite2d/mesh3d/slice headers + the thin sokol glue. (~30+ files gone.)
- **Delete the dead archetype ECS (W8).** Remove `storage/`, `scheduler/`,
  `query/` (the ~800-line unreferenced core); keep the sparse-set `World` as the
  one ECS. Already planned in [ROADMAP P9](ROADMAP.md). Symbol-neutral — the gate
  stays green.
- **Finish-or-delete the Layer-3 stubs (W4), per the buy-vs-build ledger:**
  - **Audio decoders:** add `dr_wav`/`dr_mp3`/`dr_flag`/`stb_vorbis` (or route via
    `ma_engine`/`ma_decoder`) for real format support, *or* delete the empty
    mp3/flac/ogg/opus decoders and scope the claim to WAV. Delete the bool-flip
    platform backends (miniaudio already provides them).
  - **Network:** keep + test the real reliability core; **delete the fake
    QUIC/TCP/UDP transports** (no-ops) until the determinism ADR and single-player
    ship gate them back in ([ROADMAP §6](ROADMAP.md)).
  - **Memory:** wire `mimalloc` behind the existing `OKN_MEMORY_USE_MIMALLOC`
    flag (it's coded but link-disabled), or delete `mi_backend.cpp` + the no-op
    `guard_page`/`override_new`.
  - **Platform:** replace the fake work-stealing deque with a real one or remove
    the API.
  - **Assets:** mark streaming/hot-reload/basisu honestly as absent (delete the
    no-op streamers) until needed.
- **Acceptance:** every advertised API is either **real + tested** or **removed**;
  `okn-render`/`okn-ecs` shed their dead cores; the test gate stays green
  throughout (deletions are symbol-neutral; new decoders add tests).

## Phase R3 — Close the north-star game (the forcing function) (2–4 weeks)

This is [ROADMAP P5–P7](ROADMAP.md) and the single highest-leverage gameplay work.

- **Take ONE small game to the full loop** *on the engine stack*: title screen →
  several levels (authored in the editor or a data format) → win/lose → **save/
  load progress** → a settings screen with **audio + key rebinding**. Build it on
  `sprite2d + okn-ecs + okn-physics + okn-audio + okn-script` (not a UniGUI
  bypass like voidborne). The platformer is the natural candidate.
- This **forces fixes to W6, W7, and part of W5**:
  - **`okn-ui` keyboard/text input** (settings/menu text fields) — extend the
    input router with key/char events; make `TextInput` actually receive chars.
  - **An input action-mapping layer** (bind actions→keys, rebindable) — replaces
    per-game raw key checks.
  - **Settings persistence** + a real menu/title flow.
- **Acceptance:** a person who has never seen the code plays it **start to
  finish**, progress persists across runs, audio/keys are configurable — and it
  links `okn-ecs` + the `okn-render` slice (the core is finally showcased).

## Phase R4 — Platform reach & polish (longer / ongoing)

- **Cross-platform (W9):** add the sokol **GL backend**, get the foundation
  modules + ≥1 sample building+testing on **Linux**, re-enable the Linux CI job.
  ([ROADMAP P9](ROADMAP.md).)
- **Editor uses the real renderer (W7):** render the engine's own
  `GpuSpriteRenderer` into the editor viewport (needs the UniGUI D3D11-device
  accessor patch). ([ROADMAP P8](ROADMAP.md).)
- **3D, only if pursued (W7):** materials/textures/lighting beyond Lambert cubes —
  explicitly a non-goal in the current roadmap; leave the mesh3d demo honest.
- **Determinism ADR before any netcode** (gates W4's network work permanently).

## Weakness → fix map

| Weakness | Phase | Concrete fix | Acceptance |
|---|---|---|---|
| W1 mass scaffolding | R0 (+R2) | Delete comment-only stub TUs; add stub-guard script | Near-empty ratio = legit header-first set; guard green |
| W2 dead render lib | R0 then R2 | Delete dup symbol file; then excise the native lib | Lib links, then is removed; no dead backends |
| W3 no-op CI | R1 | Re-enable a gate; build modules+games+editor; assert markers | A breaking commit fails CI |
| W4 Layer-3 stubs | R0 (bug) + R2 | Fix UdpSocket; finish-or-delete decoders/transports/memory/platform | Each API real+tested or removed |
| W5 unused error model | R1 | Adopt `result<T>` on 2 boundaries, or deprecate via ADR | Docs+code agree on one error policy |
| W6 no north-star game | R3 | One complete game on the engine stack | Played end-to-end; persists; links ecs+slice |
| W7 3D/editor/UI thin | R3 (UI) + R4 | UI keyboard/text; editor real renderer; (3D optional) | TextInput receives chars; editor uses GpuSpriteRenderer |
| W8 dead archetype ECS | R2 | Delete storage/scheduler/query | Removed; gate green |
| W9 Windows-only | R4 | sokol GL + Linux foundation build + CI | Foundation builds+tests on Linux in CI |
| W10 over-claims | R0/R1 | Finish-or-cut bilingual; add/qualify game markers | Docs match reality |

## Relationship to [ROADMAP.md](ROADMAP.md)

This plan **complements** the forward feature roadmap rather than replacing it:

- **R0/R2 (subtract dead code) is ROADMAP P9's "great prune"**, pulled forward
  because it's cheap and raises signal immediately.
- **R1 (CI) is also ROADMAP P9**; this plan makes it gate games, not just suites.
- **R3 (north-star game) is ROADMAP P5–P7** and the shared centerpiece.
- **R4 (cross-platform, editor renderer) is ROADMAP P8–P9.**

The net direction is the same as the roadmap's north star — *ship one complete
small game on the engine* — with the addition of an explicit, front-loaded
**"make the code match the honest docs"** pass (delete dead surface, finish or
cut the stubs, and gate what you claim).
