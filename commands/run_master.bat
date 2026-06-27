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
echo  SETUP MASTER MS-MPI
echo ============================================================

echo [1/5] Registry: izinkan remote launch akun lokal
reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v LocalAccountTokenFilterPolicy /t REG_DWORD /d 1 /f >nul

echo [2/5] Matikan firewall (akan dikembalikan oleh deactivate.bat)
netsh advfirewall set allprofiles state off >nul

echo [3/5] Stop MS-MPI Launch Service (cegah bentrok port 8677 dgn smpd)
net stop MsMpiLaunchSvc >nul 2>&1

echo [4/5] Daftarkan kredensial slave (cmdkey)
cmdkey /add:"%S1_HOST%" /user:"%S1_USER%" /pass:"%S1_PASS%" >nul
echo     - %S1_HOST% (%S1_USER%)
if defined S1_IP cmdkey /add:"%S1_IP%" /user:"%S1_USER%" /pass:"%S1_PASS%" >nul
cmdkey /add:"%S2_HOST%" /user:"%S2_USER%" /pass:"%S2_PASS%" >nul
echo     - %S2_HOST% (%S2_USER%)
if defined S2_IP cmdkey /add:"%S2_IP%" /user:"%S2_USER%" /pass:"%S2_PASS%" >nul

echo [5/7] Perbaiki jaringan (matikan IPv6 + adapter sampah)
call "%~dp0_netfix.bat"

echo [6/7] Petakan hostname -^> IPv4 (hosts file)
call "%~dp0_hostsfix.bat"

echo [7/7] Restart SMPD daemon bersih (window terpisah, cwd = PROJECT_DIR)
taskkill /IM smpd.exe /F >nul 2>&1
start "SMPD" /D "%PROJECT_DIR%" "%MSMPI_BIN%\smpd.exe" -d %SMPD_DEBUG%

echo.
echo Setup master selesai. Membuka GUI (role Master, engine mpi)...
echo Jangan tutup window "SMPD".
echo.

cd /d "%PROJECT_DIR%"
python gui\alchemy_gui.py --role master --engine mpi

endlocal
