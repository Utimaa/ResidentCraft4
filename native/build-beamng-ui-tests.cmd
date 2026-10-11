@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\beamng-ui-reader-tests mkdir ..\build\beamng-ui-reader-tests
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX ..\tests\beamng_ui_reader_test.cpp beamng_ui_reader.cpp transport.cpp /Fo..\build\beamng-ui-reader-tests\ /Fe..\build\beamng-ui-reader-tests\beamng_ui_reader_test.exe /link /MACHINE:X86 ws2_32.lib iphlpapi.lib
if errorlevel 1 exit /b 1
copy /y ..\build\beamng-ui-reader-tests\beamng_ui_reader_test.exe ..\build\beamng-ui-reader-tests\BeamNG.drive.x64.exe >nul
if errorlevel 1 exit /b 1
if /I "%~1"=="--build-only" exit /b 0
cd ..
build\beamng-ui-reader-tests\beamng_ui_reader_test.exe
exit /b %errorlevel%
