# scripts/lint.ps1
# 运行代码质量检查：clang-format + clang-tidy
param(
    [string]$Module = "",    # 限定检查范围（模块名）
    [switch]$Fix             # 自动修复（仅 clang-format）
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
$clang_format = (Get-Command clang-format -ErrorAction SilentlyContinue).Source
$clang_tidy = (Get-Command clang-tidy -ErrorAction SilentlyContinue).Source

Write-Host "=== OmniKillerNexus Lint ===" -ForegroundColor Cyan

# 检查工具是否存在
if (-not $clang_format) {
    Write-Host "[WARN] clang-format not found in PATH" -ForegroundColor Yellow
    $has_format = $false
} else {
    Write-Host "[OK] clang-format: $clang_format" -ForegroundColor Green
    $has_format = $true
}

if (-not $clang_tidy) {
    Write-Host "[WARN] clang-tidy not found in PATH" -ForegroundColor Yellow
    $has_tidy = $false
} else {
    Write-Host "[OK] clang-tidy: $clang_tidy" -ForegroundColor Green
    $has_tidy = $true
}

# 收集文件
if ($Module) {
    $module_dir = "$root\modules\$Module"
    if (-not (Test-Path $module_dir)) {
        Write-Host "[ERROR] Module not found: $module_dir" -ForegroundColor Red
        exit 1
    }
    $files = Get-ChildItem -Recurse -LiteralPath $module_dir -Include "*.hpp", "*.cpp" |
             Where-Object { $_.FullName -notmatch "\\build\\" -and $_.FullName -notmatch "\\vcpkg_installed\\" }
} else {
    $files = Get-ChildItem -Recurse -LiteralPath $root -Include "*.hpp", "*.cpp" |
             Where-Object { $_.FullName -notmatch "\\build\\" -and $_.FullName -notmatch "\\vcpkg_installed\\" }
}

$total = $files.Count
$pass = $true

# clang-format
if ($has_format) {
    Write-Host ""
    Write-Host "--- clang-format ($total files) ---" -ForegroundColor Cyan

    foreach ($f in $files) {
        $path = $f.FullName
        if ($Fix) {
            $result = & $clang_format -i -style=file $path 2>&1
            if ($LASTEXITCODE -ne 0) {
                Write-Host "  [ERR] $path : $result" -ForegroundColor Red
                $pass = $false
            }
        } else {
            $diff = & $clang_format --dry-run --Werror -style=file $path 2>&1
            if ($LASTEXITCODE -ne 0) {
                Write-Host "  [FMT] $path" -ForegroundColor Yellow
                $pass = $false
            }
        }
    }

    if ($pass) {
        Write-Host "[OK] All ${total} files pass clang-format" -ForegroundColor Green
    } else {
        if (-not $Fix) {
            Write-Host ""
            Write-Host "[*] Run '.\scripts\format.ps1' to auto-fix formatting issues." -ForegroundColor Yellow
        }
    }
}

# clang-tidy
if ($has_tidy -and (Test-Path "$root\build\compile_commands.json")) {
    Write-Host ""
    Write-Host "--- clang-tidy ---" -ForegroundColor Cyan
    $tidy_pass = $true

    foreach ($f in $files) {
        $path = $f.FullName
        $result = & $clang_tidy -p="$root\build" $path 2>&1
        if ($LASTEXITCODE -ne 0 -or $result -match "warning:|error:") {
            Write-Host "  [TIDY] $path" -ForegroundColor Yellow
            Write-Host $result
            $tidy_pass = $false
        }
    }

    if ($tidy_pass) {
        Write-Host "[OK] All files pass clang-tidy" -ForegroundColor Green
    } else {
        $pass = $false
    }
}

# 最终结果
Write-Host ""
if ($pass) {
    Write-Host "[✓] Lint passed" -ForegroundColor Green
    exit 0
} else {
    Write-Host "[✗] Lint failed" -ForegroundColor Red
    exit 1
}
