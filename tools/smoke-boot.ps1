# Boot smoke test: starts the emulator on the rig's game for a while, then checks that every
# seamless co-op patch and hook went into the running game (tools\re\check_party_log.py --boot).
# Needs no player: the patches go in while the game boots. Refuses to run next to a running game.
# Usage: smoke-boot.ps1 [-Role host|guest] [-Seconds 40] [-Exe <shadPS4.exe>] [-Game <eboot.bin>]
#        smoke-boot.ps1 -Engine port [-Role host|guest] [-Seconds 40]   (the native port)
param(
    [ValidateSet("host", "guest")][string]$Role = "host",
    [int]$Seconds = 40,
    [string]$Exe,
    [string]$Game,
    [ValidateSet("emulator", "port")][string]$Engine = "emulator"
)
# The native port (Bloodborne PC) boots through the rig's runner, which knows its folders and
# settings (tools\rig\engines.py).
if ($Engine -eq "port") {
    py -3.12 (Join-Path $PSScriptRoot "rig\run_scenario.py") --engine port --boot-check $Role --seconds $Seconds
    exit $LASTEXITCODE
}
. "$PSScriptRoot\env.ps1"
if (-not $Exe) { $Exe = Join-Path $env:BB_ROOT "out\emulator\build\shadPS4.exe" }
if (-not $Game) { $Game = Join-Path $env:BB_ROOT "rig\game\CUSA03173\eboot.bin" }
$dir = Join-Path $env:BB_ROOT "rig\$Role"
if (Get-Process shadps4 -ErrorAction SilentlyContinue) {
    Write-Host "A game is running; not starting another."
    exit 2
}
$log = Join-Path $dir "user\log\shad_log.txt"
if (Test-Path $log) { Copy-Item $log (Join-Path $dir "user\log\shad_log.before-smoke.txt") -Force }

# As the rig launchers, in a party of its own so it never meets a real one.
$env:SHADPS4_BLOODBORNE_SEAMLESS_COOP = "1"
$env:SHADPS4_BLOODBORNE_PARTY = "smoke:boot"
$env:SHADPS4_BLOODBORNE_PARTY_ROLE = $Role
$env:SHADPS4_P2P_PORT = if ($Role -eq "host") { "51600" } else { "51601" }
$env:SHADPS4_BLOODBORNE_RE_TRACE = "1"

# shadPS4 starts the game in a process of its own, so every shadPS4 started from here on is
# stopped (none ran before: see above).
$started = Get-Date
Start-Process -FilePath $Exe -ArgumentList "-g", "`"$Game`"" -WorkingDirectory $dir | Out-Null
Start-Sleep -Seconds $Seconds
Get-Process shadps4 -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -ge $started } |
    Stop-Process -Force
py -3.12 (Join-Path $PSScriptRoot "re\check_party_log.py") --boot $log
exit $LASTEXITCODE
