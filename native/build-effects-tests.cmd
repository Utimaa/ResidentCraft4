@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\effects-tests mkdir ..\build\effects-tests
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX ..\tests\effects_harness.cpp renderer.cpp /Fo..\build\effects-tests\ /Fe..\build\effects-tests\effects_harness.exe /link /MACHINE:X86 d3d9.lib user32.lib
if errorlevel 1 exit /b 1
..\build\effects-tests\effects_harness.exe
exit /b %errorlevel%
