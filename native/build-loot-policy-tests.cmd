@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\tests mkdir ..\build\tests
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX loot_policy_test.cpp /Fo..\build\tests\loot_policy_test.obj /Fe..\build\tests\loot_policy_test.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
..\build\tests\loot_policy_test.exe
exit /b %errorlevel%
