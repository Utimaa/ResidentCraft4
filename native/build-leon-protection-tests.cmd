@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\leon-protection-tests mkdir ..\build\leon-protection-tests
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" (
  echo Visual Studio C++ x86 build tools were not found.
  exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /we4477 /DWIN32 /DNOMINMAX leon_protection_test.cpp /Fo..\build\leon-protection-tests\ /Fe..\build\leon-protection-tests\leon_protection_test.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
..\build\leon-protection-tests\leon_protection_test.exe
exit /b %errorlevel%
