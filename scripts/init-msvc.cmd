@echo off
set "PATCH_SELECTED_VS="
for /f "usebackq delims=" %%i in (`powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0find-msvc.ps1"`) do set "PATCH_SELECTED_VS=%%i"
if not defined PATCH_SELECTED_VS exit /b 1
call "%PATCH_SELECTED_VS%\VC\Auxiliary\Build\vcvarsall.bat" %1 >nul
exit /b %errorlevel%
