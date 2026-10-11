@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\state-block-benchmark mkdir ..\build\state-block-benchmark
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX ..\tests\state_block_benchmark.cpp /Fo..\build\state-block-benchmark\ /Fe..\build\state-block-benchmark\state_block_benchmark.exe /link /MACHINE:X86 d3d9.lib user32.lib
if errorlevel 1 exit /b 1
..\build\state-block-benchmark\state_block_benchmark.exe
exit /b %errorlevel%
