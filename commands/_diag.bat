@echo off
:: =====================================================================
:: DIAGNOSTIK MS-MPI (jalankan di PC mana saja: master / slave).
:: Menunjukkan penyebab error 1726: siapa pemilik port 8677, apakah
:: MsMpiLaunchSvc masih hidup (rebut 8677), proses smpd, IPv4, firewall,
:: IPv6. Salin SELURUH output ini lalu kirim balik.
:: Jalankan: klik kanan -> Run as Administrator.
:: =====================================================================
title DIAG - Little Alchemy HPC MPI
setlocal

net session >nul 2>&1
if not "%errorLevel%"=="0" (
    echo Meminta hak akses Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

call "%~dp0_config.bat"

echo ============================================================
echo  DIAGNOSTIK di  %COMPUTERNAME%
echo ============================================================

echo.
echo --- [1] MsMpiLaunchSvc (HARUS Stopped untuk pakai smpd -d) ---
powershell -NoProfile -Command "$s=Get-Service MsMpiLaunchSvc -ErrorAction SilentlyContinue; if($s){ Write-Host ('   Status=' + $s.Status + '  StartType=' + $s.StartType) } else { Write-Host '   (service tidak ada)' }"

echo.
echo --- [2] Pemilik port 8677 (harus smpd.exe dari MSMPI_BIN, BUKAN service) ---
powershell -NoProfile -Command "$c=Get-NetTCPConnection -LocalPort 8677 -State Listen -ErrorAction SilentlyContinue; if($c){ foreach($x in $c){ $p=Get-Process -Id $x.OwningProcess -ErrorAction SilentlyContinue; Write-Host ('   LISTEN ' + $x.LocalAddress + ':8677  PID=' + $x.OwningProcess + '  proc=' + ($(if($p){$p.ProcessName}else{'?'})) + '  path=' + ($(if($p){$p.Path}else{'?'}))) } } else { Write-Host '   TIDAK ada yang LISTEN di 8677 (smpd belum jalan!)' }"

echo.
echo --- [3] Semua proses smpd ---
powershell -NoProfile -Command "$ps=Get-Process smpd -ErrorAction SilentlyContinue; if($ps){ $ps | ForEach-Object { Write-Host ('   PID=' + $_.Id + '  path=' + $_.Path) } } else { Write-Host '   (tidak ada proses smpd)' }"

echo.
echo --- [4] IPv4 aktif (idealnya SATU, di subnet cluster) ---
powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.IPAddress -notlike '169.254.*' } | Format-Table IPAddress,InterfaceAlias -AutoSize | Out-String | Write-Host"

echo --- [5] Firewall (harus semua False / off) ---
powershell -NoProfile -Command "Get-NetFirewallProfile | Select-Object Name,Enabled | Format-Table -AutoSize | Out-String | Write-Host"

echo --- [6] IPv6 binding yang masih ON ---
powershell -NoProfile -Command "$on=Get-NetAdapterBinding -ComponentID ms_tcpip6 -ErrorAction SilentlyContinue | Where-Object { $_.Enabled }; if($on){ $on | ForEach-Object { Write-Host ('   ' + $_.Name) } } else { Write-Host '   (IPv6 sudah off di semua adapter)' }"

echo --- [7] Tailscale service ---
powershell -NoProfile -Command "$t=Get-Service *tailscale* -ErrorAction SilentlyContinue; if($t){ $t | ForEach-Object { Write-Host ('   ' + $_.Name + ' = ' + $_.Status) } } else { Write-Host '   (tidak ada)' }"

echo.
echo ============================================================
echo  Selesai. SALIN SEMUA teks di atas dan kirim balik.
echo ============================================================
pause
endlocal
