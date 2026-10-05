@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /std:c++17 /utf-8 /O2 /W4 /WX /EHsc /MT /DUNICODE /D_UNICODE native\preparer-common\preparer.cpp native\preparer-common\session_test.cpp /Fo:build\native-build\ /Fe:build\native-build\preparer-session-test.exe
if errorlevel 1 exit /b 1
build\native-build\preparer-session-test.exe
exit /b %errorlevel%
