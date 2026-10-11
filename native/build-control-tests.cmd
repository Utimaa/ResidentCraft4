@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\tests mkdir ..\build\tests
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX camera_control_test.cpp camera_control.cpp game_access.cpp vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde32.c /Fo..\build\tests\ /Fe..\build\tests\camera_control_test.exe /link /MACHINE:X86 user32.lib
exit /b %errorlevel%
