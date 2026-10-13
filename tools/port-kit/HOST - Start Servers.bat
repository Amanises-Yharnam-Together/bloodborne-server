@echo off
title Start co-op servers
rem HOST ONLY. Starts shadnet (accounts, matchmaking, summons; ports 31313-31315)
rem and the local backend (game login and online check; port 20443). A shadnet with
rem BloodborneBackend=true in server\shadnet.cfg answers the backend's port itself; the
rem Python backend starts only when nothing does.
rem Leave the shadnet window open while you play. Running it again starts only what is
rem missing; "HOST - Stop Servers" stops both.
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
rem That kit's shadnet.exe may be older than this one's (the party boards the port needs: party
rem death's vitals, the host's hidden NPCs): this kit's goes over it, the old one kept beside it.
if not "%SERVERKIT%"=="%KIT%" (
    fc /b "%KIT%server\shadnet.exe" "%SERVERKIT%server\shadnet.exe" >nul 2>nul
    if errorlevel 1 (
        tasklist /fi "imagename eq shadnet.exe" | find /i "shadnet.exe" >nul
        if errorlevel 1 (
            copy /y "%SERVERKIT%server\shadnet.exe" "%SERVERKIT%server\shadnet.exe.old" >nul
            copy /y "%KIT%server\shadnet.exe" "%SERVERKIT%server\shadnet.exe" >nul
            echo Updated the co-op server to this kit's version.
        ) else (
            echo An older co-op server is running: close its window and run this again to update it.
        )
    )
)

rem The address the guests reach this PC on, for the game's online check (ss.info) when
rem shadnet answers it: the same --address the Python backend gets.
set "SHADNET_BACKEND_ADDRESS=%HOSTIP%"
tasklist /fi "imagename eq shadnet.exe" | find /i "shadnet.exe" >nul
if errorlevel 1 (
    cd /d "%SERVERKIT%server"
    start "co-op shadnet" "%SERVERKIT%server\shadnet.exe"
    echo Started shadnet.
    rem Give its own backend a moment to take port 20443 before the Python one is tried.
    findstr /i /x /c:"BloodborneBackend=true" "%SERVERKIT%server\shadnet.cfg" >nul 2>nul
    if not errorlevel 1 call :wait_backend
) else (
    echo shadnet is already running. If it is the test rig or the old
    echo "SERVER - Host P2P" rather than this one, close it and run this again.
)

netstat -ano | find ":20443 " | find "LISTENING" >nul
if errorlevel 1 (
    rem The backend is a Python script that BLauncher.exe runs, so no Python is needed. Its
    rem output goes to user\log\backend.txt; "HOST - Stop Servers" stops it.
    if not exist "%KIT%user\log" mkdir "%KIT%user\log"
    cd /d "%SERVERKIT%backend"
    start "co-op backend" /min cmd /c ""%KIT%BLauncher.exe" --script "%SERVERKIT%backend\bb_dummy_server.py" --address %HOSTIP% --port 20443 --shadnet-webapi http://%HOSTIP%:31315 >"%KIT%user\log\backend.txt" 2>&1"
    echo Started the backend for %HOSTIP%, log in user\log\backend.txt.
) else (
    echo The backend is running ^(in shadnet when BloodborneBackend=true^).
)

echo.
echo Check from any PC: http://%HOSTIP%:20443/status should say "ok": true
echo.
pause
exit /b 0

:wait_backend
for /l %%i in (1,1,10) do (
    netstat -ano | find ":20443 " | find "LISTENING" >nul
    if not errorlevel 1 exit /b 0
    timeout /t 1 /nobreak >nul
)
exit /b 0
