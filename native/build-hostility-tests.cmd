@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\hostility-tests mkdir ..\build\hostility-tests
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX native_hostility_test.cpp native_hostility.cpp /Fo..\build\hostility-tests\ /Fe..\build\hostility-tests\native_hostility_test.exe /link /MACHINE:X86 bcrypt.lib user32.lib
if errorlevel 1 exit /b 1
..\build\hostility-tests\native_hostility_test.exe
exit /b %errorlevel%
