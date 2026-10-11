@echo off
setlocal
cd /d "%~dp0"
if not exist ..\build\native-box-tests mkdir ..\build\native-box-tests
python ..\tools\probe-native-boxes.py
if errorlevel 1 exit /b 1
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DWIN32 /DNOMINMAX /DRE4CRAFT_ACCESS_FIXTURE ..\tests\native_box_consumer_test.cpp guest_controller.cpp game_access.cpp native_hostility.cpp vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde32.c /Fo..\build\native-box-tests\ /Fe..\build\native-box-tests\native_box_consumer_test.exe /link /MACHINE:X86 user32.lib
if errorlevel 1 exit /b 1
..\build\native-box-tests\native_box_consumer_test.exe
exit /b %errorlevel%
