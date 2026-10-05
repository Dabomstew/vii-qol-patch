@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /LD /I native\include native\src\core.cpp native\src\identity.cpp native\src\proxy.cpp native\src\new_game_detector.cpp native\src\load_timing.cpp native\src\neptasm.cpp native\src\mip_cache.cpp native\src\texture_upload.cpp native\src\texture_disk.cpp native\src\texture_pipeline.cpp native\src\startup_probe.cpp native\src\code_calls.cpp native\src\motion_bytes.cpp native\src\motion_adapter.cpp native\src\motion_cache.cpp native\src\event_auto_skip.cpp native\src\event_skip_buffer.cpp native\src\ordered_event_keys.cpp native\src\tutorial_suppression.cpp native\src\battle_auto_skip.cpp native\src\loose_files.cpp native\src\loose_manifest.cpp native\src\pac_archive.cpp /Fo:build\native-build\ build/generated/proxy-version.res /link /Brepro user32.lib /DEF:native\proxy.def /OUT:build\native-build\dinput8.dll /IMPLIB:build\native-build\dinput8.lib /PDB:build\native-build\dinput8.pdb
if errorlevel 1 exit /b 1
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT native\src\proxy_smoke.cpp /Fo:build\native-build\ /Fe:build\native-build\proxy-smoke.exe /link dxguid.lib
exit /b %errorlevel%
