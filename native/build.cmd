@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\native mkdir ..\build\native
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
if not exist main.cpp (
  echo native\main.cpp is required to build the complete ASI plugin.
  exit /b 1
)
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /we4477 /DWIN32 /DNOMINMAX /LD /Ivendor\minhook\include main.cpp transport.cpp beamng_ui_reader.cpp selftest.cpp game_access.cpp renderer.cpp collision_export.cpp camera_control.cpp host_controls.cpp host_input.cpp hud_renderer.cpp guest_controller.cpp native_grab_hooks.cpp native_window_hooks.cpp native_hostility.cpp world_render.cpp vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde32.c /Fo..\build\native\ /Fe..\build\native\RE4Craft.asi /link /MACHINE:X86 /DEF:exports.def d3d9.lib user32.lib ws2_32.lib iphlpapi.lib
exit /b %errorlevel%
