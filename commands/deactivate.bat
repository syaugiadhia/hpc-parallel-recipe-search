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

echo [4/6] Matikan SMPD daemon
taskkill /IM smpd.exe /F >nul 2>&1

echo [5/6] Aktifkan kembali IPv6 di semua adapter
powershell -NoProfile -Command "Get-NetAdapter | Enable-NetAdapterBinding -ComponentID ms_tcpip6 -ErrorAction SilentlyContinue"

echo [6/6] Aktifkan kembali adapter yang ter-disable (Tailscale, dll)
powershell -NoProfile -Command "Get-NetAdapter | Where-Object { $_.Status -eq 'Disabled' } | Enable-NetAdapter -Confirm:$false -ErrorAction SilentlyContinue"

echo.
echo Pengaturan keamanan dan jaringan sudah dikembalikan.
pause
endlocal
