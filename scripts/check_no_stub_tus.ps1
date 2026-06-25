# scripts/check_no_stub_tus.ps1
#
# Inventory (and optionally gate) the "placeholder halos": near-empty .cpp
# translation units that carry NO real code — only comments, includes, a BOM, or
# bare namespace scaffolding. These are the honesty debt ROADMAP v3 P11 prunes
# (e.g. the native okn-render backend and okn-network carry dozens each).
#
# Report-only by default (exit 0) so it can run informationally before the prune.
# Pass -Strict to exit non-zero when any stub is found — wire that into the gate
# AFTER P11 deletes/justifies the halos, so they can't creep back.
#
# Heuristic: a TU is a "stub" if, after stripping blank lines, line comments,
# /*...*/ blocks, preprocessor lines, and lone brace/namespace/using scaffolding,
# ZERO lines of real code remain. Tests and build trees are excluded.
#
# CAVEAT: this counts ALL near-empty .cpp, which includes *legitimate* header-first
# empties (okn-math/memory/ecs carry real, header-only impls). So the report is a
# candidate INVENTORY, not a verdict — the bulk concentrates in the known halos
# (okn-render native lib, okn-network, okn-editor). To turn -Strict into a clean
# gate after P11, add a baseline allowlist of the legit header-first empties and
# fail only on additions beyond it (or teach this to check CMake target membership).
#
# Usage:
#   pwsh scripts/check_no_stub_tus.ps1            # inventory, always exit 0
#   pwsh scripts/check_no_stub_tus.ps1 -Strict    # exit 1 if any stub TU exists
#   pwsh scripts/check_no_stub_tus.ps1 -List      # also print every stub path

param(
    [switch]$Strict,
    [switch]$List,
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

function Test-IsStubTU([string]$path) {
    $code = 0
    $inBlock = $false
    foreach ($raw in [System.IO.File]::ReadAllLines($path)) {
        $line = $raw.Trim().TrimStart([char]0xFEFF)   # trim + drop any BOM
        if ($line -eq '') { continue }
        if ($inBlock) {
            if ($line -match '\*/') { $inBlock = $false }
            continue
        }
        if ($line.StartsWith('//')) { continue }
        if ($line.StartsWith('/*')) {
            if ($line -notmatch '\*/') { $inBlock = $true }
            continue
        }
        if ($line.StartsWith('#')) { continue }          # preprocessor / includes
        # lone scaffolding: braces, namespace open/close, using-declarations
        if ($line -match '^(namespace\b.*|\{|\}|\};|\}\s*//.*|using\b.*)$') { continue }
        $code++
    }
    return ($code -eq 0)
}

$modules = Join-Path $Root 'modules'
$all = Get-ChildItem -Path $modules -Recurse -Filter *.cpp -File |
    Where-Object { $_.FullName -notmatch '[\\/](build|build-phys|build-ci|out|vcpkg_installed|tests?)[\\/]' }

$stubs = @()
foreach ($f in $all) {
    if (Test-IsStubTU $f.FullName) { $stubs += $f }
}

# Group by okn-* module for the summary.
$byModule = @{}
foreach ($s in $stubs) {
    $rel = $s.FullName.Substring($modules.Length).TrimStart('\','/')
    $mod = ($rel -split '[\\/]')[0]
    if (-not $byModule.ContainsKey($mod)) { $byModule[$mod] = 0 }
    $byModule[$mod]++
}

Write-Host "=== placeholder-halo inventory (near-empty .cpp under modules/) ===" -ForegroundColor Cyan
Write-Host ("scanned {0} .cpp; {1} are stubs" -f $all.Count, $stubs.Count)
foreach ($mod in ($byModule.Keys | Sort-Object { -$byModule[$_] })) {
    Write-Host ("  {0,-16} {1,4}" -f $mod, $byModule[$mod])
}

if ($List) {
    Write-Host "--- stub files ---" -ForegroundColor DarkGray
    foreach ($s in $stubs) {
        Write-Host ("  modules/{0}" -f $s.FullName.Substring($modules.Length).TrimStart('\','/').Replace('\','/'))
    }
}

if ($Strict -and $stubs.Count -gt 0) {
    Write-Host ("[FAIL] {0} placeholder stub TU(s) — delete them or gate behind a committed consumer (ROADMAP P11)." -f $stubs.Count) -ForegroundColor Red
    exit 1
}
Write-Host "[OK] inventory complete." -ForegroundColor Green
exit 0
