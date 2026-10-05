@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /WX /EHsc /MT /D VII_LOAD_TIMING_TESTING /I native\include native\tools\load_timing_test.cpp native\src\load_timing.cpp native\src\code_calls.cpp /Fo:build\native-build\ /Fe:build\native-build\load-timing-test.exe /link user32.lib
if errorlevel 1 exit /b 1
build\native-build\load-timing-test.exe
exit /b %errorlevel%
