@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\beamng-render-tests mkdir ..\build\beamng-render-tests
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX ..\tests\beamng_renderer_harness.cpp renderer.cpp transport.cpp beamng_ui_reader.cpp hud_renderer.cpp /Fo..\build\beamng-render-tests\ /Fe..\build\beamng-render-tests\beamng_renderer_harness.exe /link /MACHINE:X86 d3d9.lib user32.lib ws2_32.lib iphlpapi.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX ..\tests\vehicle_presentation_test.cpp /Fo..\build\beamng-render-tests\ /Fe..\build\beamng-render-tests\vehicle_presentation_test.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX ..\tests\vehicle_deformation_test.cpp /Fo..\build\beamng-render-tests\ /Fe..\build\beamng-render-tests\vehicle_deformation_test.exe
if errorlevel 1 exit /b 1
if /I "%~1"=="--build-only" exit /b 0
cd ..
build\beamng-render-tests\vehicle_presentation_test.exe
if errorlevel 1 exit /b 1
build\beamng-render-tests\vehicle_deformation_test.exe
if errorlevel 1 exit /b 1
build\beamng-render-tests\beamng_renderer_harness.exe %*
exit /b %errorlevel%
