@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\live_monitor\start_live_monitor.ps1"
if errorlevel 1 pause
