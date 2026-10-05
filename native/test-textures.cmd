@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT  /I native\include native\tools\texture_probe.cpp native\src\texture_upload.cpp /Fo:build\native-build\ /Fe:build\native-build\texture-probe.exe /link d3d11.lib
exit /b %errorlevel%
