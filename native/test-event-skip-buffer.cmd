@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /WX /EHsc /MT /I native\include native\tools\event_skip_buffer_test.cpp native\src\event_skip_buffer.cpp native\src\ordered_event_keys.cpp native\src\code_calls.cpp /Fo:build\native-build\ /Fe:build\native-build\event-skip-buffer-test.exe /link user32.lib
if errorlevel 1 exit /b 1
build\native-build\event-skip-buffer-test.exe
exit /b %errorlevel%
