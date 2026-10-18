@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\script-tests mkdir ..\build\script-tests
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /we4477 /DWIN32 /DNOMINMAX ..\tests\native_script_handoff_test.cpp /Fo..\build\script-tests\ /Fe..\build\script-tests\native_script_handoff_test.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
..\build\script-tests\native_script_handoff_test.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /we4477 /DWIN32 /DNOMINMAX ..\tests\native_window_handoff_test.cpp /Fo..\build\script-tests\ /Fe..\build\script-tests\native_window_handoff_test.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
..\build\script-tests\native_window_handoff_test.exe
exit /b %errorlevel%
