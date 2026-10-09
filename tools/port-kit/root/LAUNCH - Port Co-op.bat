@echo off
cd /d "%~dp0SeamlessCoopPort"
if not exist "settings.bat" call "%~dp0SeamlessCoopPort\1 - SETUP.bat"
if not exist "%~dp0SeamlessCoopPort\settings.bat" exit /b 1
call "%~dp0SeamlessCoopPort\2 - PLAY.bat"
exit
