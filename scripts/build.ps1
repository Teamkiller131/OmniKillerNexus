# scripts/build.ps1
# 构建指定模块 / SDK / 全量
param(
    [string]$Module = "",          # 单模块: okn-core, okn-math, okn-render ...
    [string]$Sdk = "",             # SDK: client, server, editor
    [switch]$All,                  # 全量编译
    [string]$Config = "Debug",     # Debug, Release, RelWithDebInfo
    [string]$Generator = "Ninja",  # Ninja, "Visual Studio 18 2026"
    [string]$Compiler = "msvc",    # msvc, clang-cl
    [switch]$Clean                 # 先 clean 再编译
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build_dir = "$root\build"

# 帮助
function Show-Help {
    Write-Host @"
Usage: build.ps1 [options]

Options:
  -Module <name>   Build single module (e.g. okn-core, okn-math)
  -Sdk <name>      Build SDK (client, server, editor)
  -All             Build all targets
  -Config <name>   Build configuration (Debug, Release, RelWithDebInfo)
  -Generator <G>   CMake generator (Ninja, "Visual Studio 18 2026")
  -Compiler <C>    msvc (default) or clang-cl
  -Clean           Clean build directory first

Examples:
  .\scripts\build.ps1 -Module okn-math -Config Release
  .\scripts\build.ps1 -Sdk client -Config Debug
  .\scripts\build.ps1 -All -Config Debug -Compiler clang-cl
  .\scripts\build.ps1 -Module okn-core -Clean -Config Release
"@
    exit 0
}

if (-not $Module -and -not $Sdk -and -not $All) { Show-Help }

# 确定目标
if ($Module) {
    $target = $Module
} elseif ($Sdk) {
    $target_map = @{
        client = "okn-client-sdk"
        server = "okn-server-sdk"
        editor = "okn-editor-sdk"
    }
    $target = $target_map[$Sdk]
    if (-not $target) { Write-Host "[ERROR] Unknown SDK: $Sdk (use: client, server, editor)" -ForegroundColor Red; exit 1 }
} else {
    $target = ""
}

Write-Host "=== OmniKillerNexus Build ===" -ForegroundColor Cyan
Write-Host "  Target:   $(if ($All) { 'ALL' } else { $target })"
Write-Host "  Config:   $Config"
Write-Host "  Compiler: $Compiler"
Write-Host ""

# 如果 build 目录不存在，先 configure
if (-not (Test-Path "$build_dir\CMakeCache.txt")) {
    Write-Host "[*] Build dir not configured. Running configure..." -ForegroundColor Yellow
    & "$PSScriptRoot\configure.ps1" -Config $Config -Generator $Generator -BuildDir "build"
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

# Clean
if ($Clean) {
    Write-Host "[*] Cleaning build..." -ForegroundColor Yellow
    & cmake --build $build_dir --config $Config --target clean
}

# 编译器工具链
$toolchain = ""
if ($Compiler -eq "clang-cl") {
    $toolchain = "-T", "ClangCL"
}

# 构建
$build_args = @("--build", $build_dir, "--config", $Config)
if ($target) { $build_args += "--target", $target }
if ($toolchain) { $build_args += $toolchain }

Write-Host "[*] Building..." -ForegroundColor Yellow
& cmake @build_args

if ($LASTEXITCODE -eq 0) {
    Write-Host "[OK] Build succeeded" -ForegroundColor Green
} else {
    Write-Host "[FAIL] Build failed (exit code: $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}
