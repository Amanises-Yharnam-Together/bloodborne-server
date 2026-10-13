# Builds a public release of Bloodborne seamless co-op for the native port (Bloodborne PC): one zip
# for the GitHub releases page that a player unzips anywhere and uses with nothing but their own
# copy of the game (a dumped game folder with the 1.09 update, or the .pkg files, which the
# launcher installs). Optionally publishes it as a draft GitHub release.
#
#   1. builds the port (bbport\build.sh) and packages it (bbport\packaging\windows\package.sh:
#      BLauncher.exe, the game's executables and DLLs, PkgTool, ReShade, XeSS, DLSS when built)
#   2. builds the co-op server (tools\build-server.ps1) with its Qt DLLs (windeployqt) and the
#      account tool (server\clientsample)
#   3. assembles BloodborneSeamlessCoop\: the package, the kit's scripts (tools\port-kit, without
#      the Bloodborne.PC.Online launchers), the release's own setup and texts (tools\release\kit),
#      the server, the backend and the event patcher (tools\re\party_boss_events.py). The co-op
#      event files are made by each player's setup from their own game: the release holds only
#      their expected hashes (event-reference.json, from -GameDir)
#   4. checks it (tools\release\check_release.py: everything there, no game or player files),
#      zips it, and writes the SHA-256
#   5. with -Publish: creates a draft release v<Version> on GitHub at the build's commit with the
#      zip attached (gh when installed, else the REST API with GITHUB_TOKEN or git's credential)
#
# Usage: make-release.ps1 -Version 0.1.0 [-Out <dir>] [-GameDir <game event source>]
#            [-SkipPortBuild] [-SkipServerBuild] [-AllowDirty] [-Publish [-Prerelease]] [-Repo owner/name]
#   -GameDir        a game folder with the 1.09 update over it (or the update folder itself) whose
#                   dvdroot_ps4\event the tested event files come from; default: the rig's game
#   -SkipPortBuild  package the port as it is in bbport\out (built by build.sh or the rig scripts)
#   -SkipServerBuild use the server and account tool already in <BB_ROOT>\out\release-server and
#                   <BB_ROOT>\out\release-clientsample
#   -DlssSdk        a checkout of github.com/NVIDIA/DLSS: builds the DLSS bridge first (else the
#                   release has DLSS only when bbport\out already holds bbport_dlss.dll)
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$')][string]$Version,
    [string]$Out,
    [string]$GameDir,
    [string]$DlssSdk,
    [switch]$SkipPortBuild,
    [switch]$SkipServerBuild,
    [switch]$AllowDirty,
    [switch]$Publish,
    [switch]$Prerelease,
    [string]$Repo = "lKaniel/bloodborne-server"
)
$ErrorActionPreference = "Stop"
if (-not $env:BB_ROOT) { $env:BB_ROOT = "C:\bb" }
$root = Split-Path -Parent $PSScriptRoot
$bbport = Join-Path $root "bbport"
if (-not $Out) { $Out = Join-Path $env:BB_ROOT "release" }
$name = "BloodborneSeamlessCoop"
$tag = "v$Version"
$zipName = "$name-$tag-windows.zip"
function Step([string]$Text) { Write-Host ""; Write-Host "== $Text" -ForegroundColor Cyan }
function Write-Utf8([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding $false)) }

# --- what is being released ----------------------------------------------------------------------
# The commit the release is built from, and whether the tree has changes it does not hold.
Step "Source"
$commit = $null; $dirty = $false
if (Test-Path (Join-Path $root ".jj")) {
    Push-Location $root
    try {
        $empty = (jj log -r "@" --no-graph -T "empty" 2>$null) -eq "true"
        $commit = (jj log -r $(if ($empty) { "@-" } else { "@" }) --no-graph -T "commit_id" 2>$null)
        $dirty = -not $empty
    } finally { Pop-Location }
}
if (-not $commit) {
    $commit = (git -C $root rev-parse HEAD 2>$null)
    $dirty = [bool](git -C $root status --porcelain --ignore-submodules=dirty 2>$null)
}
if (-not $commit) { throw "$root is neither a jj nor a git checkout" }
Write-Host "  $tag from $commit$(if ($dirty) { ' (with uncommitted changes)' })"
if ($dirty -and -not $AllowDirty) { throw "The checkout has uncommitted changes: commit them, or pass -AllowDirty for a test build." }
if ($dirty -and $Publish) { throw "Only a committed (and pushed) tree can be published." }

