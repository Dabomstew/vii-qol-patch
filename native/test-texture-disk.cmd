@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\texture_disk_test.cpp native\src\texture_disk.cpp native\src\texture_upload.cpp native\src\core.cpp native\src\identity.cpp /Fo:build\native-build\ /Fe:build\native-build\texture-disk-test.exe
if errorlevel 1 exit /b 1
build\native-build\texture-disk-test.exe "%CD%\build\texture-disk-test-%RANDOM%-%RANDOM%"
exit /b %errorlevel%
