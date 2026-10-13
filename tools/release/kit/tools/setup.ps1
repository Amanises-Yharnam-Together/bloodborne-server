# Seamless co-op release (Bloodborne PC, the native port): asks the player for their settings,
# then prepares this folder from the player's own copy of the game. Nothing else is assumed: no
# emulator, no Python, no earlier co-op kit.
#   - coop-game\CUSA03173: the game with its 1.09 update over it, in one folder (the port reads
#     one), made of links to the player's game folders (nothing is copied)
#   - coop-game\CUSA03173-mods\dvdroot_ps4\event: the co-op map event files, made from the
#     player's own event files by tools\party_boss_events.py (run by BLauncher.exe, no Python)
#   - user\: saves (optionally copied from a folder once), the host's game server
# Answers are kept in settings.bat, which "2 - PLAY" and the HOST bats read. The player's game
# folders are never changed. Kept the same as tools\port-kit\tools\setup.ps1 where the two agree.
$ErrorActionPreference = "Stop"
$kit = Split-Path -Parent $PSScriptRoot
$settingsPath = Join-Path $kit "settings.bat"

function Write-Utf8([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding $false)) }

function Read-Settings([string]$Path) {
    $values = @{}
    if (Test-Path $Path) {
        foreach ($line in Get-Content $Path) {
            if ($line -match '^set "([A-Z0-9_]+)=(.*)"$') { $values[$Matches[1]] = $Matches[2] }
        }
    }
    return $values
}
$saved = Read-Settings $settingsPath

function Ask([string]$Prompt, [string]$Default, [scriptblock]$Valid, [string]$Hint) {
    while ($true) {
        $shown = if ($Default) { " [$Default]" } else { "" }
        $answer = (Read-Host "  $Prompt$shown").Trim().Trim('"')
        if ($answer -eq "") { $answer = $Default }
        if ($answer -and (& $Valid $answer)) { return $answer }
        Write-Host "    $Hint" -ForegroundColor Red
    }
}

# --- the game's param.sfo ------------------------------------------------------------------------
# PS4 param.sfo: "\0PSF", then the key table offset (+8), the data table offset (+12), the entry
# count (+16) and 16-byte entries from +20 (key offset u16, format u16, length u32, max u32,
# data offset u32). Returns a hashtable of the text and number values.
function Read-Sfo([string]$Path) {
    $values = @{}
    if (-not (Test-Path $Path)) { return $values }
    $b = [IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 20 -or $b[0] -ne 0 -or $b[1] -ne 0x50 -or $b[2] -ne 0x53 -or $b[3] -ne 0x46) { return $values }
    $keys = [BitConverter]::ToUInt32($b, 8); $data = [BitConverter]::ToUInt32($b, 12)
    $count = [BitConverter]::ToUInt32($b, 16)
    for ($i = 0; $i -lt $count; $i++) {
        $e = 20 + 16 * $i
        if ($e + 16 -gt $b.Length) { break }
        $keyAt = $keys + [BitConverter]::ToUInt16($b, $e)
        $format = [BitConverter]::ToUInt16($b, $e + 2)
        $length = [BitConverter]::ToUInt32($b, $e + 4)
        $at = $data + [BitConverter]::ToUInt32($b, $e + 12)
        $end = $keyAt; while ($end -lt $b.Length -and $b[$end] -ne 0) { $end++ }
        $key = [Text.Encoding]::ASCII.GetString($b, $keyAt, $end - $keyAt)
        if ($format -eq 0x0404) { $values[$key] = [BitConverter]::ToUInt32($b, $at) }
        else { $values[$key] = [Text.Encoding]::UTF8.GetString($b, $at, $length).TrimEnd([char]0) }
    }
    return $values
}

# --- where the game is ---------------------------------------------------------------------------
# A game folder is one with eboot.bin. The launcher's "Install from PKG" puts the game (with the
# update over it) under game\ next to BLauncher.exe; a dump is usually CUSA03173 with the update
# in CUSA03173-patch beside it.
function Find-GameFolder([string]$Dir, [int]$Depth = 3) {
    if (-not $Dir -or -not (Test-Path $Dir)) { return $null }
    if (Test-Path (Join-Path $Dir "eboot.bin")) { return (Resolve-Path $Dir).Path }
    if ($Depth -le 0) { return $null }
    foreach ($sub in Get-ChildItem -Directory -Force $Dir -ErrorAction SilentlyContinue | Sort-Object Name) {
        if ($sub.Name -like "*-patch" -or $sub.Name -like "*-mods") { continue }
        $found = Find-GameFolder $sub.FullName ($Depth - 1)
        if ($found) { return $found }
    }
    return $null
}
$isGame = { param($v) [bool](Find-GameFolder $v) }
$isUpdate = { param($v) $v -eq "none" -or (Test-Path (Join-Path $v "eboot.bin")) }

