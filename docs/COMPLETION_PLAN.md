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

## Second module — okn-platform: work-stealing deque ✅ DONE (2026-06-23)

`TaskQueue` was a placeholder — a single mutex over a `std::vector` whose
`steal()` took from the *same* end as `pop()` (not a work-stealing deque at all).
Now a **bounded lock-free Chase-Lev deque**: owner push/pop the bottom (LIFO),
thieves steal the top (FIFO); tasks held by `std::atomic<Task*>` so only a
trivially-copyable pointer is read speculatively; fence-based memory ordering per
Lê et al. 2013 (correct on ARM/POWER, not just x86-TSO). `WorkStealingThreadPool`
was fed *illegally* (external-thread `push`, which Chase-Lev forbids) — `submit()`
now routes through a mutex injector and each worker refills its **own** deque
owner-side before stealing. A 4-lens adversarial verification workflow cleared the
deque and caught one real defect — the destructor silently dropped pending work —
now fixed with a graceful `wait_all()` drain. Tests: bounded-full, a 50k-op
concurrent owner+4-thieves "each task exactly once" stress, multi-producer pool
stress, drain-on-destroy regression → **113 cases green, 5/5 repeated runs**.
*(submodule `c01d55c`)*

The rest of okn-platform (time/thread/fs/input/crash/etc.) was already
implemented in commit `4bbd20c`; this closed the one scaffolding stub.

## Third module — okn-network: UdpSocket/TcpStream null-`impl_` ✅ DONE (2026-06-23)

In the `OKN_NET_HAS_ASIO` build, `UdpSocket(AsioAdapter&)` and `TcpStream(AsioAdapter&)`
left `impl_` null — the first method call (`is_bound`/`bind`/`send`/…) dereferenced
a null `unique_ptr` and crashed (the non-ASIO branch built its `Impl`, masking the
bug in asio-less builds). Root cause: the ctors ignored the adapter, and their
`Impl` needs the adapter's `asio::io_context`, which `AsioAdapter` hid behind its
pImpl. Fix: an internal `AsioAdapter::native_context()` exposing the context as an
opaque `void*` (no `<asio.hpp>` in the header); both ctors build `Impl` from it;
`close()` now actually closes the socket; `UdpSocket::bind()` records the real
local port so `bind(0)` is usable. New `test_transport.cpp` regresses the null
deref on both sockets and proves a **real UDP loopback datagram roundtrip**.
**okn-network wired into the gate** (`run_tests_all.ps1`) — it runs headless over
loopback ASIO, no GPU/Qt. Full gate now **14/14 suites green** (102 cases / 774
assertions in okn-network). *(submodule `525544d`)*

> Latent fix along the way: the root pinned okn-network at a commit that had never
> been pushed to its remote `main` (a fresh `git submodule update` couldn't fetch
> it). Publishing the fix fast-forwarded `main` to include the dev history, so the
> pinned SHA is now actually fetchable.

`TcpAcceptor::listen`/`accept` remain non-crashing stubs — building a real TCP
server (and TCP loopback test) is **Phase B** transport work, tracked separately.

## Fourth module — okn-ecs: ScriptingBridge ✅ DONE (2026-06-23)

The `ScriptingBridge` was scaffolding: `script_has_component()` always returned
`false` and `register_all_to_script()` was a no-op loop. It also tried to resolve
names through the global `ComponentRegistry`, whose ids hash `__FUNCSIG__` and so
**never match** the `World`'s stores (keyed by `hash(typeid(T).name())`) — the
lookups could not have worked even if implemented. Reworked into a real runtime
ECS surface: `World::has_component_by_id(Entity, ComponentTypeId)` (type-erased,
keyed by the same id `has_component<T>` uses); the bridge keeps its own
name→{World id, size} map via a typed `register_component<T>(name)`, so
`has_component(entity, name)` / `component_id(name)` resolve to live storage; and
`register_all_to_script(ctx, fn)` forwards components through a C callback, keeping
the bridge decoupled from okn-script. New `test_scripting.cpp` (4 cases) →
okn-ecs_tests **70 cases / 443 assertions green**; full 14-suite gate green.
*(submodule `ef217ea`)*