# --- 1. the port -------------------------------------------------------------------------------
$msys = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }
$bash = Join-Path $msys "usr\bin\bash.exe"
if (-not (Test-Path $bash)) { throw "No MSYS2 at $msys (see bbport\packaging\windows\README.md)" }
function Invoke-Clang64([string]$Command) {
    # A clean CLANG64 shell, in a child process: the MSVC dev shell's settings are not for it.
    $saved = @{}
    foreach ($var in "INCLUDE", "LIB", "LIBPATH", "CMAKE_PREFIX_PATH", "MSYSTEM", "CHERE_INVOKING") {
        $saved[$var] = [Environment]::GetEnvironmentVariable($var)
        [Environment]::SetEnvironmentVariable($var, $null)
    }
    $env:MSYSTEM = "CLANG64"; $env:CHERE_INVOKING = "1"
    try {
        $unix = (& (Join-Path $msys "usr\bin\cygpath.exe") -u $bbport).Trim()
        & $bash -lc "cd '$unix' && $Command"
        if ($LASTEXITCODE -ne 0) { throw "failed in bbport: $Command" }
    } finally {
        foreach ($var in $saved.Keys) { [Environment]::SetEnvironmentVariable($var, $saved[$var]) }
    }
}
Step "Port"
if ($DlssSdk) {
    $env:DLSS_SDK_ROOT = $DlssSdk
    Invoke-Clang64 "bash packaging/windows/build_dlss.sh"
}
if (-not $SkipPortBuild) { Invoke-Clang64 "bash build.sh" }
foreach ($exe in "bb-probe.exe", "bb-gpu-capabilities.exe", "bb-play.exe") {
    if (-not (Test-Path (Join-Path $bbport "out\$exe"))) { throw "No bbport\out\${exe}: build the port first (drop -SkipPortBuild)" }
}
if (-not (Test-Path (Join-Path $bbport "fsr4_shaders"))) {
    try { Invoke-Clang64 "bash tools/fetch_fsr4_assets.sh" }
    catch { Write-Host "  FSR 4 assets could not be fetched: the release has FSR 3.1 only, FSR 4 downloads from the launcher" -ForegroundColor Yellow }
}
if (-not (Test-Path (Join-Path $bbport "out\bbport_dlss.dll"))) {
    Write-Host "  No DLSS bridge (pass -DlssSdk): the release has no DLSS" -ForegroundColor Yellow
}
if (-not $env:WINPYTHON) {
    $python = Get-ChildItem "$env:LOCALAPPDATA\Programs\Python\Python3*\python.exe", "C:\Python3*\python.exe" -ErrorAction SilentlyContinue |
        Sort-Object { [int]($_.Directory.Name -replace '\D', '') } | Select-Object -Last 1
    if ($python) { $env:WINPYTHON = (& (Join-Path $msys "usr\bin\cygpath.exe") -u $python.FullName).Trim() }
}
Invoke-Clang64 "bash packaging/windows/package.sh"
$package = Join-Path $bbport "out\stage\bbport-windows"

