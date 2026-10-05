# Local mirror of the CI checks for the Bloodborne co-op forks.
# Usage: ci-check.ps1 -Repo shadp2p|shadnet-p2p [-All] [-SkipBuild]
#   default lints only files changed vs. trunk(); -All lints every source file like a branch push.
param(
    [Parameter(Mandatory)][ValidateSet("shadp2p", "shadnet-p2p")][string]$Repo,
    [switch]$All,
    [switch]$SkipBuild
)
$ErrorActionPreference = "Continue"
. "$PSScriptRoot\env.ps1"
$scripts = "$env:APPDATA\Python\Python312\Scripts"
$clangFormat = "$scripts\clang-format.exe"
$reuse = "$scripts\reuse.exe"
Set-Location (Join-Path $env:BB_ROOT $Repo)
$failed = @()

# 1. clang-format 19 (CI pins clang-format-19)
$fmtVersion = & $clangFormat --version
if ($fmtVersion -notmatch "version 19\.") { Write-Host "wrong clang-format: $fmtVersion"; exit 2 }
if ($All) {
    $files = Get-ChildItem src -Recurse -Include *.cpp, *.h | ForEach-Object { $_.FullName }
} else {
    $files = jj diff --name-only -r "trunk()..@" 2>$null |
        Where-Object { $_ -match '^(src|tests)[\\/].*\.(cpp|h)$' -and (Test-Path $_) }
}
$fmtBad = @()
foreach ($f in $files) {
    & $clangFormat --dry-run --Werror $f 2>$null
    if ($LASTEXITCODE -ne 0) { $fmtBad += $f }
}
if ($fmtBad) { $failed += "clang-format"; Write-Host "clang-format violations:"; $fmtBad | ForEach-Object { "  $_" } }
else { Write-Host "clang-format: ok ($($files.Count) files)" }

# 2. trailing whitespace (shadp2p CI script)
if ($Repo -eq "shadp2p") {
    $ws = Get-ChildItem src -Recurse -File | Select-String -Pattern '\s$' -List | Select-Object -First 5
    if ($ws) { $failed += "trailing-whitespace"; $ws | ForEach-Object { "  $($_.Path):$($_.LineNumber)" } }
    else { Write-Host "trailing whitespace: ok" }
}

# 3. REUSE licensing
& $reuse lint -q
if ($LASTEXITCODE -ne 0) { $failed += "reuse"; & $reuse lint | Select-Object -Last 15 } else { Write-Host "reuse: ok" }

# 4. build + tests
if (-not $SkipBuild) {
    if ($Repo -eq "shadp2p") {
        cmake --build build --parallel 24 2>&1 | Select-String -Pattern "error:|FAILED" | Select-Object -First 20
        if ($LASTEXITCODE -ne 0) { $failed += "build" }
        ctest --test-dir build --output-on-failure -E GcnTest 2>&1 | Select-Object -Last 15
        if ($LASTEXITCODE -ne 0) { $failed += "ctest" }
    } else {
        cmake --build build --parallel 24 --target all test_bloodborne_summon_broker 2>&1 | Select-String -Pattern "error:|FAILED" | Select-Object -First 20
        if ($LASTEXITCODE -ne 0) { $failed += "build" }
        $env:Path = "C:\Qt\6.8.3\msvc2022_64\bin;" + $env:Path
        ctest --test-dir build --output-on-failure 2>&1 | Select-Object -Last 15
        if ($LASTEXITCODE -ne 0) { $failed += "ctest" }
    }
}

if ($failed) { Write-Host "CI CHECK FAILED: $($failed -join ', ')"; exit 1 }
Write-Host "CI CHECK PASSED"


