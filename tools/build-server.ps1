# Builds shadnet (server/) and its tests, outside the source tree.
param(
    [string]$Type = "Release",
    [string]$Src,
    [string]$Out
)
. "$PSScriptRoot\env.ps1"
if (-not $Src) { $Src = Join-Path $env:BB_ROOT "bloodborne-server\server" }
if (-not $Out) { $Out = Join-Path $env:BB_ROOT "out\server" }
cmake -S $Src -B $Out -G Ninja "-DCMAKE_BUILD_TYPE=$Type" -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl 2>&1 |
    Select-Object -Last 1
$log = Join-Path $Out "build.log"
cmake --build $Out --parallel 24 --target all test_bloodborne_summon_broker *> $log
if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED (log: $log)"
    Select-String -Path $log -Pattern "error:|FAILED" | Select-Object -First 20 | ForEach-Object Line
    exit 1
}
Get-ChildItem (Join-Path $Out "shadnet.exe") | Select-Object FullName, Length
