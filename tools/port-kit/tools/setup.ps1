# Seamless co-op kit for the native port (Bloodborne PC): asks the player for their settings, then
# prepares this kit's own folders from their Bloodborne.PC.Online install:
#   - user\: the saves (from the emulator co-op kit beside this one when there is one, else from
#     the install), the first time only; and the game server of the host (host_overrides)
#   - game\CUSA03173: the game with its 1.09 update over it, in one folder (the port reads one)
#   - game\CUSA03173-mods: the install's active mods with the co-op event files on top
# Answers are kept in settings.bat, which PLAY.bat and the HOST bats read. The install is never
# changed. Kept the same as the emulator kit's tools\setup.ps1 where the two agree.
$ErrorActionPreference = "Stop"
$kit = Split-Path -Parent $PSScriptRoot
$settingsPath = Join-Path $kit "settings.bat"
$emulatorKit = Join-Path (Split-Path -Parent $kit) "SeamlessCoop"

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
# This kit's answers, else the emulator kit's: the same server, account and party.
$saved = Read-Settings $settingsPath
if ($saved.Count -eq 0) { $saved = Read-Settings (Join-Path $emulatorKit "settings.bat") }

function Ask([string]$Prompt, [string]$Default, [scriptblock]$Valid, [string]$Hint) {
    while ($true) {
        $shown = if ($Default) { " [$Default]" } else { "" }
        $answer = (Read-Host "  $Prompt$shown").Trim()
        if ($answer -eq "") { $answer = $Default }
        if ($answer -and (& $Valid $answer)) { return $answer }
        Write-Host "    $Hint" -ForegroundColor Red
    }
}

# Not loopback: shadnet tells the other player the address it sees you on, and 127.0.0.1 means nothing to them.
$isIp = { param($v) $ip = $null; ($v -match '^\d{1,3}(\.\d{1,3}){3}$') -and [Net.IPAddress]::TryParse($v, [ref]$ip) -and $v -notmatch '^127\.' }
$isName = { param($v) $v -match '^[A-Za-z0-9_-]{3,16}$' }
$isSecret = { param($v) $v -match '^[A-Za-z0-9_-]{1,32}$' }
$isParty = { param($v) $v -match '^[A-Za-z0-9_-]{1,32}:[A-Za-z0-9_-]{1,32}$' }
$isRole = { param($v) $v -eq "host" -or $v -eq "guest" }
$isInstall = { param($v) Test-Path (Join-Path $v "DATA\Bloodborne\CUSA03173\eboot.bin") }
$isPort = { param($v) ($v -match '^\d{4,5}$') -and [int]$v -ge 1024 -and [int]$v -le 65535 }
$isFps = { param($v) $v -in @("30", "60") }
$isYesNo = { param($v) $v -in @("yes", "no") }

Write-Host ""
Write-Host "  Bloodborne seamless co-op on Bloodborne PC (native port): setup"
Write-Host "  ================================================================"
Write-Host "  Press Enter to keep the value in [brackets]."
Write-Host ""

