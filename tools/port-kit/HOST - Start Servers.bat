@echo off
title Start co-op servers
rem HOST ONLY. Starts shadnet (accounts, matchmaking, summons; ports 31313-31315)
rem and the local backend (game login and online check; port 20443).
rem Leave both windows open while you play. Running it again starts only what is missing.
set "KIT=%~dp0"
cd /d "%KIT%"
if not exist "%KIT%settings.bat" (
    echo Run "1 - SETUP" first.
    pause
    exit /b 1
)
call "%KIT%settings.bat"
rem The shadPS4 co-op kit's servers when it is beside this one: the same accounts (server\db).
set "SERVERKIT=%KIT%"
if exist "%KIT%..\SeamlessCoop\server\shadnet.exe" set "SERVERKIT=%KIT%..\SeamlessCoop\"

set "PY=py -3"
where py >nul 2>nul
if errorlevel 1 set "PY=python"
%PY% --version >nul 2>nul
if errorlevel 1 (
    echo Python was not found. Install it from https://www.python.org/downloads/
    echo and tick "Add python.exe to PATH" during setup.
    pause
    exit /b 1
)

tasklist /fi "imagename eq shadnet.exe" | find /i "shadnet.exe" >nul
if errorlevel 1 (
    cd /d "%SERVERKIT%server"
    start "co-op shadnet" "%SERVERKIT%server\shadnet.exe"
    echo Started shadnet.
) else (
    echo shadnet is already running. If it is the test rig or the old
    echo "SERVER - Host P2P" rather than this one, close it and run this again.
)

netstat -ano | find ":20443 " | find "LISTENING" >nul
if errorlevel 1 (
    cd /d "%SERVERKIT%backend"
    start "co-op backend" %PY% "%SERVERKIT%backend\bb_dummy_server.py" --address %HOSTIP% --port 20443 --shadnet-webapi http://%HOSTIP%:31315
    echo Started the backend for %HOSTIP%.
) else (
    echo The backend is already running.
)

echo.
echo Check from any PC: http://%HOSTIP%:20443/status should say "ok": true
echo.
pause
