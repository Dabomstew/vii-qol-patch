@echo off
setlocal
set "prepare_output=build\prepare-build"
set "prepare_defines="
if "%~1"=="--test" (
  set "prepare_output=build\prepare-test"
  set "prepare_defines=/DVII_PREPARE_TEST"
  goto validate
)
if "%~1"=="--existing-proxy" goto validate
if not "%~1"=="" exit /b 2
call "%~dp0build.cmd"
if errorlevel 1 exit /b 1
:validate
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0validate-proxy-registry.ps1"
if errorlevel 1 exit /b 1
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist %prepare_output% mkdir %prepare_output%
rc /nologo /fo %prepare_output%\prepare_game.res native\tools\prepare_game.rc
if errorlevel 1 exit /b 1
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /utf-8 /O2 /W4 /EHsc /MT /DUNICODE /D_UNICODE %prepare_defines% /I native\include native\src\identity.cpp native\src\texture_disk.cpp native\src\texture_upload.cpp native\src\texture_pipeline.cpp native\src\texture_sources.cpp native\src\texture_prepare.cpp native\src\pac_archive.cpp native\src\loose_manifest.cpp native\src\pac_unpack.cpp native\src\prepare_install.cpp native\src\prepare_maintenance.cpp native\src\prepare_config.cpp native\src\prepare_game.cpp native\tools\prepare_game_main.cpp %prepare_output%\prepare_game.res /Fo:%prepare_output%\ /Fe:%prepare_output%\VII-Prepare-Game.exe /link /SUBSYSTEM:WINDOWS
exit /b %errorlevel%
