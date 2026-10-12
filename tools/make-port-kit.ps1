# Builds the seamless co-op kit for the native port (Bloodborne PC) that players unzip into their
# Bloodborne.PC.Online folder: <Out>\SeamlessCoopPort (the port's package with the kit's scripts,
# the co-op servers and the map event files) plus the "... Port Co-op" launchers beside it, and
# <Out>.zip of both.
#   -Package  the port's package (bbport\packaging\windows\package.sh: out\stage\bbport-windows)
#   -Servers  a shadPS4 co-op kit to take the servers and event files from (its server, backend and
#             event-patch folders), so both kits play on the same servers
# Usage: make-port-kit.ps1 [-Package <dir>] [-Servers <dir>] [-Out <dir>]
param(
    [string]$Package,
    [string]$Servers,
    [string]$Out
)
$ErrorActionPreference = "Stop"
if (-not $env:BB_ROOT) { $env:BB_ROOT = "C:\bb" }
if (-not $Package) { $Package = Join-Path $env:BB_ROOT "bloodborne-server\bbport\out\stage\bbport-windows" }
if (-not $Servers) { $Servers = Join-Path $env:BB_ROOT "online-kit" }
if (-not $Out) { $Out = Join-Path $env:BB_ROOT "port-kit" }
$source = Join-Path $PSScriptRoot "port-kit"
foreach ($need in "$Package\BLauncher.exe", "$Package\bin\bb-probe.exe", "$Servers\server\shadnet.exe",
                  "$Servers\backend\bb_dummy_server.py", "$Servers\event-patch\manifest.json") {
    if (-not (Test-Path $need)) { throw "Missing $need" }
}
# The event files must be the ones tools\re\party_boss_events.py wrote and checked (its
# manifest.json lists each with its hash), not an older or hand-edited copy.
$events = Join-Path $Servers "event-patch"
$manifest = Get-Content -Raw (Join-Path $events "manifest.json") | ConvertFrom-Json
$listed = @($manifest.files.PSObject.Properties.Name)
foreach ($file in Get-ChildItem -File $events -Filter *.emevd.dcx) {
    if ($file.Name -notin $listed) { throw "$($file.FullName) is not in the event patch manifest" }
}
foreach ($name in $listed) {
    $path = Join-Path $events $name
    if (-not (Test-Path $path)) { throw "Missing $path (in the event patch manifest)" }
    $hash = (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant()
    if ($hash -ne $manifest.files.$name.output_sha256) {
        throw "$path is not the file the event patch manifest lists (sha256 $hash)"
    }
}

# A fresh kit each time: what a player makes in theirs (user, game, settings.bat) is never here.
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
$kit = Join-Path $Out "SeamlessCoopPort"
New-Item -ItemType Directory -Force $kit | Out-Null
robocopy $Package $kit /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "copying the package failed" }
foreach ($dir in "server", "backend", "event-patch") {
    robocopy (Join-Path $Servers $dir) (Join-Path $kit $dir) /E /XF *.db *.log /XD __pycache__ /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "copying $dir failed" }
}

# The kit's own files. cmd needs CRLF in batch files (labels and goto break on LF), and the
# player reads the text files in Notepad.
function Copy-Crlf([string]$From, [string]$To) {
    $text = [IO.File]::ReadAllText($From) -replace "`r`n", "`n" -replace "`n", "`r`n"
    New-Item -ItemType Directory -Force (Split-Path -Parent $To) | Out-Null
    [IO.File]::WriteAllText($To, $text, (New-Object Text.UTF8Encoding $false))
}
foreach ($file in Get-ChildItem -Recurse -File $source) {
    $relative = $file.FullName.Substring($source.Length + 1)
    $target = if ($relative -like "root\*") { Join-Path $Out $relative.Substring(5) } else { Join-Path $kit $relative }
    Copy-Crlf $file.FullName $target
}

$zip = "$Out.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $Out "*") -DestinationPath $zip
$size = [math]::Round((Get-Item $zip).Length / 1MB)
Write-Host "Port co-op kit: $Out ($zip, $size MB)"
