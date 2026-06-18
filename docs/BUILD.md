# OmniKillerNexus — Build, Test & Run

Practical, copy-pasteable instructions for building the engine, running the test
gate, and launching the demo games. For *what* the modules are, see
[ARCHITECTURE.md](ARCHITECTURE.md); for the games, see [GAMES.md](GAMES.md).

---

## 1. Prerequisites

| Requirement | Notes |
|---|---|
| **C++26 compiler** | MSVC (Visual Studio 18 2026) — the primary target. The build uses `/std:c++latest` (= C++26 preview); the literal `CMAKE_CXX_STANDARD 23` is only the CMake→MSVC mapping, **not** a downgrade ([ADR-0001](DECISIONS.md)). |
| **CMake ≥ 3.20** | Generator: **Ninja** (recommended). |
| **vcpkg** (manifest mode) | The root `vcpkg.json` declares every third-party dep; vcpkg installs them into `build-phys/vcpkg_installed/` at configure time. Default root used here: `D:\vcpkg`. |
| **Submodules** | `git submodule update --init --recursive` — the `okn-*` modules **and** `third_party/TeamkillerUniGUI` (needed for the editor and `games/voidborne`). |

> **Use a dedicated build directory.** This repo's canonical out-of-source dir is
> **`build-phys/`**. A stale `build/` directory in this tree pins a deleted MSVC
> toolset and will fail to configure — always build into a fresh `build-*` dir
> (all of `build/`, `build-*/` are git-ignored).

---

## 2. Configure (one time)

MSVC + Ninja needs the MSVC environment. From a developer prompt **or** by
sourcing `vcvars64.bat` first:

```bat
:: load the MSVC toolchain (adjust the VS path to your install)
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

:: configure into build-phys with the vcpkg toolchain
cmake -S . -B build-phys -G Ninja ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

Key root CMake options (all default **ON** except install):

| Option | Default | Purpose |
|---|---|---|
| `OKN_ENABLE_CLIENT_SDK` / `_SERVER_SDK` / `_EDITOR_SDK` | ON | the aggregate INTERFACE SDK targets |
| `OKN_BUILD_EDITOR_EXE` | ON | build `okn-editor-app` (needs `unigui::unigui`) |
| `OKN_BUILD_CLI` | ON | build the `tools/` CLI |
| `OKN_ENABLE_INSTALL` | **OFF** | install/export is off until modules add `install()` rules — leave off |

---

## 3. Build

```bat
:: everything
cmake --build build-phys -j

:: a single target (module, test suite, game, sample)
cmake --build build-phys --target okn-physics
cmake --build build-phys --target voidborne
cmake --build build-phys --target okn-math_tests
```

Outputs land in `build-phys/bin/` (executables) and `build-phys/lib/`.

---

## 4. The test gate

There are **13 unit-test suites, all green** (~620 cases). **They are *not*
registered with ctest** (`ctest -N` reports `Total Tests: 0`) — run the exes
directly. **Gotcha:** the core suite is `okn-core-tests` (**hyphen**); every
other suite is `okn-<module>_tests` (**underscore**).

```powershell
# build + run all suites with one command (handles the hyphen/underscore split)
pwsh scripts/run_tests_all.ps1
```

Or run one:

```bat
build-phys\bin\okn-physics_tests.exe
build-phys\bin\okn-core-tests.exe
```

The 13 suites: `okn-core-tests`, and `okn-{math, memory, platform, ecs, asset,
audio, script, physics, render2d, render2d_gpu, slice, lua_slice}_tests`.

---

## 5. Run a game

Build the target, then launch its exe from `build-phys/bin/`:

```bat
cmake --build build-phys --target flappy
build-phys\bin\flappy.exe
```

- **Data-driven games run from `build-phys/bin/`.** `voidborne` and `harvest`
  read `data/` / `assets/` relative to the working directory (the build copies
  them next to the exe via a POST_BUILD step), so `cd build-phys\bin` first or
  launch with that as the working dir — otherwise data load fails.
- **Headless / CI mode.** Each game exposes an autodemo that drives the real
  game logic with no display and writes a result marker file. Examples:
  `voidborne.exe --autodemo` → `voidborne_result.txt` ("VOIDBORNE M0-M7 OK …");
  `OKN_HARVEST_AUTODEMO=1 harvest.exe` → `harvest_result.txt`;
  `OKN_PLAT_AUTODEMO`, `OKN_MARIO_AUTODEMO`, `OKN_MARIO3D_*`. See
  [GAMES.md](GAMES.md) for each game's hooks.
- **Frame-limited render.** sokol/UniGUI apps accept `--frames N` to render N
  frames and exit (used for screenshots and smoke tests).

The 7 games: `flappy`, `knockdown`, `platformer`, `mario`, `mario3d`, `harvest`,
`voidborne`. Plus the render samples `okn-sprite2d_app`, `okn-slice_app`.

---

## 6. The editor

```bat
cmake --build build-phys --target okn-editor-app
build-phys\bin\okn-editor-app.exe            :: live ImGui/DX11 editor
build-phys\bin\okn-editor-app.exe --selftest :: headless save/serialize round-trip (exit 0)
```

It edits a real `SliceWorld`/`LuaSlice` (ECS + Jolt + 2D render + audio + Lua)
and only builds inside the root build (it needs `unigui::unigui`).

---

## 7. Screenshots of the windowed apps

sokol (D3D11) and UniGUI windows are **DPI-sensitive** and don't composite into
the desktop the way GDI windows do. The reliable capture (used throughout
development) is **`PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT=2)`** with a
**DPI-aware capture thread**:

```powershell
# before GetWindowRect / capture:
SetThreadDpiAwarenessContext((IntPtr)-4)   # PerMonitorV2
# then PrintWindow(hwnd, hdc, 2) into a bitmap sized by GetWindowRect.
```

`CopyFromScreen` tends to grab the wallpaper instead of an occluded swapchain;
`PrintWindow` forces the window to paint itself into the DC. Every windowed
target embeds a **PerMonitorV2 `.manifest`** + `high_dpi = true` so the
framebuffer matches the window (without it, the scene renders into only the
top-left `1/dpi_scale` of the client).

---

## 8. Continuous integration

- **`.gitea/workflows/ci.yml`** runs the `scripts/run_tests_all.ps1` gate on
  push/PR (needs the self-hosted runner — see
  [gitea_runner_setup.md](gitea_runner_setup.md)).
- **`.github/workflows/`** holds `full-build.yml` and `quick-check.yml`.

No change lands without the 13-suite gate green.
