@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\beamng-mode-tests mkdir ..\build\beamng-mode-tests
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX beamng_mode_test.cpp /Fo..\build\beamng-mode-tests\ /Fe..\build\beamng-mode-tests\beamng_mode_test.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
..\build\beamng-mode-tests\beamng_mode_test.exe
exit /b %errorlevel%