# Not loopback: shadnet tells the other player the address it sees you on, and 127.0.0.1 means nothing to them.
$isIp = { param($v) $ip = $null; ($v -match '^\d{1,3}(\.\d{1,3}){3}$') -and [Net.IPAddress]::TryParse($v, [ref]$ip) -and $v -notmatch '^127\.' }
$isName = { param($v) $v -match '^[A-Za-z0-9_-]{3,16}$' }
$isSecret = { param($v) $v -match '^[A-Za-z0-9_-]{1,32}$' }
$isParty = { param($v) $v -match '^[A-Za-z0-9_-]{1,32}:[A-Za-z0-9_-]{1,32}$' }
$isRole = { param($v) $v -eq "host" -or $v -eq "guest" }
$isPort = { param($v) ($v -match '^\d{4,5}$') -and [int]$v -ge 1024 -and [int]$v -le 65535 }
$isFps = { param($v) $v -in @("30", "60") }
$isYesNo = { param($v) $v -in @("yes", "no") }

Write-Host ""
Write-Host "  Bloodborne seamless co-op on Bloodborne PC (native port): setup"
Write-Host "  ================================================================"
Write-Host "  Press Enter to keep the value in [brackets]."
Write-Host ""
Write-Host "  Your own copy of Bloodborne, version 1.09: a dumped game folder (the one with eboot.bin),"
Write-Host "  or install it from your .pkg files first with BLauncher.exe -> 'Install from PKG'."

$defGame = $saved["GAME"]
if (-not $defGame) {
    foreach ($c in @((Join-Path $kit "game"), (Join-Path (Split-Path -Parent $kit) "CUSA03173"), (Split-Path -Parent $kit))) {
        $found = Find-GameFolder $c 2
        if ($found) { $defGame = $found; break }
    }
}
$game = Find-GameFolder (Ask "Your Bloodborne game folder" $defGame $isGame "No eboot.bin in that folder or the folders inside it.")
$gameSfo = Read-Sfo (Join-Path $game "sce_sys\param.sfo")
$title = if ($gameSfo["TITLE_ID"]) { $gameSfo["TITLE_ID"] } else { "?" }

# The 1.09 update: already over the game (a merged dump, or Install from PKG), or a folder of its own.
$update = "none"
if ($gameSfo["APP_VER"] -ne "01.09") {
    $defUpdate = $saved["UPDATE"]
    if (-not $defUpdate -or -not (& $isUpdate $defUpdate)) {
        $defUpdate = $null
        foreach ($c in @("$game-patch", (Join-Path (Split-Path -Parent $game) "$title-patch"))) {
            if (Test-Path (Join-Path $c "eboot.bin")) { $defUpdate = $c; break }
        }
    }
    Write-Host ""
    Write-Host "  That game folder is version $($gameSfo['APP_VER']); co-op needs the 1.09 update over it."
    Write-Host "  Type the folder of the dumped 1.09 update (the one with its own eboot.bin)."
    $update = Ask "Your 1.09 update folder" $defUpdate { param($v) Test-Path (Join-Path $v "eboot.bin") } "No eboot.bin in that folder."
    $update = (Resolve-Path $update).Path
    $updateSfo = Read-Sfo (Join-Path $update "sce_sys\param.sfo")
    if ($updateSfo["APP_VER"] -and $updateSfo["APP_VER"] -ne "01.09") {
        throw "That update is version $($updateSfo['APP_VER']); co-op needs 01.09."
    }
}
if ($title -ne "CUSA03173") {
    Write-Host "  This is $title. Co-op is tested on CUSA03173 (the European Game of the Year edition); other" -ForegroundColor Yellow
    Write-Host "  editions at 1.09 may work. Every player in a party must use the same edition." -ForegroundColor Yellow
}

