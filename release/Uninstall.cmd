@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0release\Uninstall.ps1" %*
pause
