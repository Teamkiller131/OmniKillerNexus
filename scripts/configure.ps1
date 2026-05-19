# scripts/configure.ps1
# 一键初始化：安装 vcpkg 依赖 + CMake 配置
param(
    [string]$Config = "Debug",
    [string]$Generator = "Ninja",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$vcpkg_toolchain = "D:/vcpkg/scripts/buildsystems/vcpkg.cmake"

Write-Host "=== OmniKillerNexus Configure ===" -ForegroundColor Cyan

# 检查 vcpkg toolchain
if (-not (Test-Path $vcpkg_toolchain)) {
    Write-Host "[ERROR] vcpkg toolchain not found at: $vcpkg_toolchain" -ForegroundColor Red
    Write-Host "  Install vcpkg to D:\vcpkg or update the path in this script."
    exit 1
}
Write-Host "[OK] vcpkg toolchain found" -ForegroundColor Green

# 检查 vcpkg.json
if (-not (Test-Path "$root\vcpkg.json")) {
    Write-Host "[ERROR] vcpkg.json not found in project root" -ForegroundColor Red
    exit 1
}

# CMake 配置
Write-Host "[*] Running CMake configure (${Config}, ${Generator})..." -ForegroundColor Yellow
$cmake_args = @(
    "-S", $root,
    "-B", "$root\$BuildDir",
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkg_toolchain"
)

& cmake @cmake_args
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] CMake configure failed" -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Configure complete. Build dir: $root\$BuildDir" -ForegroundColor Green
Write-Host "  Next: .\scripts\build.ps1 -Module okn-core -Config $Config"
