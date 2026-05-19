# scripts/format.ps1
# 一键格式化所有源文件（基于 .clang-format）
param(
    [string]$Module = ""     # 可选：限定格式化范围
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$clang_format = (Get-Command clang-format -ErrorAction SilentlyContinue).Source

if (-not $clang_format) {
    Write-Host "[ERROR] clang-format not found in PATH" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path "$root\.clang-format")) {
    Write-Host "[ERROR] .clang-format not found in project root" -ForegroundColor Red
    exit 1
}

Write-Host "=== OmniKillerNexus Format ===" -ForegroundColor Cyan

# 收集文件
if ($Module) {
    $module_dir = "$root\modules\$Module"
    if (-not (Test-Path $module_dir)) {
        Write-Host "[ERROR] Module not found: $module_dir" -ForegroundColor Red
        exit 1
    }
    $search_path = $module_dir
} else {
    $search_path = $root
}

$files = Get-ChildItem -Recurse -LiteralPath $search_path -Include "*.hpp", "*.cpp" |
         Where-Object { $_.FullName -notmatch "\\build\\" -and $_.FullName -notmatch "\\vcpkg_installed\\" }

$count = 0
foreach ($f in $files) {
    & $clang_format -i -style=file $f.FullName
    $count++
}

Write-Host "[OK] Formatted $count files" -ForegroundColor Green
