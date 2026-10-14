@echo off
rem Double-click to (re)start TitanVSP and open it in a new browser tab.
rem Kills any old server first and rebuilds the UI if its sources changed.
rem Drag a .vsp3 file onto this script to open it. Close this window to stop the server.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0vsp-web.ps1" %*
if errorlevel 1 pause
