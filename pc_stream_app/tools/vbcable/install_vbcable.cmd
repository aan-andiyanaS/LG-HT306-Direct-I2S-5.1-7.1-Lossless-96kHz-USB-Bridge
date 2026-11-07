@echo off
cd /d "%~dp0"
start /wait "" "VBCABLE_Setup_x64.exe"
powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0..\setup_ht306_audio.ps1"