$role = Ask "Are you the host or the guest? (host/guest)" $saved["ROLE"] $isRole "Type host or guest."
Write-Host ""
Write-Host "  The host's IP is the one the guests reach the host's PC on: a VPN address (Radmin VPN,"
Write-Host "  Tailscale, ZeroTier) or the host's public IP with ports forwarded. The host types their own"
Write-Host "  address here too, never 127.0.0.1."
$hostIp = Ask "Host IP" $saved["HOSTIP"] $isIp "Enter an IPv4 address like 26.12.34.56."
Write-Host ""
Write-Host "  Your account on the host's server. The host registers it with 'HOST - Register Account'."
$npid = Ask "Account name (3-16 letters, digits, _ or -)" $saved["NPID"] $isName "3-16 characters: letters, digits, _ or -."
$pass = Ask "Account password (letters, digits, _ or -)" $saved["PASS"] $isSecret "1-32 characters: letters, digits, _ or -."
Write-Host ""
Write-Host "  The party is name:secret, and every player must type exactly the same thing."
$party = Ask "Party" $(if ($saved["PARTY"]) { $saved["PARTY"] } else { "hunters:changeme" }) $isParty "Use name:secret, letters, digits, _ or - only."
# "auto" lets Windows pick a free port. A fixed one only matters for port forwarding, and Windows
# (Hyper-V, WSL, Docker) can reserve ranges that refuse the bind with error 10013.
$defPort = if ($saved["P2PPORT"]) { $saved["P2PPORT"] } else { "auto" }
$p2pPort = Ask "P2P UDP port (auto unless you port-forward)" $defPort { param($v) $v -eq "auto" -or (& $isPort $v) } "Type auto, or a number from 1024 to 65535."
$fps = Ask "Frame rate (60 or 30)" $(if ($saved["FPS"]) { $saved["FPS"] } else { "60" }) $isFps "Type 60 or 30."
# Every enemy's and boss's HP, damage and poise multipliers; everyone in a party should use the same.
$isHarder = { param($v) $v -eq "off" -or $v -match '^\d+(\.\d+)?,\d+(\.\d+)?,\d+(\.\d+)?$' }
$harder = Ask "Harder enemies: HP,damage,poise multipliers (off for the normal game)" $(if ($saved["HARDER"]) { $saved["HARDER"] } else { "off" }) $isHarder "Type off, or three numbers from 1 to 10 like 3,1.5,3."
Write-Host ""
Write-Host "  Party death, as in Elden Ring Nightreign: a killing blow puts you down instead of killing"
Write-Host "  you while a partner stands on your map; they revive you by striking you (R1/R2, their"
Write-Host "  weapon's damage counts), and you can hold Circle to give up. Always on."

# --- saves ---------------------------------------------------------------------------------------
# The port keeps saves in user\savedata\1\<title>\. Copied once, only when asked: later progress
# stays here.
$user = Join-Path $kit "user"
$saves = Join-Path $user "savedata\1"
New-Item -ItemType Directory -Force (Join-Path $user "log") | Out-Null
if (-not (Test-Path $saves)) {
    Write-Host ""
    Write-Host "  Saves: Enter starts with a new character. To bring a character along, type a folder with"
    Write-Host "  your saves: a shadPS4 'user' folder (it has home\1000\savedata), or a savedata folder that"
    Write-Host "  holds a CUSA... folder."
    $isSaves = { param($v) $v -eq "new" -or (Test-Path (Join-Path $v "home\1000\savedata")) -or (Get-ChildItem -Directory $v -Filter "CUSA*" -ErrorAction SilentlyContinue) }
    $from = Ask "Saves to copy (new for a new character)" "new" $isSaves "No home\1000\savedata or CUSA... folder there."
    New-Item -ItemType Directory -Force $saves | Out-Null
    if ($from -ne "new") {
        if (Test-Path (Join-Path $from "home\1000\savedata")) { $from = Join-Path $from "home\1000\savedata" }
        Write-Host "  Copying your saves from $from ..."
        robocopy $from $saves /E /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
    }
}