$defInstall = $saved["INSTALL"]
if (-not $defInstall) {
    foreach ($c in @((Split-Path -Parent $kit), "C:\Bloodborne.PC.Online")) {
        if (& $isInstall $c) { $defInstall = $c; break }
    }
}
$install = Ask "Your Bloodborne.PC.Online folder" $defInstall $isInstall "No DATA\Bloodborne\CUSA03173\eboot.bin in that folder."
$role = Ask "Are you the host or the guest? (host/guest)" $saved["ROLE"] $isRole "Type host or guest."
Write-Host ""
Write-Host "  The host's IP is the one shown in Radmin VPN (or Tailscale) on the HOST's PC."
Write-Host "  The host types their own VPN IP here too, never 127.0.0.1."
$hostIp = Ask "Host IP" $saved["HOSTIP"] $isIp "Enter an IPv4 address like 26.12.34.56."
Write-Host ""
Write-Host "  Your shadnet account (the same one as in the shadPS4 co-op kit works)."
Write-Host "  The host registers new ones with 'HOST - Register Account'."
$npid = Ask "Account name (3-16 letters, digits, _ or -)" $saved["NPID"] $isName "3-16 characters: letters, digits, _ or -."
$pass = Ask "Account password (letters, digits, _ or -)" $saved["PASS"] $isSecret "1-32 characters: letters, digits, _ or -."
Write-Host ""
Write-Host "  The party is name:secret, and both players must type exactly the same thing."
$party = Ask "Party" $(if ($saved["PARTY"]) { $saved["PARTY"] } else { "hunters:changeme" }) $isParty "Use name:secret, letters, digits, _ or - only."
# "auto" lets Windows pick a free port. A fixed one only matters for port forwarding, and Windows
# (Hyper-V, WSL, Docker) can reserve ranges that refuse the bind with error 10013.
$defPort = if ($saved["P2PPORT"] -and $saved["P2PPORT"] -notin @("51610", "51611")) { $saved["P2PPORT"] } else { "auto" }
$p2pPort = Ask "P2P UDP port (auto unless you port-forward)" $defPort { param($v) $v -eq "auto" -or (& $isPort $v) } "Type auto, or a number from 1024 to 65535."
$fps = Ask "Frame rate (60, or 30 as on shadPS4)" $(if ($saved["FPS"]) { $saved["FPS"] } else { "60" }) $isFps "Type 60 or 30."
# Every enemy's and boss's HP, damage and poise multipliers; everyone in a party should use the same.
$isHarder = { param($v) $v -eq "off" -or $v -match '^\d+(\.\d+)?,\d+(\.\d+)?,\d+(\.\d+)?$' }
$harder = Ask "Harder enemies: HP,damage,poise multipliers (off for the normal game)" $(if ($saved["HARDER"]) { $saved["HARDER"] } else { "3,1.5,3" }) $isHarder "Type off, or three numbers from 1 to 10 like 3,1.5,3."
Write-Host ""
Write-Host "  Party death, as in Elden Ring Nightreign: a killing blow puts you down instead of killing"
Write-Host "  you while a partner stands on your map; they revive you by striking you (R1/R2, their"
Write-Host "  weapon's damage counts), and you can hold Circle to give up. Always on."

# --- saves -----------------------------------------------------------------------------------
# The port keeps saves in user\savedata\<user>\<title>\ (user 1), shadPS4 in
# user\home\1000\savedata\<title>\. Copied once: later progress stays in this kit.
$user = Join-Path $kit "user"
$saves = Join-Path $user "savedata\1"
New-Item -ItemType Directory -Force (Join-Path $user "log") | Out-Null
if (-not (Test-Path $saves)) {
    $from = $null
    foreach ($c in @((Join-Path $emulatorKit "user"), "$install\DATA\user")) {
        if (Test-Path (Join-Path $c "home\1000\savedata")) { $from = $c; break }
    }
    New-Item -ItemType Directory -Force $saves | Out-Null
    if ($from) {
        Write-Host ""
        Write-Host "  Copying your saves from $from ..."
        robocopy (Join-Path $from "home\1000\savedata") $saves /E /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
        # Co-op's own state (where a guest was when its game closed, ...).
        Get-ChildItem $from -Filter "bloodborne_party_*.txt" -File | Copy-Item -Destination $user
    } else {
        Write-Host "  No saves found: the game starts a new character." -ForegroundColor Yellow
    }
}

