# scripts/configure.ps1
# 一键初始化：CMake 配置（默认 Ninja）
param(
    [string]$Config = "Debug",
    [string]$Generator = "Ninja",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$vcpkg_toolchain = "D:/vcpkg/scripts/buildsystems/vcpkg.cmake"

Write-Host "=== OmniKillerNexus Configure (Ninja) ===" -ForegroundColor Cyan

# 检查 vcpkg toolchain
if (-not (Test-Path $vcpkg_toolchain)) {
    Write-Host "[ERROR] vcpkg toolchain not found at: $vcpkg_toolchain" -ForegroundColor Red
    exit 1
}
Write-Host "[OK] vcpkg toolchain" -ForegroundColor Green

# 检查 vcpkg.json
if (-not (Test-Path "$root\vcpkg.json")) {
    Write-Host "[ERROR] vcpkg.json not found" -ForegroundColor Red
    exit 1
}

# 激活 VS 编译环境 (Ninja + MSVC 需要)
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if (Test-Path $vcvars) {
    Write-Host "[OK] VS 2026 vcvars64" -ForegroundColor Green
} else {
    Write-Host "[WARN] vcvars64.bat not found, Ninja may fail without MSVC environment" -ForegroundColor Yellow
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

# Ninja 需要 vcvars 环境
if ($Generator -eq "Ninja" -and (Test-Path $vcvars)) {
    $cmd = "call `"$vcvars`" > nul && cmake $($cmake_args -join ' ')"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] CMake configure failed" -ForegroundColor Red
        exit 1
    }
} else {
    & cmake @cmake_args
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] CMake configure failed" -ForegroundColor Red
        exit 1
    }
}

Write-Host "[OK] Configure complete" -ForegroundColor Green
Write-Host "  Build: .\scripts\build.ps1 -All"
Write-Host "  Output: $root\$BuildDir\bin\  +  $root\$BuildDir\lib\"
