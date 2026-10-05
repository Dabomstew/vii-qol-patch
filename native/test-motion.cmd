@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /WX /EHsc /MT /I native\include native\tools\motion_bytes_test.cpp native\src\motion_bytes.cpp /Fo:build\native-build\ /Fe:build\native-build\motion-bytes-test.exe
if errorlevel 1 exit /b 1
build\native-build\motion-bytes-test.exe
exit /b %errorlevel%
