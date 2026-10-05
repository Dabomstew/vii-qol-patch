@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-tests mkdir build\native-tests
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\battle_auto_skip_test.cpp native\src\battle_auto_skip.cpp native\src\code_calls.cpp /Fo:build\native-tests\ /Fe:build\native-tests\battle-auto-skip-test.exe
if errorlevel 1 exit /b 1
build\native-tests\battle-auto-skip-test.exe
exit /b %errorlevel%
