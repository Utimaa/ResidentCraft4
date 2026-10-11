@echo off
setlocal
for %%I in ("%~dp0..") do set "PROJECT_ROOT=%%~fI"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b %errorlevel%
if not exist "%PROJECT_ROOT%\build\tests" mkdir "%PROJECT_ROOT%\build\tests"
cl /nologo /std:c++17 /EHsc /W4 /MT /O2 "%PROJECT_ROOT%\tests\stream_harness.cpp" "%PROJECT_ROOT%\native\renderer.cpp" /Fo"%PROJECT_ROOT%\build\tests\\" /Fe"%PROJECT_ROOT%\build\tests\stream_harness.exe" /link /MACHINE:X86 d3d9.lib user32.lib
exit /b %errorlevel%