# --- 2. the server -----------------------------------------------------------------------------
Step "Server"
$serverBuild = Join-Path $env:BB_ROOT "out\release-server"
$sampleBuild = Join-Path $env:BB_ROOT "out\release-clientsample"
if (-not $SkipServerBuild) {
    # In child processes: env.ps1 enters the MSVC dev shell for the whole session.
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build-server.ps1") -Src (Join-Path $root "server") -Out $serverBuild
    if ($LASTEXITCODE -ne 0) { throw "the server build failed" }
    $sample = Join-Path $root "server\clientsample"
    powershell -NoProfile -ExecutionPolicy Bypass -Command ". '$PSScriptRoot\env.ps1'; cmake -S '$sample' -B '$sampleBuild' -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl *> '$sampleBuild.log'; if (`$LASTEXITCODE) { exit 1 }; cmake --build '$sampleBuild' --target shadnet-sample *>> '$sampleBuild.log'; exit `$LASTEXITCODE"
    if ($LASTEXITCODE -ne 0) { Get-Content "$sampleBuild.log" -Tail 20; throw "the account tool (server\clientsample) build failed (log: $sampleBuild.log)" }
}
foreach ($need in "$serverBuild\shadnet.exe", "$sampleBuild\shadnet-sample.exe") {
    if (-not (Test-Path $need)) { throw "Missing $need" }
}

# --- 3. assemble -------------------------------------------------------------------------------
Step "Assemble"
$stage = Join-Path $Out "stage"
$release = Join-Path $stage $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $release | Out-Null
robocopy $package $release /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "copying the port's package failed" }

