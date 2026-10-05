# Puts an existing test rig (rig-setup.ps1) on a low-memory profile so two instances fit on one PC:
#   - a rig-only game folder made of junctions to the real base game and patch, plus its own
#     "-mods" overlay holding only Potatoborne (hardlinked, so no extra disk space). shadPS4 derives
#     the overlay from the -g path lexically, so the live install's mods are left untouched.
#   - 960x540 render patch, lowest model LOD, shadows/SSAO/DoF/AA off, 30 FPS, no debug menu.
#   - no extra dmem, FSR/RCAS off, 960x540 windows.
param(
    [string]$Rig = "C:\bb\rig",
    [string]$Install = "C:\Bloodborne.PC.Online",
    [string]$LowMod = "! Potatoborne (Massively Reduces Memory Usage)",
    [int]$Width = 960,
    [int]$Height = 540
)
$ErrorActionPreference = "Stop"
function Write-Utf8([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding $false)) }

# --- rig game folder ----------------------------------------------------------------------
$src = "$Install\DATA\Bloodborne"
$game = "$Rig\game"
New-Item -ItemType Directory -Force $game | Out-Null
foreach ($name in "CUSA03173", "CUSA03173-patch") {
    if (-not (Test-Path "$game\$name")) { New-Item -ItemType Junction -Path "$game\$name" -Target "$src\$name" | Out-Null }
}
$mods = "$game\CUSA03173-mods\dvdroot_ps4"
if (Test-Path "$game\CUSA03173-mods") { Remove-Item -Recurse -Force "$game\CUSA03173-mods" }
$modSrc = "$src\CUSA03173-mods\dvdroot_ps4\MODS\$LowMod"
if (-not (Test-Path $modSrc)) { throw "low-memory mod not found: $modSrc" }
Get-ChildItem $modSrc -Recurse -File | ForEach-Object {
    $dst = Join-Path $mods $_.FullName.Substring($modSrc.Length + 1)
    New-Item -ItemType Directory -Force (Split-Path $dst -Parent) | Out-Null
    New-Item -ItemType HardLink -Path $dst -Target $_.FullName | Out-Null
}

# --- per-player settings ------------------------------------------------------------------
$enable = @("Skip Intro", "Performance Patch (perf increase)", "Disable Motion Blur (perf increase)",
            "Disable Dynamic Light Shadows (perf increase)", "Disable Chromatic Aberration", "Disable AA",
            "Disable DoF", "Disable SSAO", "Model LOD 2 (Lowest)", "Intel 12th Gen+ SFX workaround",
            "Intel Black Tonemap Fix", "Resolution Patch 960x540 (16:9)",
            "Disable Camera Auto Rotation via Movement")
foreach ($p in "host", "guest") {
    $dir = "$Rig\$p"
    $xmlPath = "$dir\user\patches\shadPS4\Bloodborne.xml"
    $xml = [xml](Get-Content $xmlPath -Raw)
    foreach ($meta in $xml.SelectNodes("//Metadata")) {
        $meta.SetAttribute("isEnabled", $(if ($enable -contains $meta.Name) { "true" } else { "false" }))
    }
    $xml.Save($xmlPath)

    $cfgPath = "$dir\user\custom_configs\CUSA03173.json"
    $cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
    $cfg.General.extra_dmem_in_mbytes = 0
    $cfg.GPU.fsr_enabled = $false
    $cfg.GPU.rcas_enabled = $false
    $cfg.GPU.full_screen = $false
    $cfg.GPU.window_width = $Width
    $cfg.GPU.window_height = $Height
    Write-Utf8 $cfgPath ($cfg | ConvertTo-Json -Depth 10)

    $launch = Get-Content "$dir\launch.bat" -Raw
    $launch = $launch -replace '-g "[^"]+"', "-g `"$game\CUSA03173\eboot.bin`""
    Set-Content "$dir\launch.bat" $launch -Encoding ascii -NoNewline
}
Write-Host "Low-spec profile applied: $Width x $Height, mod '$LowMod', no extra dmem."
