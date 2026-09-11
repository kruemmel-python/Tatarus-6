@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\imaginatio_lab\start_imaginatio_lab.ps1"
if errorlevel 1 pause
