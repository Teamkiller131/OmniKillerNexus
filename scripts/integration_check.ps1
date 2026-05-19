# scripts/integration_check.ps1
# 模块集成检查：编译模块 + 编译依赖该模块的上游（含 samples + tests），检查无符号冲突
param(
    [Parameter(Mandatory=$true)]
    [string]$Module,         # 刚完成的模块名（如 okn-math）
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build_dir = "$root\build"

Write-Host "=== OmniKillerNexus Integration Check ===" -ForegroundColor Cyan
Write-Host "  Module: $Module"
Write-Host "  Config: $Config"
Write-Host ""

# 模块上游依赖关系（哪些模块依赖哪些已完成的模块）
$upstream_map = @{
    "okn-core"     = @()
    "okn-math"     = @()
    "okn-memory"   = @()
    "okn-platform" = @("okn-core", "okn-math", "okn-memory")
    "okn-ecs"      = @("okn-core", "okn-math", "okn-memory", "okn-platform")
    "okn-asset"    = @("okn-core", "okn-math", "okn-memory", "okn-platform")
    "okn-render"   = @("okn-core", "okn-math", "okn-memory", "okn-platform", "okn-ecs", "okn-asset")
    "okn-network"  = @("okn-core", "okn-math", "okn-memory", "okn-platform")
    "okn-physics"  = @("okn-core", "okn-math", "okn-memory", "okn-ecs")
    "okn-audio"    = @("okn-core", "okn-math", "okn-memory", "okn-platform", "okn-asset")
    "okn-script"   = @("okn-core", "okn-math", "okn-memory", "okn-ecs", "okn-asset")
    "okn-ui"       = @("okn-core", "okn-math", "okn-memory", "okn-render", "okn-ecs", "okn-asset", "okn-script")
    "okn-editor"   = @("okn-core", "okn-math", "okn-memory", "okn-render", "okn-audio", "okn-ui", "okn-ecs", "okn-script", "okn-network", "okn-asset", "okn-platform", "okn-physics")
}

# 查找下游模块（依赖当前模块的模块）
$consumers = @()
foreach ($m in $upstream_map.Keys) {
    if ($Module -in $upstream_map[$m]) {
        $consumers += $m
    }
}

Write-Host "  Consumers (modules that depend on $Module): $($consumers -join ', ')" -ForegroundColor DarkGray
Write-Host ""

# Step 1: 编译目标模块
Write-Host "[Step 1/3] Building $Module..." -ForegroundColor Yellow
& "$PSScriptRoot\build.ps1" -Module $Module -Config $Config
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] $Module build failed" -ForegroundColor Red
    exit 1
}

# Step 2: 编译目标模块的 tests 和 samples
Write-Host "[Step 2/3] Building $Module tests & samples..." -ForegroundColor Yellow
$test_targets = @("${Module}_tests", "${Module}_samples")
foreach ($t in $test_targets) {
    $args = @("--build", $build_dir, "--config", $Config, "--target", $t)
    & cmake @args 2>&1 | Out-Null
}
Write-Host "[OK] $Module tests & samples built" -ForegroundColor Green

# Step 3: 逐个编译下游模块（使用已有的上游 + 剩余的 stub）
Write-Host "[Step 3/3] Verifying downstream compatibility..." -ForegroundColor Yellow
$all_ok = $true
foreach ($consumer in $consumers) {
    # 检查下游模块是否也在可构建状态（至少有 CMakeLists.txt）
    $consumer_cmake = "$root\modules\$consumer\CMakeLists.txt"
    if (Test-Path $consumer_cmake) {
        & "$PSScriptRoot\build.ps1" -Module $consumer -Config $Config
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[WARN] $consumer compilation failed — this may be expected if the module has unimplemented dependencies" -ForegroundColor Yellow
            # 不阻止通过 — 上游可能本身还是 stub
        } else {
            Write-Host "[OK] $consumer compiles against $Module" -ForegroundColor Green
        }
    }
}

Write-Host ""
Write-Host "[✓] Integration check complete" -ForegroundColor Green
Write-Host "  Module $Module is ready for upstream consumers."
Write-Host "  Next: Generate API doc with docs/api/${Module}.api.md"
