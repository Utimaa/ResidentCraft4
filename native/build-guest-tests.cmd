@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\tests mkdir ..\build\tests
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX guest_controller_test.cpp guest_controller.cpp game_access.cpp native_hostility.cpp vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde32.c /Fo..\build\tests\ /Fe..\build\tests\guest_controller_test.exe /link /MACHINE:X86 user32.lib
if errorlevel 1 exit /b 1
..\build\tests\guest_controller_test.exe
exit /b %errorlevel%
