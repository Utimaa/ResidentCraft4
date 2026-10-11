@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\native-grab-tests mkdir ..\build\native-grab-tests
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /we4477 /DWIN32 /DNOMINMAX /DRE4CRAFT_GRAB_FIXTURE /Ivendor\minhook\include ..\tests\native_grab_hooks_test.cpp native_grab_hooks.cpp vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde32.c /Fo..\build\native-grab-tests\ /Fe..\build\native-grab-tests\native_grab_hooks_test.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
..\build\native-grab-tests\native_grab_hooks_test.exe
exit /b %errorlevel%
