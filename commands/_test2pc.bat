@echo off
:: =====================================================================
:: PREFLIGHT 2-PC (jalankan di MASTER). Gerbang penentu sebelum GUI.
:: Memastikan jalur smpd master<->slave benar2 jalan (no error 1726)
:: dengan perintah paling minimal: mpiexec ... hostname.
:: Syarat: run_master.bat sudah jalan di master & run_slaveN.bat di slave,
:: kedua window SMPD hidup. Jalankan: Run as Administrator.
:: =====================================================================
title PREFLIGHT 2-PC - Little Alchemy HPC MPI
setlocal

net session >nul 2>&1
if not "%errorLevel%"=="0" (
    echo Meminta hak akses Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

call "%~dp0_config.bat"
set "MPIEXEC=%MSMPI_BIN%\mpiexec.exe"

if not defined S1_IP (
    echo [ERROR] S1_IP belum diisi di _config.bat. Isi IP slave dulu, mis. set "S1_IP=192.168.2.101"
    pause & exit /b
)

echo ============================================================
echo  PREFLIGHT 2-PC: master %COMPUTERNAME%  ^<-^>  slave %S1_IP%
echo ============================================================

echo [cek 1] TCP ke smpd slave (%S1_IP%:8677)
powershell -NoProfile -Command "$r=Test-NetConnection '%S1_IP%' -Port 8677 -WarningAction SilentlyContinue; if($r.TcpTestSucceeded){ Write-Host '   OK - smpd slave terjangkau' } else { Write-Host '   GAGAL - smpd slave tidak terjangkau (run_slaveN.bat sudah jalan? firewall? IP benar?)' }"

echo.
echo [cek 2] mpiexec -hosts 2 localhost 1 %S1_IP% 1 hostname
echo         (gerbang utama: harus cetak 2 hostname, tanpa error 1726)
"%MPIEXEC%" -hosts 2 localhost 1 %S1_IP% 1 hostname
set "RC=%errorlevel%"

echo.
if "%RC%"=="0" (
  echo ============================================================
  echo  LULUS. Jalur MPI master^<-^>slave sehat. Lanjut pakai GUI Run/Compare.
  echo ============================================================
) else (
  echo ============================================================
  echo  GAGAL ^(exit %RC%^). Kalau "error 1726": jaringan slave belum bersih.
  echo  Di PC SLAVE jalankan ULANG run_slaveN.bat ^(versi baru: Tailscale/IPv6
  echo  dimatikan, smpd restart^), pastikan IPv4 slave tinggal SATU = %S1_IP%.
  echo ============================================================
)

echo.
pause
endlocal
