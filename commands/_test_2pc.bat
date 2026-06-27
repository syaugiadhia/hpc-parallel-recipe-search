@echo off
title TEST 2-PC - Little Alchemy HPC MPI
setlocal

:: Minta hak Administrator (butuh taskkill/net stop/start smpd).
net session >nul 2>&1
if not "%errorLevel%"=="0" (
    echo Meminta hak akses Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

call "%~dp0_config.bat"

if not exist "%MSMPI_BIN%\smpd.exe" (
    echo [error] smpd.exe tidak ditemukan di "%MSMPI_BIN%". Install MS-MPI / perbaiki _config.bat.
    pause
    exit /b 1
)
set "EXE=%PROJECT_DIR%\build\alchemy_mpi.exe"

echo.
echo  TES REMOTE MS-MPI (2 PC) - membuktikan mpiexec bisa launch ke slave
echo.

echo [1/6] Matikan smpd lama (yang mungkin tanpa MSMPI_NETMASK)...
taskkill /IM smpd.exe /F >nul 2>&1

echo [2/6] Stop MsMpiLaunchSvc (bebaskan port 8677)...
net stop MsMpiLaunchSvc >nul 2>&1

echo [3/6] Hitung MSMPI_NETMASK dari subnet slave (S1_IP) + IP master...
call "%~dp0_netmask.bat"
set "MASTER_IP=%ALCHEMY_NODE_IP%"

echo [4/6] (best-effort) Bersihkan IPv6/APIPA...
call "%~dp0_netfix.bat" >nul 2>&1

echo [5/6] Start smpd daemon (window terpisah) + tunggu 8677...
start "SMPD-test" "%MSMPI_BIN%\smpd.exe" -d %SMPD_DEBUG%
set /a _tries=0
:waitsmpd
python -c "import socket; socket.create_connection(('127.0.0.1',8677),timeout=0.5).close()" >nul 2>&1
if "%errorlevel%"=="0" goto smpdready
set /a _tries+=1
if %_tries% geq 20 (
    echo [warn] smpd belum listen di 8677, lanjut saja...
    goto smpdready
)
ping -n 1 -w 400 127.0.0.1 >nul
goto waitsmpd
:smpdready

:: Tentukan IP slave: S1_IP kalau ada, kalau tidak minta input.
set "SLAVE_IP=%S1_IP%"
if not defined SLAVE_IP set /p "SLAVE_IP=Masukkan IP slave (mis. 172.20.10.2): "
if not defined MASTER_IP set /p "MASTER_IP=Tidak bisa deteksi IP master, masukkan manual: "

echo.
echo [6/6] Jalankan tes: master=%MASTER_IP%  slave=%SLAVE_IP%  netmask=%MSMPI_NETMASK%
echo ---------------------------------------------------------------
mpiexec -genv MSMPI_NETMASK %MSMPI_NETMASK% -hosts 2 %MASTER_IP% 1 %SLAVE_IP% 1 hostname
set "RC=%errorlevel%"
echo ---------------------------------------------------------------
echo exit=%RC%
echo.
echo Cara baca hasil:
echo   - Muncul DUA hostname (master ^& slave)  = LULUS. Remote MPI jalan, lanjut GUI Run.
echo   - error 1722                             = smpd di IP itu mati (pastikan run_slaveN.bat hidup di slave).
echo   - error 1726                             = smpd advertise adapter salah. Cek MSMPI_NETMASK di atas
echo                                              harus = subnet hotspot (mis. 172.20.10.0/...). Pastikan
echo                                              S1_IP di _config.bat benar, lalu jalankan ulang script ini.
echo   - Hang lama lalu gagal                   = slave tak terjangkau (beda subnet / firewall slave).
echo.
echo Tutup window "SMPD-test" kalau sudah selesai (atau biarkan untuk lanjut GUI).
pause
endlocal
