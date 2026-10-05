@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\prepare-build mkdir build\prepare-build
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /utf-8 /O2 /W4 /EHsc /MT /I native\include native\src\identity.cpp native\src\texture_sources.cpp native\src\texture_pipeline.cpp native\tools\texture_sources_test.cpp /Fo:build\prepare-build\ /Fe:build\prepare-build\texture-sources-test.exe
if errorlevel 1 exit /b 1
build\prepare-build\texture-sources-test.exe
exit /b %errorlevel%
