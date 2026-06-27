@echo off
:: =====================================================================
:: SELF-TEST MS-MPI di SATU mesin. Membuktikan jalur remote-launch smpd
:: + bentuk perintah yang dipakai GUI sudah benar, SEBELUM coba 2-PC.
:: Meng-address mesin ini lewat HOSTNAME (bukan localhost) menempuh
:: jalur smpd remote-connect yang sama persis dengan slave nyata.
:: Jalankan: klik-kanan -> Run as Administrator.
:: =====================================================================
title SELFTEST - Little Alchemy HPC MPI
setlocal

:: ---------- Self-elevate ke Administrator ----------
net session >nul 2>&1
if not "%errorLevel%"=="0" (
    echo Meminta hak akses Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

call "%~dp0_config.bat"
set "MPIEXEC=%MSMPI_BIN%\mpiexec.exe"
set "FAIL=0"

echo ============================================================
echo  SELF-TEST MS-MPI (1 mesin) - host ini: %COMPUTERNAME%
echo ============================================================

echo [setup] Registry + firewall off + stop MsMpiLaunchSvc + netfix
reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v LocalAccountTokenFilterPolicy /t REG_DWORD /d 1 /f >nul
netsh advfirewall set allprofiles state off >nul
net stop MsMpiLaunchSvc >nul 2>&1
call "%~dp0_netfix.bat"

echo [setup] (re)start SMPD daemon dari PROJECT_DIR
taskkill /IM smpd.exe /F >nul 2>&1
start "SMPD" /D "%PROJECT_DIR%" "%MSMPI_BIN%\smpd.exe" -d %SMPD_DEBUG%
ping -n 3 127.0.0.1 >nul

echo.
echo [TES 1] mpiexec -n 2 hostname  (sanity lokal)
"%MPIEXEC%" -n 2 hostname
if errorlevel 1 ( echo   ^>^> TES 1 GAGAL & set "FAIL=1" ) else ( echo   ^>^> TES 1 OK )

echo.
echo [TES 2] mpiexec -hosts 2 localhost 1 %COMPUTERNAME% 1 hostname  (jalur remote smpd)
"%MPIEXEC%" -hosts 2 localhost 1 %COMPUTERNAME% 1 hostname
if errorlevel 1 ( echo   ^>^> TES 2 GAGAL ^(cek smpd/firewall/registry^) & set "FAIL=1" ) else ( echo   ^>^> TES 2 OK )

echo.
echo [TES 3] alchemy_mpi.exe lintas-rank via localhost + %COMPUTERNAME%
"%MPIEXEC%" -hosts 2 localhost 2 %COMPUTERNAME% 2 -wdir "%PROJECT_DIR%" "%PROJECT_DIR%\build\alchemy_mpi.exe" --data "%PROJECT_DIR%\data\recipes.json" --tiers "%PROJECT_DIR%\data\tiers.json" --algorithm bfs --mode multiple --render json --output "%PROJECT_DIR%\results\cli_selftest" --target Brick --limit 5 --split-depth 1
if errorlevel 1 ( echo   ^>^> TES 3 GAGAL & set "FAIL=1" ) else ( echo   ^>^> TES 3 OK )

echo.
if "%FAIL%"=="0" (
  echo ============================================================
  echo  SEMUA TES LULUS. Mekanisme MS-MPI + bentuk perintah BENAR.
  echo  Lihat baris "Rank hostnames:" di TES 3 = bukti lintas-rank.
  echo  Untuk 2-PC: run_master.bat / run_slaveN.bat di tiap PC, lalu
  echo  dari master: mpiexec -hosts 2 localhost 1 ^<hostname-slave^> 1 hostname
  echo ============================================================
) else (
  echo ============================================================
  echo  ADA TES GAGAL atau menggantung. Lihat pesan + window SMPD.
  echo  Kalau TES 2 hang/1726: jalankan _netfix lagi / cek adapter.
  echo ============================================================
)

echo.
echo Setelah selesai, jalankan deactivate.bat untuk kembalikan firewall/IPv6/adapter.
pause
endlocal
