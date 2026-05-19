# scripts/run_tests.ps1
# 运行 doctest 单测
param(
    [string]$Module = "",     # 限定模块
    [string]$Filter = "",     # doctest 测试名称过滤器
    [string]$Config = "Debug",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot

Write-Host "=== OmniKillerNexus Tests ===" -ForegroundColor Cyan
Write-Host "  Config: $Config"
if ($Module) { Write-Host "  Module: $Module" }
if ($Filter) { Write-Host "  Filter: $Filter" }

# 检查 build 目录
if (-not (Test-Path "$root\$BuildDir")) {
    Write-Host "[ERROR] Build directory not found. Run '.\scripts\build.ps1' first." -ForegroundColor Red
    exit 1
}

# 构建测试目标
$build_args = @("--build", "$root\$BuildDir", "--config", $Config)
if ($Module) {
    $build_args += "--target", "${Module}_tests"
} else {
    $build_args += "--target", "tests"
}

Write-Host "[*] Building test targets..." -ForegroundColor Yellow
& cmake @build_args 2>&1 | Out-Null

# 运行 ctest
$test_dir = "$root\$BuildDir"
if ($Module) {
    # 单模块：找模块的 test 目录
    $mod_test_dir = "$root\modules\$Module\tests"
    if (Test-Path $mod_test_dir) {
        $test_dir = "$root\$BuildDir\modules\$Module\tests"
    }
}

Write-Host "[*] Running ctest..." -ForegroundColor Yellow
$ctest_args = @("--test-dir", $test_dir, "--config", $Config, "--output-on-failure")
if ($Filter) { $ctest_args += "-R", $Filter }

$result = & ctest @ctest_args 2>&1
$exit_code = $LASTEXITCODE

# 输出摘要
Write-Host ""
if ($exit_code -eq 0) {
    Write-Host "[✓] All tests passed" -ForegroundColor Green
} else {
    Write-Host "[✗] Tests failed (exit code: $exit_code)" -ForegroundColor Red
    Write-Host $result
}

exit $exit_code
