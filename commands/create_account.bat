@echo off
:: =====================================================================
:: TOMBOL: buat akun cluster (CLUSTER_USER/CLUSTER_PASS dari _config.bat)
:: di PC ini. Berguna di slave yang akun aslinya BEDA (mis. 'hallo') supaya
:: tetap punya akun bersama 'hp' untuk MS-MPI. Aman: kalau akun sudah ada,
:: dibiarkan. Hapus lagi dengan remove_users.bat / reset_all.bat.
:: (Catatan: run_master.bat / run_slaveN.bat juga sudah otomatis bikin ini.)
:: Klik kanan -> Run as Administrator.
:: =====================================================================
title CREATE ACCOUNT - akun cluster MS-MPI
setlocal

net session >nul 2>&1
if not "%errorLevel%"=="0" (
    echo Meminta hak akses Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

call "%~dp0_config.bat"

echo ============================================================
echo  BUAT AKUN CLUSTER '%CLUSTER_USER%' di %COMPUTERNAME%
echo ============================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_users.ps1"
echo.
echo Selesai. Akun '%CLUSTER_USER%' siap dipakai untuk MS-MPI.
pause
endlocal
