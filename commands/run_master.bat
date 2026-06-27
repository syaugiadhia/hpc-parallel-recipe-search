@echo off
title MASTER - Little Alchemy HPC MPI
setlocal

net session >nul 2>&1
if not "%errorLevel%"=="0" (
    echo Meminta hak akses Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

call "%~dp0_config.bat"

if not exist "%MSMPI_BIN%\smpd.exe" (
    echo [error] smpd.exe tidak ditemukan di "%MSMPI_BIN%".
    echo         Install MS-MPI atau perbaiki MSMPI_BIN di commands\_config.bat.
    pause
    exit /b 1
)

echo
echo  SETUP MASTER MS-MPI
echo

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

echo [5/6] Perbaiki jaringan (matikan IPv6 + adapter sampah)
call "%~dp0_netfix.bat"

echo [6/6] Set MSMPI_NETMASK lalu jalankan SMPD daemon (window terpisah)
call "%~dp0_netmask.bat"
echo     ^>^> IP master ini untuk dipakai/diberitahu: %ALCHEMY_NODE_IP%
start "SMPD" "%MSMPI_BIN%\smpd.exe" -d %SMPD_DEBUG%

echo.
echo Setup master selesai. Membuka GUI (role Master, engine mpi)...
echo Jangan tutup window "SMPD".
echo.

cd /d "%PROJECT_DIR%"
python gui\alchemy_gui.py --role master --engine mpi

endlocal
