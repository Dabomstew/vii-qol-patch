@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x64
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\uncompressed-build mkdir build\uncompressed-build
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\src\pac_archive.cpp native\src\loose_manifest.cpp native\src\pac_unpack.cpp native\tools\pac_unpack_test.cpp /Fo:build\uncompressed-build\ /Fe:build\uncompressed-build\pac-unpack-test.exe
if errorlevel 1 exit /b 1
build\uncompressed-build\pac-unpack-test.exe build\uncompressed-build\unit-unpack-%RANDOM%-%RANDOM%
exit /b %errorlevel%
