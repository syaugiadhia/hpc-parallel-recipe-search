@echo off
:: =====================================================================
:: HAPUS akun cluster yang dibuat otomatis (tubes-mpi auto) di PC ini.
:: Jalankan di tiap PC saat sudah selesai memakai cluster.
:: Aman: hanya menghapus akun bertanda, bukan akun aslimu.
:: Klik kanan -> Run as Administrator.
:: =====================================================================
title REMOVE USERS - Little Alchemy HPC MPI
setlocal

net session >nul 2>&1
if not "%errorLevel%"=="0" (
    echo Meminta hak akses Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo ============================================================
echo  HAPUS AKUN CLUSTER (tubes-mpi auto) di %COMPUTERNAME%
echo ============================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_users_remove.ps1"
echo.
pause
endlocal
