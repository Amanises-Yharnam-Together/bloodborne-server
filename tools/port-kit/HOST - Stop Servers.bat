@echo off
title Stop co-op servers
rem HOST ONLY. Stops what "HOST - Start Servers" started from this folder: shadnet (this kit's,
rem or the shadPS4 co-op kit's beside it) and the backend, which runs inside BLauncher.exe
rem without a window of its own. Other programs and the game are left alone.
set "KIT=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$kit = (Resolve-Path $env:KIT).Path; $other = Join-Path (Split-Path -Parent $kit) 'SeamlessCoop';" ^
  "foreach ($p in Get-CimInstance Win32_Process -Filter \"Name='shadnet.exe' OR Name='BLauncher.exe'\") {" ^
  "  $path = [string]$p.ExecutablePath;" ^
  "  $ours = $path.StartsWith($kit, 'OrdinalIgnoreCase') -or $path.StartsWith($other, 'OrdinalIgnoreCase');" ^
  "  if ($p.Name -eq 'BLauncher.exe') { $ours = $ours -and ([string]$p.CommandLine -like '*bb_dummy_server.py*') }" ^
  "  if ($ours) { Stop-Process -Id $p.ProcessId -Force; 'Stopped ' + $(if ($p.Name -eq 'shadnet.exe') { 'shadnet' } else { 'the backend' }) + '.' }" ^
  "}"
echo.
pause