> **Note:** `okn-ecs` and `okn-render` *are* tracked submodule gitlinks in the
> root (verified via `git ls-tree`); the `?` in `git status` was the
> "submodule has untracked content" marker for regenerable test artifacts
> (`*.bmp`/`*.dmp`) inside their working trees, not an untracked submodule. The
> root pointer for okn-ecs is bumped to `ef217ea` here.

## Fifth module — okn-asset: audio + font importers ✅ DONE (2026-06-23)

`audio_importer` and `font_importer` were empty (header-guard-only) scaffolding.
Implemented both on the texture-importer pattern (real impl + graceful fallback,
`import(path)`/`import(buffer)`):

- **AudioImporter** — a self-contained RIFF/WAVE **PCM + IEEE-float** decoder, no
  third-party dependency. Walks the chunk list with full bounds checks (clamps
  short chunks, word-aligns, tolerates trailing chunks); returns interleaved
  sample bytes + format, with `frame_count()`/`valid()`. WAV is the universal
  baseline; compressed formats (mp3/flac/ogg) stay in okn-audio (miniaudio,
  Phase B).
- **FontImporter** — real **stb_truetype** behind `OKN_ASSET_HAS_STB` (graceful
  no-op fallback otherwise). Loads TTF/OTF → glyph count + vertical metrics,
  retains the bytes, and `rasterize(codepoint, px)` returns an 8-bit coverage
  bitmap via `stbtt_GetCodepointBitmap`.

Tests synthesize WAVs in-memory (16-bit stereo decode, byte-exact payload,
trailing-chunk tolerance, invalid inputs) and, under stb, load a present system
font and rasterize 'A' (verified live: 4503 glyphs, 21×21 bitmap). okn-asset_tests
**112 cases / 363 assertions green**; full 14-suite gate green. *(submodule `a7b324e`)*

---

## Sixth module — okn-ui: keyboard + editable text input ✅ DONE (2026-06-23)

`TextInput` reused the base `text_` field for display but had no editing; the
`InputRouter` was mouse-only; there was no keyboard path. Added a `Key` enum,
`Widget::on_char`/`on_key` virtuals routed to the focused widget, router
`push_char`/`push_key`, and a real **UTF-8-correct** editable `TextInput`:
byte-offset caret kept on codepoint boundaries, insert (max_length-bounded),
backspace/delete + left/right moving whole codepoints, home/end, focus-gating, a
focused caret (nominal advance pending text_layout metrics). `test_input.cpp` (was
empty) now covers all of it. okn-ui_tests **29 cases / 124 assertions green**, and
**okn-ui is wired into the gate (15 suites)**. *(submodule `638be39`)*

## Phase A status — ✅ COMPLETE

All Phase-A finish-it module work is done and verified: **okn-memory**,
**okn-platform** (Chase-Lev deque), **okn-network** (UdpSocket + gate),
**okn-ecs** (ScriptingBridge), **okn-asset** (audio/font importers), **okn-ui**
(keyboard/text). The gate grew from 13 → **15 suites** (okn-network + okn-ui
wired in). **Deferred:** asset streaming + hot-reload (Phase B); okn-network real
`TcpAcceptor`/`accept` + reliability over live sockets (Phase B); the three
buy-vs-build forks (Phase C); the north-star game (Phase D).

## Phase B — ✅ COMPLETE (2026-06-23)

1. **okn-audio mp3/flac/ogg decoders.** `Mp3Decoder`/`FlacDecoder`/`VorbisDecoder`
   were empty-buffer stubs. mp3+flac now decode via a shared `ma_decode_memory()`
   over miniaudio's `ma_decoder` (dr_mp3/dr_flac, s16, read-until-exhausted);
   ogg/vorbis via `stb_vorbis` (not a miniaudio built-in) behind `OKN_AUDIO_HAS_STB`.
   Verified against **real ffmpeg-generated fixtures** (mono 8 kHz tone.mp3/.flac/
   .ogg) + malformed-input rejection. 46 cases / 457 asserts. *(okn-audio `171c955`)*