# The kit's and the release's own files. cmd needs CRLF in batch files (labels and goto break on
# LF), and players read the text files in Notepad. The kit's root\ launchers belong to the
# Bloodborne.PC.Online layout, and its setup and READ ME are replaced by the release's.
function Copy-Crlf([string]$From, [string]$To) {
    $text = [IO.File]::ReadAllText($From) -replace "`r`n", "`n" -replace "`n", "`r`n"
    New-Item -ItemType Directory -Force (Split-Path -Parent $To) | Out-Null
    [IO.File]::WriteAllText($To, $text, (New-Object Text.UTF8Encoding $false))
}
$kitSource = Join-Path $PSScriptRoot "port-kit"
foreach ($file in Get-ChildItem -Recurse -File $kitSource) {
    $relative = $file.FullName.Substring($kitSource.Length + 1)
    if ($relative -like "root\*" -or $relative -in @("READ ME FIRST.txt", "tools\setup.ps1")) { continue }
    Copy-Crlf $file.FullName (Join-Path $release $relative)
}
$releaseSource = Join-Path $PSScriptRoot "release\kit"
foreach ($file in Get-ChildItem -Recurse -File $releaseSource) {
    $relative = $file.FullName.Substring($releaseSource.Length + 1)
    $target = Join-Path $release $relative
    if ($file.Extension -in ".txt", ".bat", ".ps1", ".cfg") { Copy-Crlf $file.FullName $target }
    else { New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null; Copy-Item $file.FullName $target }
}
Copy-Item (Join-Path $root "tools\re\party_boss_events.py") (Join-Path $release "tools\") -Force
Copy-Item (Join-Path $root "emulator\LICENSES\GPL-2.0-or-later.txt") (Join-Path $release "licenses\") -Force

# The server: shadnet with the Qt DLLs it loads, the account tool, and the release's shadnet.cfg.
$server = Join-Path $release "server"
Copy-Item "$serverBuild\shadnet.exe" $server -Force
$qt = if ($env:QT_ROOT) { $env:QT_ROOT } else { "C:\Qt\6.8.3\msvc2022_64" }
cmd /c "`"$qt\bin\windeployqt6.exe`" --release --no-translations `"$server\shadnet.exe`" >nul 2>nul"
if (-not (Test-Path "$server\Qt6Core.dll")) { throw "windeployqt failed for $server\shadnet.exe" }
New-Item -ItemType Directory -Force "$server\clientsample" | Out-Null
Copy-Item "$sampleBuild\shadnet-sample.exe" "$server\clientsample\" -Force
# The backend (the game's own online services, run by BLauncher.exe on the host).
$backend = Join-Path $release "backend"
New-Item -ItemType Directory -Force $backend | Out-Null
Copy-Item (Join-Path $root "backend\*.py"), (Join-Path $root "backend\ss_info_template.xml"), (Join-Path $root "backend\README.txt") $backend -Force
Write-Utf8 (Join-Path $backend "notice.txt") ""

# The event files this release is tested with, as hashes only: setup compares each player's own.
if (-not $GameDir) { $GameDir = Join-Path $env:BB_ROOT "rig\port\game\CUSA03173" }
$gameEvents = Join-Path $GameDir "dvdroot_ps4\event"
if (Test-Path (Join-Path $gameEvents "common.emevd.dcx")) {
    $made = Join-Path $Out "event-check"
    if (Test-Path $made) { Remove-Item -Recurse -Force $made }
    $log = Join-Path $Out "event-check.log"
    # The release's own BLauncher.exe runs it, as it will on a player's PC.
    $tool = Start-Process -FilePath (Join-Path $release "BLauncher.exe") -Wait -PassThru -NoNewWindow `
        -ArgumentList @("--script", "`"$release\tools\party_boss_events.py`"", "`"$made`"", "`"$gameEvents`"") `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    if ($tool.ExitCode -ne 0 -or -not (Test-Path "$made\manifest.json")) {
        Get-Content $log, "$log.err" -ErrorAction SilentlyContinue | Select-Object -Last 10
        throw "the event patcher failed inside BLauncher.exe on $gameEvents"
    }
    $files = [ordered]@{}
    $manifest = Get-Content -Raw "$made\manifest.json" | ConvertFrom-Json
    foreach ($file in $manifest.files.PSObject.Properties | Sort-Object Name) {
        $files[$file.Name] = [ordered]@{ input_sha256 = $file.Value.input_sha256; output_sha256 = $file.Value.output_sha256 }
    }
    Write-Utf8 (Join-Path $release "event-reference.json") ((@{ files = $files } | ConvertTo-Json -Depth 4) + "`n")
    Remove-Item -Recurse -Force $made
    Write-Host "  event-reference.json: $($files.Count) event files from $gameEvents"
} else {
    Write-Host "  No game events at $gameEvents (-GameDir): the release has no event-reference.json, so setup cannot tell players whether their event files match" -ForegroundColor Yellow
}

$built = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd HH:mm 'UTC'")
Write-Utf8 (Join-Path $release "VERSION.txt") (@(
    "Bloodborne Seamless Co-op $tag (Windows, Bloodborne PC native port)",
    "Commit: $commit$(if ($dirty) { ' + uncommitted changes (test build)' })",
    "Built: $built",
    "Game: Bloodborne 1.09 (tested with CUSA03173)",
    "Source: https://github.com/$Repo"
) -join "`r`n")

# --- 4. check and zip --------------------------------------------------------------------------
# The scripts must run inside the release's BLauncher.exe as they would with python: mods.py's
# hud_patch is the one in scripts\ and finds its icon strip (a frozen copy patched the HUD without
# the party list's icons and portraits, Kaniel 2026-10-13).
Step "Launcher scripts"
$probeDir = Join-Path $stage "launcher-probe"
New-Item -ItemType Directory -Force $probeDir | Out-Null
# Beside mods.py, as mods.py imports it (and removed again before the release check).
$probe = Join-Path $release "scripts\launcher_probe.py"
$probeOut = Join-Path $probeDir "probe.txt"
Write-Utf8 $probe (@(
    "import pathlib",
    "import hud_patch",
    "pathlib.Path(r'$probeOut').write_text(f'{pathlib.Path(hud_patch.__file__).resolve()}|{hud_patch.HUD_ICONS.is_file()}', encoding='utf-8')"
) -join "`n")
$run = Start-Process -FilePath (Join-Path $release "BLauncher.exe") -Wait -PassThru -NoNewWindow `
    -ArgumentList @("--script", "`"$probe`"") `
    -RedirectStandardOutput (Join-Path $probeDir "out.txt") -RedirectStandardError (Join-Path $probeDir "err.txt")
Remove-Item -Force $probe
$answer = if (Test-Path $probeOut) { (Get-Content -Raw $probeOut).Trim() } else { "" }
$expected = (Join-Path $release "scripts\hud_patch.py")
if ($run.ExitCode -ne 0 -or $answer -ne "$((Resolve-Path $expected).Path)|True") {
    Get-Content (Join-Path $probeDir "err.txt") -ErrorAction SilentlyContinue | Select-Object -Last 10
    throw "BLauncher.exe runs the HUD patch from '$answer', not scripts\hud_patch.py with its icon strip"
}
Remove-Item -Recurse -Force $probeDir
Write-Host "  BLauncher.exe --script: hud_patch from scripts\, icon strip found"

Step "Check"
$checkPython = (& (Join-Path $msys "usr\bin\cygpath.exe") -w $env:WINPYTHON).Trim()
& $checkPython -I (Join-Path $PSScriptRoot "release\check_release.py") $release
if ($LASTEXITCODE -ne 0) { throw "the release check failed" }

Step "Zip"
$zip = Join-Path $Out $zipName
if (Test-Path $zip) { Remove-Item -Force $zip }
& $checkPython -I (Join-Path $bbport "packaging\windows\make_zip.py") $stage $name $zip
if ($LASTEXITCODE -ne 0) { throw "zipping failed" }
$sha = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
Write-Utf8 "$zip.sha256" "$sha *$zipName`n"
Write-Host "  $zip ($([math]::Round((Get-Item $zip).Length / 1MB)) MB, sha256 $sha)"

# --- 5. publish --------------------------------------------------------------------------------
if (-not $Publish) { Write-Host ""; Write-Host "Release built (not published: pass -Publish for a draft on GitHub)."; exit 0 }
Step "Publish (draft)"
$notes = @"
Seamless co-op for Bloodborne on Bloodborne PC (the native Windows port).

**No game files are included.** You need your own Bloodborne 1.09 (a dumped game folder with the 1.09 update, or the PS4 .pkg files, which the included launcher installs).

1. Download ``$zipName`` and unzip it anywhere.
2. Open ``READ ME FIRST.txt`` and follow it: run ``1 - SETUP``, the host runs ``HOST - Start Servers``, everyone runs ``2 - PLAY``.

Everyone in a party must use this same release.

Built from $commit. SHA-256 of the zip: ``$sha``
"@
if (Get-Command gh -ErrorAction SilentlyContinue) {
    $notesFile = Join-Path $Out "notes.md"
    Write-Utf8 $notesFile $notes
    $flags = @("--draft", "--target", $commit, "--title", "Bloodborne Seamless Co-op $tag", "--notes-file", $notesFile)
    if ($Prerelease) { $flags += "--prerelease" }
    gh release create $tag $zip "$zip.sha256" --repo $Repo @flags
    if ($LASTEXITCODE -ne 0) { throw "gh release create failed" }
} else {
    $token = $env:GITHUB_TOKEN
    if (-not $token) {
        $answer = "protocol=https`nhost=github.com`n`n" | git credential fill 2>$null
        $token = ($answer | Where-Object { $_ -like "password=*" } | Select-Object -First 1) -replace '^password=', ''
    }
    if (-not $token) { throw "No gh, GITHUB_TOKEN or git credential for github.com: cannot publish" }
    $headers = @{ Authorization = "Bearer $token"; Accept = "application/vnd.github+json"; "User-Agent" = "make-release" }
    $body = @{ tag_name = $tag; target_commitish = $commit; name = "Bloodborne Seamless Co-op $tag"; body = $notes
               draft = $true; prerelease = [bool]$Prerelease } | ConvertTo-Json
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $created = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$Repo/releases" -Headers $headers -Body $body -ContentType "application/json"
    foreach ($asset in @(@{ Path = $zip; Type = "application/zip" }, @{ Path = "$zip.sha256"; Type = "text/plain" })) {
        $assetName = [Uri]::EscapeDataString((Split-Path -Leaf $asset.Path))
        Invoke-RestMethod -Method Post -Uri "https://uploads.github.com/repos/$Repo/releases/$($created.id)/assets?name=$assetName" `
            -Headers $headers -InFile $asset.Path -ContentType $asset.Type -TimeoutSec 3600 | Out-Null
    }
    Write-Host "  Draft release: $($created.html_url)"
}
Write-Host ""
Write-Host "Draft release $tag created: check it on GitHub and press Publish."
