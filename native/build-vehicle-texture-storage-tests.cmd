@echo off
setlocal
for %%I in ("%~dp0..") do set "PROJECT_ROOT=%%~fI"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b %errorlevel%
if not exist "%PROJECT_ROOT%\build\vehicle-texture-storage-tests" mkdir "%PROJECT_ROOT%\build\vehicle-texture-storage-tests"
cl /nologo /std:c++17 /EHsc /W4 /we4477 /MT /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN "%PROJECT_ROOT%\tests\vehicle_texture_storage_test.cpp" /Fo"%PROJECT_ROOT%\build\vehicle-texture-storage-tests\test.obj" /Fe"%PROJECT_ROOT%\build\vehicle-texture-storage-tests\test.exe" /link /MACHINE:X86
if errorlevel 1 exit /b %errorlevel%
"%PROJECT_ROOT%\build\vehicle-texture-storage-tests\test.exe"
exit /b %errorlevel%
