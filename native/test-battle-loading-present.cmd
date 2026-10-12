@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /std:c++17 /O2 /W4 /WX /EHsc /MT /D VII_BATTLE_PRESENT_TESTING /D VII_LOAD_TIMING_TESTING /D VII_CODE_CALLS_TESTING /I native\include native\tools\battle_loading_present_test.cpp native\src\battle_loading_present.cpp native\src\load_timing.cpp native\src\code_calls.cpp /Fo:build\native-build\ /Fe:build\native-build\battle-loading-present-test.exe /link user32.lib d3d11.lib
if errorlevel 1 exit /b 1
build\native-build\battle-loading-present-test.exe
exit /b %errorlevel%
