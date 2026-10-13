# Local mirror of the upstream CI checks, per component of the monorepo.
# Usage: ci-check.ps1 -Component emulator|server|backend|tools|bbport [-All] [-SkipBuild] [-Src <dir>]
#                    [-CompileDb <dir>]
#   default lints only files changed vs. trunk(); -All lints every source file like a branch push.
#   -CompileDb (emulator): also tools\re\code_health.py --check on the co-op sources, with the
#   compile_commands.json in <dir> (the port's: bbport\out after `ninja -C out/gpu -t compdb`).
param(
    [Parameter(Mandatory)][ValidateSet("emulator", "server", "backend", "tools", "bbport")][string]$Component,
    [switch]$All,
    [switch]$SkipBuild,
    [string]$Src,
    [string]$CompileDb
)
$ErrorActionPreference = "Continue"
. "$PSScriptRoot\env.ps1"
$scripts = "$env:APPDATA\Python\Python312\Scripts"
$clangFormat = "$scripts\clang-format.exe"
$reuse = "$scripts\reuse.exe"
if (-not $Src) { $Src = Join-Path $env:BB_ROOT "bloodborne-server\$Component" }
$out = Join-Path $env:BB_ROOT "out\$Component"
Set-Location $Src
$failed = @()

if ($Component -eq "tools") {
    # The RE and log tools (tools\re), the rig's runner and scenarios (tools\rig) and the release
    # check (tools\release): their own unit tests.
    foreach ($tests in @("re\tests", "rig\tests", "release\tests")) {
        py -3.12 -m unittest discover -s $tests 2>&1 | Select-Object -Last 4
        if ($LASTEXITCODE -ne 0) { Write-Host "CI CHECK FAILED: $tests"; exit 1 }
    }
    Write-Host "CI CHECK PASSED"
    exit 0
}

if ($Component -eq "bbport") {
    # The native port (Bloodborne PC): upstream lints neither format nor licenses and runs no tests
    # on Windows; its build (MSYS2 CLANG64) and the co-op tests (guest code hooks, the rig's log).
    if ($SkipBuild) { Write-Host "CI CHECK PASSED (nothing to lint)"; exit 0 }
    & "$PSScriptRoot\build-bbport.ps1" -Src $Src -Test
    if ($LASTEXITCODE -ne 0) { Write-Host "CI CHECK FAILED: bbport build or tests"; exit 1 }
    Write-Host "CI CHECK PASSED"
    exit 0
}

if ($Component -eq "backend") {
    # Python stand-in for the retail backend: no upstream CI, just its service tests. With a
    # built shadnet, test_parity also checks shadnet's own backend answers the same.
    $shadnet = Join-Path $env:BB_ROOT "out\server\shadnet.exe"
    if (-not $env:SHADNET_EXE -and (Test-Path $shadnet)) { $env:SHADNET_EXE = $shadnet }
    py -3.12 -m unittest discover -s tests 2>&1 | Select-Object -Last 4
    if ($LASTEXITCODE -ne 0) { Write-Host "CI CHECK FAILED: backend tests"; exit 1 }
    Write-Host "CI CHECK PASSED"
    exit 0
}

# 1. clang-format 19 (both upstream CIs pin clang-format-19)
$fmtVersion = & $clangFormat --version
if ($fmtVersion -notmatch "version 19\.") { Write-Host "wrong clang-format: $fmtVersion"; exit 2 }
if ($All) {
    $files = Get-ChildItem src -Recurse -Include *.cpp, *.h | ForEach-Object { $_.FullName }
} else {
    # jj prints paths relative to the cwd, so ask from the repo root (emulator/src/...) and keep
    # this component's sources.
    $repoRoot = Split-Path $Src -Parent
    Push-Location $repoRoot
    $changed = jj diff --name-only -r "trunk()..@" 2>$null
    Pop-Location
    $files = @($changed |
        Where-Object { $_ -match "^$Component[\\/](src|tests)[\\/].*\.(cpp|h)$" } |
        ForEach-Object { Join-Path $repoRoot $_ } |
        Where-Object { Test-Path $_ })
}
$fmtBad = @()
foreach ($f in $files) {
    & $clangFormat --dry-run --Werror $f 2>$null
    if ($LASTEXITCODE -ne 0) { $fmtBad += $f }
}
if ($fmtBad) { $failed += "clang-format"; Write-Host "clang-format violations:"; $fmtBad | ForEach-Object { "  $_" } }
else { Write-Host "clang-format: ok ($(@($files).Count) files)" }

