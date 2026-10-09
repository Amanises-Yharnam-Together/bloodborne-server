@echo off
title Register a shadnet account
rem HOST ONLY, with "HOST - Start Servers" running. Register yourself and your friend,
rem using the account names and passwords each of you typed into "1 - SETUP".
set "KIT=%~dp0"
rem The shadPS4 co-op kit's servers when it is beside this one: the same accounts (server\db).
set "SERVERKIT=%KIT%"
if exist "%KIT%..\SeamlessCoop\server\shadnet.exe" set "SERVERKIT=%KIT%..\SeamlessCoop\"
cd /d "%SERVERKIT%server\clientsample"

:again
echo.
set "ID="
set "PW="
set /p "ID=Account name (blank to quit): "
if "%ID%"=="" exit /b 0
set /p "PW=Password: "
if "%PW%"=="" goto :again
"%SERVERKIT%server\clientsample\shadnet-sample.exe" 127.0.0.1 31313 register "%ID%" "%PW%" "%ID%.%PW%@p2p.db"
goto :again
