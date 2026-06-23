# scripts/run_tests_all.ps1
# CI gate: build + run every buildable module test target and report a summary.
#
# Why this exists: the per-module test targets are named INCONSISTENTLY —
# okn-core uses a HYPHEN ("okn-core-tests") while every other module uses an
# UNDERSCORE ("okn-<m>_tests"). Naive scripts that assume "okn-core_tests" fail.
# This gate special-cases that mapping so the whole suite stays green in one shot.
#
# okn-editor and the native render backend are intentionally excluded (need GPU
# backends / windowing). okn-network (loopback ASIO) and okn-ui (pure widget /
# layout / input logic) run headless, so they ARE in the gate.
#
# After the module suites it ALSO compile-gates all 7 games (a broken game build
# must fail CI) and behaviour-gates VOIDBORNE headlessly (--selftest/--autodemo
# result markers). The 6 sokol games need a GPU/window, so they are compile-only.
#
# Usage:
#   .\scripts\run_tests_all.ps1                 # configure (if needed) + build + run
#   .\scripts\run_tests_all.ps1 -BuildDir build-phys
#   .\scripts\run_tests_all.ps1 -SkipGames      # modules only (faster)
param(
    [string]$BuildDir = "build",
    [string]$Config   = "Debug",
    [switch]$SkipGames
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $root $BuildDir

# Modules with buildable, real test suites, and their actual target names.
# NOTE the okn-core hyphen — this is the whole point of the special-case.
$targets = [ordered]@{
    "okn-core"     = "okn-core-tests"
    "okn-math"     = "okn-math_tests"
    "okn-memory"   = "okn-memory_tests"
    "okn-platform" = "okn-platform_tests"
    "okn-ecs"      = "okn-ecs_tests"
    "okn-asset"    = "okn-asset_tests"
    "okn-audio"    = "okn-audio_tests"
    "okn-script"   = "okn-script_tests"
    "okn-physics"  = "okn-physics_tests"
    "okn-network"  = "okn-network_tests"        # headless transport over loopback ASIO
    "okn-ui"       = "okn-ui_tests"             # widget / layout / input logic (headless)
    "okn-render2d" = "okn-render2d_tests"       # 2D sprite path (software backend)
    "okn-render2d-gpu" = "okn-render2d_gpu_tests" # sokol_gfx GPU backend (dummy, headless)
    "okn-slice" = "okn-slice_tests"               # ECS+physics+sprite+UI+audio integration
    "okn-lua-slice" = "okn-lua_slice_tests"       # Lua-authored, hot-reloaded gameplay (sol2)
}

# Locate vcvars64 (MSVC + Ninja need the developer environment).
$vcvarsCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) { Write-Host "[ERROR] vcvars64.bat not found." -ForegroundColor Red; exit 1 }

# Runs a command inside the MSVC dev environment. Build output streams to the
# host; the exit code is captured in $script:devExit (NOT returned, so cmake's
# stdout never pollutes the caller's value).
function Invoke-Dev([string]$cmd) {
    cmd /c "call `"$vcvars`" > nul && $cmd"
    $script:devExit = $LASTEXITCODE
}

# Configure if there's no cache yet.
if (-not (Test-Path (Join-Path $buildPath "CMakeCache.txt"))) {
    Write-Host "[*] Configuring $BuildDir ..." -ForegroundColor Yellow
    $toolchain = "D:/vcpkg/scripts/buildsystems/vcpkg.cmake"
    $cfg = "cmake -S `"$root`" -B `"$buildPath`" -G Ninja -DCMAKE_BUILD_TYPE=$Config " +
           "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    Invoke-Dev $cfg
    if ($script:devExit -ne 0) { Write-Host "[ERROR] Configure failed." -ForegroundColor Red; exit 1 }
}

$failures = @()
$summary  = @()

foreach ($mod in $targets.Keys) {
    $tgt = $targets[$mod]
    Write-Host "[*] Building $tgt ..." -ForegroundColor Yellow
    Invoke-Dev "cmake --build `"$buildPath`" --target $tgt"
    if ($script:devExit -ne 0) {
        $failures += $mod
        $summary  += "  {0,-14} BUILD FAILED" -f $mod
        continue
    }
    $exe = Join-Path $buildPath "bin\$tgt.exe"
    if (-not (Test-Path $exe)) {
        $failures += $mod
        $summary  += "  {0,-14} EXE MISSING ($exe)" -f $mod
        continue
    }
    & $exe | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $failures += $mod
        $summary  += "  {0,-14} TESTS FAILED (exit $LASTEXITCODE)" -f $mod
    } else {
        $summary += "  {0,-14} OK" -f $mod
    }
}

# ── Games: compile-gate all 7, behaviour-gate the headless north-star ──
# A commit that breaks a game's compile must fail CI (the module gate alone never
# caught that). The 6 sokol games need a GPU/window so they are compile-gated
# only; VOIDBORNE runs fully headless so its --selftest/--autodemo result markers
# are asserted (the one game whose behaviour CI can verify without a display).
$gameCount = 0
if (-not $SkipGames) {
    $gameTargets = @("flappy", "knockdown", "platformer", "mario", "mario3d", "harvest", "voidborne")
    foreach ($g in $gameTargets) {
        Write-Host "[*] Building game $g ..." -ForegroundColor Yellow
        Invoke-Dev "cmake --build `"$buildPath`" --target $g"
        if ($script:devExit -ne 0) {
            $failures += "game:$g"; $summary += "  game:{0,-10} BUILD FAILED" -f $g; continue
        }
        if (-not (Test-Path (Join-Path $buildPath "bin\$g.exe"))) {
            $failures += "game:$g"; $summary += "  game:{0,-10} EXE MISSING" -f $g; continue
        }
        $summary += "  game:{0,-10} BUILD OK" -f $g
        $gameCount++
    }
    # VOIDBORNE is the one game that runs headless (its demo returns before the
    # window loop), so assert its result markers.
    $binDir = Join-Path $buildPath "bin"
    if (Test-Path (Join-Path $binDir "voidborne.exe")) {
        Push-Location $binDir
        foreach ($probe in @(
            @{ tag = "--selftest"; pass = "VOIDBORNE DATA OK" },
            @{ tag = "--autodemo"; pass = "VOIDBORNE M0-M7 OK" })) {
            Remove-Item "voidborne_result.txt" -ErrorAction SilentlyContinue
            & ".\voidborne.exe" $probe.tag | Out-Null
            $txt = if (Test-Path "voidborne_result.txt") { Get-Content "voidborne_result.txt" -Raw } else { "" }
            if ($txt -like "*$($probe.pass)*") {
                $summary += "  voidborne {0,-11} OK" -f $probe.tag
            } else {
                $failures += "voidborne $($probe.tag)"
                $summary += "  voidborne {0,-11} MARKER FAILED" -f $probe.tag
            }
        }
        Pop-Location
    }
}

Write-Host ""
Write-Host "=== Test gate summary ===" -ForegroundColor Cyan
$summary | ForEach-Object { Write-Host $_ }
Write-Host ""

if ($failures.Count -gt 0) {
    Write-Host "[X] FAILED: $($failures -join ', ')" -ForegroundColor Red
    exit 1
}
$gamesMsg = if ($SkipGames) { "(games skipped)" } else { "+ $gameCount games compiled, VOIDBORNE autodemo/selftest green" }
Write-Host "[OK] All $($targets.Count) module test suites passed $gamesMsg." -ForegroundColor Green
exit 0
