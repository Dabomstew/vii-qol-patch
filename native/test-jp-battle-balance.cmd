@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /std:c++17 /O2 /W4 /WX /EHsc /MT /DVII_JP_BALANCE_TESTING /DVII_CODE_CALLS_TESTING /I native\include native\tools\jp_battle_balance_test.cpp native\src\jp_battle_balance.cpp native\src\code_calls.cpp /Fo:build\native-build\ /Fe:build\native-build\jp-battle-balance-test.exe
if errorlevel 1 exit /b 1
build\native-build\jp-battle-balance-test.exe
exit /b %errorlevel%
