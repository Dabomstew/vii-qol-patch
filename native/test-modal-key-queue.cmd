@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /WX /EHsc /MT /I native\include native\tools\modal_key_queue_test.cpp /Fo:build\native-build\ /Fe:build\native-build\modal-key-queue-test.exe
if errorlevel 1 exit /b 1
build\native-build\modal-key-queue-test.exe
exit /b %errorlevel%