# --- game folder ---------------------------------------------------------------------------------
# coop-game\CUSA03173: folder links (junctions) to the game's folders, the update's files as hard
# links (copies on another drive) over them. coop-game\CUSA03173-mods\dvdroot_ps4\event: the co-op
# event files; the port takes the "-mods" folder beside the game. Junctions need NTFS: on an exFAT
# or FAT32 drive the folder goes under %LOCALAPPDATA% instead.
function Remove-Junction([string]$Path) {
    # rmdir removes only the link; Remove-Item -Recurse could follow it into the game.
    if (Test-Path $Path) { cmd /c rmdir "$Path" | Out-Null }
}
function Test-JunctionSupport([string]$Dir) {
    $probe = Join-Path $Dir ".junction-probe"
    try {
        New-Item -ItemType Directory -Force $Dir | Out-Null
        Remove-Junction $probe
        New-Item -ItemType Junction -Path $probe -Target $game -ErrorAction Stop | Out-Null
        Remove-Junction $probe
        return $true
    } catch { return $false }
}
# A folder made of links, never what they point at: junctions are removed as links, the rest
# (real folders, hard links, copies) belongs to the view.
function Remove-View([string]$Path) {
    if (-not (Test-Path $Path)) { return }
    $item = Get-Item -Force $Path
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { Remove-Junction $Path; return }
    foreach ($entry in Get-ChildItem -Force $Path) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { Remove-Junction $entry.FullName }
        elseif ($entry.PSIsContainer) { Remove-View $entry.FullName }
        else { Remove-Item -Force $entry.FullName }
    }
    Remove-Item -Force $Path
}
function Add-FileLink([string]$Path, [string]$Target) {
    try { New-Item -ItemType HardLink -Path $Path -Target $Target -ErrorAction Stop | Out-Null }
    catch { Copy-Item $Target $Path }
}
# The game with its update over it: a folder only one of them has is a junction, a folder both
# have is merged, a file is the update's when it has one.
function Merge-Game([string]$Game, [string]$Update, [string]$View) {
    New-Item -ItemType Directory -Force $View | Out-Null
    $entries = [ordered]@{}
    foreach ($e in Get-ChildItem -Force $Game) { $entries[$e.Name.ToLowerInvariant()] = @($e, $null) }
    if ($Update -and (Test-Path $Update)) {
        foreach ($e in Get-ChildItem -Force $Update) {
            $key = $e.Name.ToLowerInvariant()
            if ($entries.Contains($key)) { $entries[$key][1] = $e } else { $entries[$key] = @($null, $e) }
        }
    }
    foreach ($pair in $entries.Values) {
        $own, $newer = $pair
        if ($own -and $newer -and $own.PSIsContainer -and $newer.PSIsContainer) {
            Merge-Game $own.FullName $newer.FullName (Join-Path $View $own.Name)
            continue
        }
        $entry = if ($newer) { $newer } else { $own }
        $path = Join-Path $View $entry.Name
        if ($entry.PSIsContainer) { New-Item -ItemType Junction -Path $path -Target $entry.FullName | Out-Null }
        else { Add-FileLink $path $entry.FullName }
    }
}

$gameDir = Join-Path $kit "coop-game"
if (-not (Test-JunctionSupport $gameDir)) {
    if ((Test-Path $gameDir) -and -not (Get-ChildItem -Force -Recurse $gameDir -File)) { Remove-Item -Recurse -Force $gameDir }
    $gameDir = Join-Path $env:LOCALAPPDATA "BloodborneSeamlessCoop\coop-game"
    Write-Host "  This drive can't hold folder links, so the game folder goes in $gameDir"
    if (-not (Test-JunctionSupport $gameDir)) { throw "Can't create folder links in $gameDir either" }
}
Write-Host ""
Write-Host "  Preparing the game folder (links to your game, nothing is copied) ..."
Remove-View "$gameDir\CUSA03173"
Merge-Game $game $(if ($update -ne "none") { $update } else { $null }) "$gameDir\CUSA03173"
$merged = Read-Sfo "$gameDir\CUSA03173\sce_sys\param.sfo"
if ($merged["APP_VER"] -ne "01.09") {
    throw "The game is version $($merged['APP_VER']) with the update over it; co-op needs 01.09."
}

