@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /WX /EHsc /MT /I native\include native\tools\tutorial_suppression_test.cpp native\src\tutorial_suppression.cpp native\src\code_calls.cpp /Fo:build\native-build\ /Fe:build\native-build\tutorial-suppression-test.exe
if errorlevel 1 exit /b 1
build\native-build\tutorial-suppression-test.exe
exit /b %errorlevel%
