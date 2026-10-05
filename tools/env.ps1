# Dev environment for the Bloodborne co-op forks: PATH refresh + MSVC x64 dev shell + LLVM + Qt.
# BB_ROOT is the folder holding the shadp2p and shadnet-p2p checkouts (default C:\bb).
$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User")
$env:Path = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;" + $env:Path
if (-not $env:BB_ROOT) { $env:BB_ROOT = "C:\bb" }
$vs = & vswhere.exe -products * -latest -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
$env:Path = "C:\Program Files\LLVM\bin;C:\Qt\6.8.3\msvc2022_64\bin;" + $env:Path
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\msvc2022_64"
