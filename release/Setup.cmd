@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0release\Setup.ps1" %*
if errorlevel 1 echo Setup failed. Read the message above; your saves were preserved.
pause