# 2. trailing whitespace (emulator CI script), on the same files as clang-format. Upstream main
#    already has a few offenders, so -All would flag code we never touched. CR is stripped first
#    because Windows checkouts may use CRLF while CI runs on LF.
if ($Component -eq "emulator") {
    $ws = foreach ($f in $files) {
        $n = 0
        foreach ($line in [IO.File]::ReadAllLines($f)) {
            $n++
            if ($line.TrimEnd("`r") -match '\s$') { "${f}:$n"; break }
        }
    }
    if ($ws) { $failed += "trailing-whitespace"; $ws | Select-Object -First 10 | ForEach-Object { "  $_" } }
    else { Write-Host "trailing whitespace: ok" }
}

# 3. REUSE licensing (each component keeps its own LICENSES/ and REUSE.toml). Upstream CI lints a
#    checkout without submodule contents; inside the monorepo reuse can't tell they are submodules,
#    so lint a copy of just this component's tracked files (jj's view, including uncommitted edits).
$repoRoot = Split-Path $Src -Parent
$lintDir = Join-Path $env:TEMP "bb-reuse-$Component"
Remove-Item -Recurse -Force $lintDir -ErrorAction SilentlyContinue
Push-Location $repoRoot
foreach ($rel in (jj file list $Component 2>$null)) {
    $dst = Join-Path $lintDir ($rel.Substring($Component.Length + 1))
    New-Item -ItemType Directory -Force (Split-Path $dst -Parent) | Out-Null
    Copy-Item (Join-Path $repoRoot $rel) $dst
}
Pop-Location
& $reuse --root $lintDir lint -q
if ($LASTEXITCODE -ne 0) { $failed += "reuse"; & $reuse --root $lintDir lint --lines | Select-Object -First 25 }
else { Write-Host "reuse: ok" }

# 3b. code health (optional, needs a compile database): no new function over cognitive 25,
#     nesting 4 or 120 lines, and no function over them getting worse than its baseline.
if ($Component -eq "emulator" -and $CompileDb) {
    py -3.12 "$PSScriptRoot\re\code_health.py" -p $CompileDb --root (Split-Path $Src -Parent) `
        --check --worst 0 2>&1 | Select-Object -Last 12
    if ($LASTEXITCODE -ne 0) { $failed += "code-health" }
}

# 4. build + tests
if (-not $SkipBuild) {
    if ($Component -eq "emulator") {
        & "$PSScriptRoot\build-emu.ps1" -Src $Src -Out $out
        if ($LASTEXITCODE -ne 0) { $failed += "build" }
        # The game-file checks (test_bloodborne_eboot.cpp) need the game's eboot; the rig has one.
        $eboot = Join-Path $env:BB_ROOT "rig\game\CUSA03173-patch\eboot.bin"
        if (-not $env:BLOODBORNE_EBOOT -and (Test-Path $eboot)) { $env:BLOODBORNE_EBOOT = $eboot }
        ctest --test-dir (Join-Path $out "tests") --output-on-failure -E GcnTest 2>&1 | Select-Object -Last 15
        if ($LASTEXITCODE -ne 0) { $failed += "ctest" }
    } else {
        & "$PSScriptRoot\build-server.ps1" -Src $Src -Out $out
        if ($LASTEXITCODE -ne 0) { $failed += "build" }
        ctest --test-dir $out --output-on-failure 2>&1 | Select-Object -Last 15
        if ($LASTEXITCODE -ne 0) { $failed += "ctest" }
    }
}

if ($failed) { Write-Host "CI CHECK FAILED: $($failed -join ', ')"; exit 1 }
Write-Host "CI CHECK PASSED"

