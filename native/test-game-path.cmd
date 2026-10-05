@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-tests mkdir build\native-tests
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\game_path_test.cpp /Fo:build\native-tests\ /Fe:build\native-tests\game-path-test.exe
if errorlevel 1 exit /b 1
build\native-tests\game-path-test.exe
exit /b %errorlevel%
