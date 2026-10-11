@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /std:c++17 /O2 /W4 /WX /EHsc /MT /I native\include native\tools\runtime_diagnostics_test.cpp native\src\texture_upload.cpp native\src\texture_disk.cpp native\src\texture_pipeline.cpp native\src\identity.cpp native\src\code_calls.cpp /Fo:build\native-build\ /Fe:build\native-build\runtime-diagnostics-test.exe /link d3d11.lib user32.lib
if errorlevel 1 exit /b 1
build\native-build\runtime-diagnostics-test.exe "%CD%\build\runtime-diagnostics-test-%RANDOM%-%RANDOM%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /W4 /WX /EHsc /MT /I native\include native\tools\neptasm_runtime_test.cpp native\src\code_calls.cpp /Fo:build\native-build\ /Fe:build\native-build\neptasm-runtime-test.exe /link user32.lib
if errorlevel 1 exit /b 1
build\native-build\neptasm-runtime-test.exe
exit /b %errorlevel%
