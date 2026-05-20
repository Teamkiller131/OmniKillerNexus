# scripts/build.ps1
# 构建指定模块 / SDK / 全量（默认 Ninja）
param(
    [string]$Module = "",          # 单模块: okn-core, okn-math ...
    [string]$Sdk = "",             # SDK: client, server, editor
    [switch]$All,                  # 全量编译
    [string]$Config = "Debug",     # Debug, Release, RelWithDebInfo
    [int]$Jobs = 16,               # Ninja 并行数
    [switch]$Clean                 # 先 clean 再编译
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build_dir = "$root\build"

function Show-Help {
    Write-Host @"
Usage: build.ps1 [options]

Options:
  -Module <name>   Build single module (okn-core, okn-math, okn-render ...)
  -Sdk <name>      Build SDK (client, server, editor)
  -All             Build all targets
  -Config <name>   Build configuration (Debug, Release, RelWithDebInfo)
  -Jobs <N>        Ninja parallel jobs (default: 16)
  -Clean           Clean build directory first

Examples:
  .\scripts\build.ps1 -Module okn-math
  .\scripts\build.ps1 -Sdk client
  .\scripts\build.ps1 -All -Config Release -Jobs 32
  .\scripts\build.ps1 -Module okn-core -Clean
"@
    exit 0
}

if (-not $Module -and -not $Sdk -and -not $All) { Show-Help }

# 确定目标
if ($Module) {
    $target = $Module
} elseif ($Sdk) {
    $target_map = @{ client = "okn-client-sdk"; server = "okn-server-sdk"; editor = "okn-editor-sdk" }
    $target = $target_map[$Sdk]
    if (-not $target) { Write-Host "[ERROR] Unknown SDK: $Sdk" -ForegroundColor Red; exit 1 }
} else {
    $target = ""
}

Write-Host "=== OmniKillerNexus Build (Ninja) ===" -ForegroundColor Cyan
Write-Host "  Target: $(if ($All) { 'ALL' } else { $target })"
Write-Host "  Config: $Config | Jobs: $Jobs"
Write-Host ""

# 如果 build 目录不存在，先 configure
if (-not (Test-Path "$build_dir\build.ninja")) {
    Write-Host "[*] Build dir not configured. Running configure..." -ForegroundColor Yellow
    & "$PSScriptRoot\configure.ps1" -Config $Config -BuildDir "build"
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

# Clean
if ($Clean) {
    Write-Host "[*] Cleaning build..." -ForegroundColor Yellow
    & cmake --build $build_dir --target clean
}

# 构建 (Ninja)
$build_args = @("--build", $build_dir)
if ($target) { $build_args += "--target", $target }
$build_args += "-j", $Jobs

Write-Host "[*] Building..." -ForegroundColor Yellow
& cmake @build_args

if ($LASTEXITCODE -eq 0) {
    Write-Host "[OK] Build succeeded" -ForegroundColor Green
    Write-Host "  lib/ → $build_dir\lib"
    Write-Host "  bin/ → $build_dir\bin"
} else {
    Write-Host "[FAIL] Build failed (exit code: $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}
