@echo off
title MASTER - Little Alchemy HPC MPI
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
echo  SETUP MASTER MS-MPI  (sesi dijalankan sebagai %CLUSTER_USER%)
echo ============================================================

echo [1/6] Registry: izinkan remote launch akun lokal
reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v LocalAccountTokenFilterPolicy /t REG_DWORD /d 1 /f >nul

echo [2/6] Matikan firewall (akan dikembalikan oleh deactivate.bat)
netsh advfirewall set allprofiles state off >nul

echo [3/6] Disable + Stop MS-MPI Launch Service (cegah rebut port 8677 dgn smpd -d)
sc config MsMpiLaunchSvc start= disabled >nul 2>&1
net stop MsMpiLaunchSvc >nul 2>&1

echo [4/6] Buat akun cluster '%CLUSTER_USER%' (sama di semua PC; INTI fix multi-PC)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_users.ps1"

echo [5/6] Perbaiki jaringan (matikan IPv6 + adapter sampah)
call "%~dp0_netfix.bat"

echo [6/6] Petakan hostname -^> IPv4 (hosts file)
call "%~dp0_hostsfix.bat"

echo.
echo Menjalankan smpd + GUI sebagai akun cluster '%CLUSTER_USER%'...
echo Jangan tutup window "smpd". GUI sudah role Master + engine mpi.
echo.

set "GUI_ARGS=--role master --engine mpi"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_run_session.ps1"

echo.
echo (Window ini boleh diminimize. Tutup smpd hanya saat selesai / deactivate.)
pause
endlocal
