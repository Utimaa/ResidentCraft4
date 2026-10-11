@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\window-tests mkdir ..\build\window-tests
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /we4477 /DWIN32 /DNOMINMAX ..\tests\native_window_handoff_test.cpp /Fo..\build\window-tests\ /Fe..\build\window-tests\native_window_handoff_test.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
..\build\window-tests\native_window_handoff_test.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /we4477 /DWIN32 /DNOMINMAX /DRE4CRAFT_WINDOW_FIXTURE ..\tests\native_window_hooks_test.cpp native_window_hooks.cpp vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde32.c /Fo..\build\window-tests\ /Fe..\build\window-tests\native_window_hooks_test.exe /link /MACHINE:X86 user32.lib
if errorlevel 1 exit /b 1
..\build\window-tests\native_window_hooks_test.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /we4477 /DWIN32 /DNOMINMAX /DRE4CRAFT_ACCESS_FIXTURE ..\tests\native_window_access_test.cpp game_access.cpp vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde32.c /Fo..\build\window-tests\ /Fe..\build\window-tests\native_window_access_test.exe /link /MACHINE:X86 user32.lib
if errorlevel 1 exit /b 1
..\build\window-tests\native_window_access_test.exe
exit /b %errorlevel%
