# Builds a self-contained two-player test rig on one PC:
#   <Rig>\server   shadnet (our build) on private ports, seamless co-op on, own DB
#   <Rig>\backend  ss.info/content backend (LocalServer's bb_dummy_server.py) on 127.0.0.1
#   <Rig>\host, <Rig>\guest   one shadPS4 working dir each, with its own portable `user` dir
# The game files and saves are copied from the live install; the live install is never modified.
param(
    [string]$Rig = "C:\bb\rig",
    [string]$Install = "C:\Bloodborne.PC.Online",
    [int]$GamePort = 32313,          # shadnet TCP; UDP matching = +1, WebAPI = +2
    [int]$BackendPort = 20453,
    [string]$Party = "rig:hunters",  # both players share it; auto-rejoin needs a party
    [switch]$Trace,                  # SHADPS4_BLOODBORNE_RE_TRACE for reverse engineering
    [switch]$Force
)
$ErrorActionPreference = "Stop"
if (-not $env:BB_ROOT) { $env:BB_ROOT = "C:\bb" }
# shadPS4 reads these with a JSON parser; write UTF-8 without the BOM Windows PowerShell adds.
function Write-Utf8([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding $false)) }
$matchPort = $GamePort + 1
$webPort = $GamePort + 2
if ((Test-Path $Rig) -and -not $Force) { throw "$Rig exists; pass -Force to rebuild it" }
New-Item -ItemType Directory -Force "$Rig\server", "$Rig\backend" | Out-Null

# --- server -------------------------------------------------------------------------------
$shadnet = Join-Path $env:BB_ROOT "out\server\shadnet.exe"
Copy-Item $shadnet "$Rig\server\" -Force
# Qt runtime matching our build (windeployqt warns on stderr about VCINSTALLDIR; harmless).
cmd /c "`"C:\Qt\6.8.3\msvc2022_64\bin\windeployqt6.exe`" --release --no-translations `"$Rig\server\shadnet.exe`" >nul 2>nul"
if (-not (Test-Path "$Rig\server\Qt6Core.dll")) { throw "windeployqt failed for $Rig\server\shadnet.exe" }
New-Item -ItemType Directory -Force "$Rig\server\clientsample" | Out-Null
Copy-Item "$Install\DATA\shadNet-P2P\clientsample\*" "$Rig\server\clientsample\" -Recurse -Force
@"
[General]
Host=127.0.0.1
UnsecuredPort=$GamePort
MatchingUdpPort=$matchPort
WebApiPort=$webPort
StatsEnabled=false
Matching2Enabled=true
BloodborneSeamlessCoop=true
EmailValidated=false
AdminsList=
RegistrationSecretKey=
"@ | Set-Content "$Rig\server\shadnet.cfg" -Encoding ascii

# --- backend ------------------------------------------------------------------------------
Copy-Item "$PSScriptRoot\..\backend\*.py", "$PSScriptRoot\..\backend\ss_info_template.xml" "$Rig\backend\" -Force
"" | Set-Content "$Rig\backend\notice.txt"

# --- players ------------------------------------------------------------------------------
$players = @(
    @{ Name = "host";  NpId = "rig_host";  P2P = 51600 },
    @{ Name = "guest"; NpId = "rig_guest"; P2P = 51601 }
)
foreach ($p in $players) {
    $dir = "$Rig\$($p.Name)"
    New-Item -ItemType Directory -Force $dir | Out-Null
    robocopy "$Install\DATA\user" "$dir\user" /E /XD log shader cache captures screenshots temp /NFL /NDL /NJH /NJS /NP | Out-Null
    $users = Get-Content "$dir\user\users.json" -Raw | ConvertFrom-Json
    $u = $users.Users.user[0]
    $u.shadnet_npid = $p.NpId; $u.shadnet_password = "rig"; $u.user_name = $p.NpId; $u.shadnet_token = ""
    Write-Utf8 "$dir\user\users.json" ($users | ConvertTo-Json -Depth 10)
    $cfgPath = "$dir\user\custom_configs\CUSA03173.json"
    $cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
    $cfg.General.shadnet_server = "127.0.0.1:$GamePort"
    $cfg.General.shadnet_webapi_server = "http://127.0.0.1:$webPort"
    $cfg.GPU.full_screen = $false
    $cfg.GPU.window_width = 1280; $cfg.GPU.window_height = 720
    Write-Utf8 $cfgPath ($cfg | ConvertTo-Json -Depth 10)
    Write-Utf8 "$dir\user\host_overrides.json" (@{ "https://ss4.scej-network.jp:20443" = "http://127.0.0.1:$BackendPort" } | ConvertTo-Json)
    @"
@echo off
cd /d "%~dp0"
set SHADPS4_BLOODBORNE_SEAMLESS_COOP=1
set SHADPS4_P2P_PORT=$($p.P2P)
set SHADPS4_BLOODBORNE_PARTY=$Party
set SHADPS4_BLOODBORNE_PARTY_ROLE=$($p.Name)
set SHADPS4_BLOODBORNE_RE_TRACE=$(if ($Trace) { 1 } else { 0 })
start "" "$($env:BB_ROOT)\out\emulator\build\shadPS4.exe" -g "$Install\DATA\Bloodborne\CUSA03173\eboot.bin"
"@ | Set-Content "$dir\launch.bat" -Encoding ascii
}

@"
@echo off
cd /d "%~dp0server"
start "rig shadnet" shadnet.exe
cd /d "%~dp0backend"
start "rig backend" py -3.12 bb_dummy_server.py --address 127.0.0.1 --port $BackendPort --shadnet-webapi http://127.0.0.1:$webPort
"@ | Set-Content "$Rig\start-servers.bat" -Encoding ascii

@"
@echo off
cd /d "%~dp0server\clientsample"
.\shadnet-sample.exe 127.0.0.1 $GamePort register rig_host rig rig_host.rig@p2p.db
.\shadnet-sample.exe 127.0.0.1 $GamePort register rig_guest rig rig_guest.rig@p2p.db
"@ | Set-Content "$Rig\register-accounts.bat" -Encoding ascii

Write-Host "Rig ready at $Rig"
Write-Host "  1. start-servers.bat   2. register-accounts.bat (once)   3. host\launch.bat, guest\launch.bat"





