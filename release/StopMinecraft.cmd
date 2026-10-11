@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0release\StopMinecraft.ps1" %*
pause
