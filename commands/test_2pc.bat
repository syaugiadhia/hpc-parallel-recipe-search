@echo off
:: =====================================================================
:: TEST 2-PC (sekali klik). Menjalankan "mpiexec ... hostname" SEBAGAI
:: akun cluster (hp) supaya cocok dgn smpd -> tidak kena 'error 5'.
:: IP slave diambil dari S1_IP / S2_IP di _config.bat (isi yang SEKARANG).
:: Klik kanan -> Run as Administrator.
:: =====================================================================
title TEST 2PC - Little Alchemy HPC MPI
setlocal

net session >nul 2>&1
if not "%errorLevel%"=="0" (
    echo Meminta hak akses Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

call "%~dp0_config.bat"

echo ============================================================
echo  PREFLIGHT 2-PC  (sebagai %CLUSTER_USER%, cocok dgn smpd)
echo  Host: localhost + S1_IP(%S1_IP%) + S2_IP(%S2_IP%)
echo ============================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_test2pc.ps1"
echo.
echo (Kalau IP slave berubah / ganti hotspot, edit S1_IP / S2_IP di _config.bat.)
pause
endlocal
