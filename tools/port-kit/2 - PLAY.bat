@echo off
rem Starts Bloodborne PC (the native port) with seamless co-op, from this kit's folders and
rem settings.bat (1 - SETUP). The game's log is user\log\shad_log.txt, its console output
rem user\log\console.txt.
set "KIT=%~dp0"
cd /d "%KIT%"
if not exist "settings.bat" (
    echo Run "1 - SETUP" first.
    pause
    exit /b 1
)
call "%KIT%settings.bat"
if "%GAMEDIR%"=="" set "GAMEDIR=%KIT%game"
if not exist "%GAMEDIR%\CUSA03173\eboot.bin" (
    echo Run "1 - SETUP" again: the game folder is missing.
    pause
    exit /b 1
)
rem A newer kit brings newer event files; put them in place on every start.
copy /y "%KIT%event-patch\*" "%GAMEDIR%\CUSA03173-mods\dvdroot_ps4\event\" >nul

rem The port (run.py, inside BLauncher.exe): this kit's game, saves and settings.
set "BB_DATA_DIR=%KIT:~0,-1%"
set "BB_USER_DIR=%KIT%user"
set "BB_GAME_DIR=%GAMEDIR%\CUSA03173"
if "%FPS%"=="" set "FPS=60"
set "BB_FPS=%FPS%"
set "BB_LOG_FILE=%KIT%user\log\shad_log.txt"
set "BB_LOG_LEVEL=info"
rem Online through the host's shadnet and backend (user\host_overrides.custom.json).
set "BB_ONLINE=1"
set "BB_SHADNET_SERVER=%HOSTIP%:31313"
set "BB_SHADNET_WEBAPI=http://%HOSTIP%:31315"
set "BB_SHADNET_NPID=%NPID%"
set "BB_SHADNET_PASSWORD=%PASS%"
set "BB_USER_NAME=%NPID%"

rem Seamless co-op: the same settings as the shadPS4 kit.
set SHADPS4_BLOODBORNE_SEAMLESS_COOP=1
rem auto (and the old kit defaults 51610/51611) lets Windows pick a free UDP port.
set "SHADPS4_P2P_PORT="
if /i not "%P2PPORT%"=="auto" if not "%P2PPORT%"=="51610" if not "%P2PPORT%"=="51611" set "SHADPS4_P2P_PORT=%P2PPORT%"
set "SHADPS4_BLOODBORNE_PARTY=%PARTY%"
set "SHADPS4_BLOODBORNE_PARTY_ROLE=%ROLE%"
set SHADPS4_BLOODBORNE_RE_TRACE=0
rem Harder enemies: HP,damage,poise multipliers for every enemy and boss (1 - SETUP), or off.
if "%HARDER%"=="" set "HARDER=3,1.5,3"
set "SHADPS4_BLOODBORNE_HARDER_ENEMIES="
if /i not "%HARDER%"=="off" set "SHADPS4_BLOODBORNE_HARDER_ENEMIES=%HARDER%"
rem Party death (Nightreign style) is always on in the port: no setting.
set "SHADPS4_BLOODBORNE_PARTY_DEATH="

if not exist "%KIT%user\log" mkdir "%KIT%user\log"
if exist "%KIT%user\log\shad_log.txt" copy /y "%KIT%user\log\shad_log.txt" "%KIT%user\log\shad_log.prev.txt" >nul
if exist "%KIT%user\log\console.txt" copy /y "%KIT%user\log\console.txt" "%KIT%user\log\console.prev.txt" >nul
start "Bloodborne co-op" /min cmd /c ""%KIT%BLauncher.exe" --run >"%KIT%user\log\console.txt" 2>&1"
