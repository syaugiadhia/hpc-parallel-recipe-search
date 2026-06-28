@echo off
:: =====================================================================
:: RESET TOTAL: kembalikan PC ini ke kondisi AWAL sepenuhnya.
::   - tutup smpd + GUI
::   - firewall ON
::   - hapus registry LocalAccountTokenFilterPolicy
::   - hapus semua kredensial cmdkey (slave host + IP)
::   - kembalikan MS-MPI Launch Service (start= demand)
::   - nyalakan IPv6 + adapter ter-disable + Tailscale
::   - bersihkan entri hosts file (# tubes-mpi)
::   - HAPUS akun cluster bikinan (tubes-mpi auto) + profilnya
:: Akun ASLI-mu (mis. 'hp' di slave-1, 'hallo' di slave-2) TIDAK disentuh.
:: Jalankan di TIAP PC saat selesai. Klik kanan -> Run as Administrator.
:: (deactivate.bat = versi ringan: TIDAK menghapus akun.)
:: =====================================================================
title RESET ALL - Little Alchemy HPC MPI
setlocal

net session >nul 2>&1
if not "%errorLevel%"=="0" (
    echo Meminta hak akses Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

call "%~dp0_config.bat"

echo ============================================================
echo  RESET TOTAL ke kondisi awal di %COMPUTERNAME%
echo ============================================================

echo [1/9] Tutup smpd + GUI
taskkill /IM smpd.exe /F >nul 2>&1
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='python.exe' OR Name='pythonw.exe'\" | Where-Object { $_.CommandLine -match 'alchemy_gui' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"

echo [2/9] Nyalakan kembali firewall
netsh advfirewall set allprofiles state on >nul

echo [3/9] Hapus registry LocalAccountTokenFilterPolicy
reg delete HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v LocalAccountTokenFilterPolicy /f >nul 2>&1

echo [4/9] Hapus kredensial cmdkey
cmdkey /delete:"%S1_HOST%" >nul 2>&1
if defined S1_IP cmdkey /delete:"%S1_IP%" >nul 2>&1
cmdkey /delete:"%S2_HOST%" >nul 2>&1
if defined S2_IP cmdkey /delete:"%S2_IP%" >nul 2>&1

echo [5/9] Kembalikan MS-MPI Launch Service (start= demand)
sc config MsMpiLaunchSvc start= demand >nul 2>&1

echo [6/9] Nyalakan kembali IPv6 di semua adapter
powershell -NoProfile -Command "Get-NetAdapter | Enable-NetAdapterBinding -ComponentID ms_tcpip6 -ErrorAction SilentlyContinue"

echo [7/9] Nyalakan adapter ter-disable + Tailscale
powershell -NoProfile -Command "Get-NetAdapter | Where-Object { $_.Status -eq 'Disabled' } | Enable-NetAdapter -Confirm:$false -ErrorAction SilentlyContinue; Start-Service -Name 'Tailscale' -ErrorAction SilentlyContinue; $ts='C:\Program Files\Tailscale\tailscale.exe'; if(Test-Path $ts){ & $ts up 2>$null }"

echo [8/9] Bersihkan entri hosts file (# tubes-mpi)
powershell -NoProfile -Command "$h=\"$env:WINDIR\System32\drivers\etc\hosts\"; $k=@(Get-Content $h -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch '# tubes-mpi' }); [System.IO.File]::WriteAllLines($h,[string[]]$k)"

echo [9/9] Hapus akun cluster bikinan (tubes-mpi auto) + profil
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_users_remove.ps1"

echo.
echo ============================================================
echo  SELESAI. Semua setting sudah dikembalikan ke kondisi awal.
echo ============================================================
pause
endlocal
