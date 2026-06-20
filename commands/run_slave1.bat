@echo off
title SLAVE 1 - Little Alchemy HPC MPI
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
echo  SETUP SLAVE 1 MS-MPI
echo ============================================================

echo [1/4] Registry: izinkan remote launch akun lokal
reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v LocalAccountTokenFilterPolicy /t REG_DWORD /d 1 /f >nul

echo [2/4] Matikan firewall (akan dikembalikan oleh deactivate.bat)
netsh advfirewall set allprofiles state off >nul

echo [3/4] Stop MS-MPI Launch Service (cegah bentrok port 8677 dgn smpd)
net stop MsMpiLaunchSvc >nul 2>&1

echo [4/5] Perbaiki jaringan (matikan IPv6 + adapter sampah)
call "%~dp0_netfix.bat"

echo [5/5] Jalankan SMPD daemon (window terpisah)
start "SMPD" "%MSMPI_BIN%\smpd.exe" -d %SMPD_DEBUG%

echo.
echo Setup slave selesai. Membuka GUI (role Slave, %S1_SLOTS% slot, auto-start)...
echo Jangan tutup window "SMPD". Klik Accept saat master mengundang.
echo.

cd /d "%PROJECT_DIR%"
python gui\alchemy_gui.py --role slave --slots %S1_SLOTS% --autostart

endlocal