# --- co-op event files ---------------------------------------------------------------------------
# Made from this game's own event files every time setup runs, by the same script the rig's and
# the developers' kits use, inside BLauncher.exe (no Python needed). event-reference.json holds
# the hashes of the files this release was tested with: every player in a party needs the same.
$events = Join-Path $kit "event-patch"
$gameEvents = "$gameDir\CUSA03173\dvdroot_ps4\event"
if (-not (Test-Path (Join-Path $gameEvents "common.emevd.dcx"))) {
    throw "No dvdroot_ps4\event\common.emevd.dcx in the game folder: is it a complete dump?"
}
Write-Host "  Making the co-op event files from your game ..."
if (Test-Path $events) { Remove-Item -Recurse -Force $events }
$eventLog = Join-Path $user "log\event-patch.txt"
$tool = Start-Process -FilePath (Join-Path $kit "BLauncher.exe") -Wait -PassThru -NoNewWindow `
    -ArgumentList @("--script", "`"$kit\tools\party_boss_events.py`"", "`"$events`"", "`"$gameEvents`"") `
    -RedirectStandardOutput $eventLog -RedirectStandardError "$eventLog.err"
if ($tool.ExitCode -ne 0 -or -not (Test-Path (Join-Path $events "manifest.json"))) {
    Get-Content $eventLog, "$eventLog.err" -ErrorAction SilentlyContinue | Select-Object -Last 10 | ForEach-Object { Write-Host "    $_" }
    throw "Making the co-op event files failed (log: $eventLog)."
}
$made = (Get-Content -Raw (Join-Path $events "manifest.json") | ConvertFrom-Json).files
$referencePath = Join-Path $kit "event-reference.json"
if (Test-Path $referencePath) {
    $reference = (Get-Content -Raw $referencePath | ConvertFrom-Json).files
    $differ = @()
    foreach ($name in $reference.PSObject.Properties.Name) {
        $mine = $made.$name
        if (-not $mine -or $mine.output_sha256 -ne $reference.$name.output_sha256) { $differ += $name }
    }
    foreach ($name in $made.PSObject.Properties.Name) {
        if (-not $reference.$name) { $differ += $name }
    }
    if ($differ) {
        Write-Host "  Your event files differ from the ones this release was tested with: $($differ -join ', ')." -ForegroundColor Yellow
        Write-Host "  That happens with another game version or edition. Co-op may still work, but every player in" -ForegroundColor Yellow
        Write-Host "  a party must have the same files; compare event-patch\manifest.json with your partners." -ForegroundColor Yellow
    } else {
        Write-Host "  The event files match the tested ones."
    }
}
$modsEvents = "$gameDir\CUSA03173-mods\dvdroot_ps4\event"
Remove-View "$gameDir\CUSA03173-mods"
New-Item -ItemType Directory -Force $modsEvents | Out-Null
Copy-Item "$events\*" $modsEvents -Force

# --- online --------------------------------------------------------------------------------------
# The game's own server (sign-in, online check) is the host's backend; run.py uses this file as
# it is. shadnet's addresses and the account go to the game in "2 - PLAY".
Write-Utf8 (Join-Path $user "host_overrides.custom.json") ("{`n  `"https://ss4.scej-network.jp:20443`": `"http://${hostIp}:20443`"`n}`n")

$lines = @(
    "@rem Written by 1 - SETUP.bat. Run it again to change these.",
    "set `"GAME=$game`"",
    "set `"UPDATE=$update`"",
    "set `"ROLE=$role`"",
    "set `"HOSTIP=$hostIp`"",
    "set `"NPID=$npid`"",
    "set `"PASS=$pass`"",
    "set `"PARTY=$party`"",
    "set `"P2PPORT=$p2pPort`"",
    "set `"FPS=$fps`"",
    "set `"HARDER=$harder`"",
    "set `"GAMEDIR=$gameDir`""
)
[IO.File]::WriteAllLines($settingsPath, $lines, (New-Object Text.ASCIIEncoding))

Write-Host ""
Write-Host "  Done." -ForegroundColor Green
Write-Host "    game $title $($merged['APP_VER']) ($game$(if ($update -ne 'none') { " + $update" }))"
Write-Host "    role $role, host $hostIp, account $npid, party $party, P2P UDP $p2pPort, $fps FPS, harder enemies $harder"
Write-Host "    shadnet ${hostIp}:31313, WebAPI ${hostIp}:31315, backend ${hostIp}:20443"
if ($role -eq "host") {
    Write-Host ""
    Write-Host "  Next: 'HOST - Start Servers', then 'HOST - Register Account' for every player's account."
} else {
    Write-Host ""
    Write-Host "  Next: make sure the host has registered '$npid', then run '2 - PLAY'."
}