# --- game folder -----------------------------------------------------------------------------
# game\CUSA03173: folder links (junctions) to the game's folders, the update's files as hard links
# (copies on another drive) over them. game\CUSA03173-mods\dvdroot_ps4: links to the install's
# active mods, plus a real "event" folder with the kit's co-op map events on top; the port takes
# the "-mods" folder beside the game. Junctions need NTFS: on an exFAT or FAT32 drive the folder
# goes under %LOCALAPPDATA% instead (a junction there can still point at the other drive).
$src = "$install\DATA\Bloodborne"
function Remove-Junction([string]$Path) {
    # rmdir removes only the link; Remove-Item -Recurse could follow it into the install.
    if (Test-Path $Path) { cmd /c rmdir "$Path" | Out-Null }
}
function Test-JunctionSupport([string]$Dir) {
    $probe = Join-Path $Dir ".junction-probe"
    try {
        New-Item -ItemType Directory -Force $Dir | Out-Null
        Remove-Junction $probe
        New-Item -ItemType Junction -Path $probe -Target $src -ErrorAction Stop | Out-Null
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

$gameDir = Join-Path $kit "game"
if (-not (Test-JunctionSupport $gameDir)) {
    if ((Test-Path $gameDir) -and -not (Get-ChildItem -Force -Recurse $gameDir -File)) { Remove-Item -Recurse -Force $gameDir }
    $gameDir = Join-Path $env:LOCALAPPDATA "BloodborneSeamlessCoopPort\game"
    Write-Host "  This drive can't hold folder links, so the game folder goes in $gameDir"
    if (-not (Test-JunctionSupport $gameDir)) { throw "Can't create folder links in $gameDir either" }
}
Write-Host ""
Write-Host "  Preparing the game folder (links to your install, nothing is copied) ..."
Remove-View "$gameDir\CUSA03173"
Merge-Game "$src\CUSA03173" "$src\CUSA03173-patch" "$gameDir\CUSA03173"

$modsDst = "$gameDir\CUSA03173-mods\dvdroot_ps4"
Remove-View $modsDst
New-Item -ItemType Directory -Force $modsDst | Out-Null
# The install's active mods (the mod manager copies them into dvdroot_ps4), file by file as hard
# links, without the files of mods the port does without: Vertex Explosion Fix (the port fixes
# vertex explosions itself; the mod only costs the characters their face shapes).
$installMods = "$src\CUSA03173-mods\dvdroot_ps4"
$skip = @("MODS", "!BACKUP", "!INSTLOGS", "JSGME.exe", "JSGME.ini", "event", "dvdroot_ps4")
$withoutMods = @("! Vertex Explosion Fix*")
$left = @{}
foreach ($pattern in $withoutMods) {
    foreach ($mod in Get-ChildItem -Directory "$installMods\MODS" -Filter $pattern -ErrorAction SilentlyContinue) {
        foreach ($file in Get-ChildItem -Recurse -File $mod.FullName) {
            $relative = $file.FullName.Substring($mod.FullName.Length + 1) -replace '^dvdroot_ps4\\', ''
            $left[$relative.ToLowerInvariant()] = $file.Length
        }
        Write-Host "  Leaving out the mod '$($mod.Name)' (not needed on the port)"
    }
}
if (Test-Path $installMods) {
    foreach ($item in Get-ChildItem -Force $installMods | Where-Object { $_.Name -notin $skip }) {
        $files = if ($item.PSIsContainer) { Get-ChildItem -Recurse -File -Force $item.FullName } else { @($item) }
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($installMods.Length + 1)
            $key = $relative.ToLowerInvariant()
            if ($left.ContainsKey($key) -and $left[$key] -eq $file.Length) { continue }
            $target = Join-Path $modsDst $relative
            New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
            Add-FileLink $target $file.FullName
        }
    }
}
New-Item -ItemType Directory -Force "$modsDst\event" | Out-Null
if (Test-Path "$src\CUSA03173-mods\dvdroot_ps4\event") { Copy-Item "$src\CUSA03173-mods\dvdroot_ps4\event\*" "$modsDst\event\" -Recurse -Force }
Copy-Item "$kit\event-patch\*" "$modsDst\event\" -Force

# --- online ----------------------------------------------------------------------------------
# The game's own server (sign-in, online check) is the host's backend; run.py uses this file as
# it is. shadnet's addresses and the account go to the game in PLAY.bat.
Write-Utf8 (Join-Path $user "host_overrides.custom.json") ("{`n  `"https://ss4.scej-network.jp:20443`": `"http://${hostIp}:20443`"`n}`n")

$lines = @(
    "@rem Written by 1 - SETUP.bat. Run it again to change these.",
    "set `"INSTALL=$install`"",
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
Write-Host "    role $role, host $hostIp, account $npid, party $party, P2P UDP $p2pPort, $fps FPS, harder enemies $harder"
Write-Host "    shadnet ${hostIp}:31313, WebAPI ${hostIp}:31315, backend ${hostIp}:20443"
if ($role -eq "host") {
    Write-Host ""
    Write-Host "  Next: 'HOST - Start Servers', then 'HOST - Register Account' for any new account."
} else {
    Write-Host ""
    Write-Host "  Next: make sure the host has registered '$npid', then run '2 - PLAY'."
}
