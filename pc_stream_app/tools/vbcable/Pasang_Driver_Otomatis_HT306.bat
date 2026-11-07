@echo off
NET SESSION >nul 2>&1
IF %ERRORLEVEL% NEQ 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"
echo ============================================================
echo   Memasang Driver Audio VB-Cable untuk LG HT306...
echo ============================================================
echo.
echo Silakan klik 'Install Driver' pada jendela yang muncul...
start /wait "" "VBCABLE_Setup_x64.exe"

echo.
echo Menerapkan nama LG HT306 Home Theater dan menonaktifkan 16ch...
call "%~dp0..\Sinkronkan_Nama_HT306.bat"