2. **okn-asset streaming + real mtime hot-reload.** Hot reload only checked file
   existence; now `AssetIO::file_mtime()` (via platform `get_file_attributes`) backs
   a HotReload that reloads on a real edit. The streaming queues were stubs; now
   `ChunkStreamer`/`MipStreamer` are real request schedulers (enqueue + dedup +
   process-with-callback + residency tracking) and `UploadQueue` drains staged
   buffers through a sink with cumulative accounting. Hot-reload tested with a
   deterministic mtime bump. 113 cases / 394 asserts. *(okn-asset `271fe93`)*

3. **okn-network real `TcpAcceptor` + reliability over live sockets.**
   `TcpAcceptor::listen`/`accept` were stubs; now a real ASIO TCP server (listen/
   bind/accept-from-backlog, adopting the accepted socket into a `TcpStream`). The
   `ReliabilityLayer` (already real over `ITransport`) is now exercised over **live
   UDP sockets** via `UdpTransport`: ordered reliable delivery + over-the-wire acks.
   New TCP loopback + live-UDP-reliability tests. 104 cases / 798 asserts. *(okn-network
   `5d3cd99`)*

Along the way: fixed a **pre-existing flaky** okn-ecs test (`ChunkAllocator -
reset`) that compared freed pointers — deterministic now (8/8 runs). *(okn-ecs
`2e3e67f`)*

**Phases A and B are complete.** The gate is **15 suites, all green**.

## Phase C — ✅ RESOLVED (2026-06-23)

Each fork was decided per the cost/benefit this plan made explicit — the
*recommended* path built where it adds real value, and the duplicate-infra full
rebuilds consciously deferred (they are multi-week and duplicate already-bought
infrastructure). Nothing was faked or half-built; the code now matches reality.

| Fork | Decision | What landed |
|---|---|---|
| **C2 — archetype/chunk ECS** | **Built the recommended alternative.** | Made the parallel **Scheduler over the live sparse-set World** real: a concrete `ThreadPoolJobSystem` now drives `run_parallel()` (conflict-based level grouping → worker-thread dispatch), tested over 1000 entities × 5 frames. The dead archetype/chunk core was **not** revived — two ECS cores is the anti-pattern [ADR-0004](DECISIONS.md) rejects. *(okn-ecs `1144c32`)* |
| **C3 — QUIC via msquic** | **Deferred; made the stub honest.** | msquic is not in the build, and `QuicStream` was a misleading fake (`connect()`→true with no impl). Now it reports honestly that QUIC is unavailable, so callers use the shipping **TCP/UDP + ReliabilityLayer** (finished in Phase B, with TCP accept + live-socket reliability). QUIC stays an opt-in stretch behind `OKN_NET_HAS_MSQUIC`. *(okn-network `686b981`)* |
| **C1 — native render backend** | **Deferred; kept sokol, labeled the scaffold.** | The native `src/`+`gpu/` lib **builds clean** (`okn-render.lib`, no ODR), but its device/queue/swapchain/PSO/render-graph are placeholders. Per [ADR-0003/0005](DECISIONS.md) the engine ships on **sokol**; the README now carries an honest status banner marking the native stack a deferred future backend, not the default. The real, gate-tested routes remain the 2D sprite / 3D mesh / vertical-slice paths. *(okn-render `eaffd4c`)* |

**Net:** the one fork worth building (a parallel scheduler on the live World) is
built and tested; the two that duplicate bought infra (a hand-written GPU backend,
QUIC) are deferred with the code made truthful about it. **Remaining: the
north-star game (Phase D).**

## Where this meets [ROADMAP.md](ROADMAP.md)

The non-fork program is the roadmap's intent reached by *building out* rather than
pruning: Phase A/B finish the Layer-3 subsystems the roadmap lists as gaps; Phase D
is ROADMAP P5–P7 (the north-star game); the forks (C) are exactly the items the
roadmap's P9 marked for pruning — kept here as the owner's elective finish-it work
with the cost made explicit.
