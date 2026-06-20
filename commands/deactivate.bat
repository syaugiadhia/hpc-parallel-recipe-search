@echo off
title CLEANUP - Little Alchemy HPC MPI
setlocal

:: ---------- Self-elevate ke Administrator ----------
net session >nul 2>&1
if not "%errorLevel%"=="0" (
    echo Meminta hak akses Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

:: ---------- Muat konfigurasi ----------
call "%~dp0_config.bat"

echo ============================================================
echo  MENGEMBALIKAN PENGATURAN KEAMANAN
echo ============================================================

echo [1/4] Nyalakan kembali firewall
netsh advfirewall set allprofiles state on >nul

echo [2/4] Hapus registry LocalAccountTokenFilterPolicy
reg delete HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v LocalAccountTokenFilterPolicy /f >nul 2>&1

echo [3/4] Hapus kredensial cmdkey
cmdkey /delete:"%S1_HOST%" >nul 2>&1
if defined S1_IP cmdkey /delete:"%S1_IP%" >nul 2>&1
cmdkey /delete:"%S2_HOST%" >nul 2>&1
if defined S2_IP cmdkey /delete:"%S2_IP%" >nul 2>&1

echo [4/4] Matikan SMPD daemon
taskkill /IM smpd.exe /F >nul 2>&1

echo.
echo Pengaturan keamanan sudah dikembalikan.
pause
endlocal
