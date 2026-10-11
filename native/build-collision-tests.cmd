@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\collision-tests mkdir ..\build\collision-tests
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /W4 /MT /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN collision_export.cpp collision_export_test.cpp /Fo..\build\collision-tests\ /Fe..\build\collision-tests\collision_export_test.exe /link /MACHINE:X86
if errorlevel 1 exit /b %errorlevel%
..\build\collision-tests\collision_export_test.exe
exit /b %errorlevel%
