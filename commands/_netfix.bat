@echo off
:: =====================================================================
:: NETWORK FIX untuk MS-MPI di hotspot/workgroup.
:: Penyebab error 1726 "SMPD Manager Instance": IPv6 didahulukan +
:: adapter multi-homed (Tailscale, 169.254 APIPA, WiFi-Direct virtual)
:: membuat smpd manager bind/connect ke alamat yang salah. Di sini kita
:: paksa IPv4 tunggal di subnet cluster.
:: AUTO: adapter pemegang default-route TIDAK PERNAH di-disable.
:: Reversible lewat deactivate.bat (termasuk Start-Service Tailscale).
:: Dipanggil "call" oleh run_master/run_slaveN.bat SEBELUM smpd start.
:: =====================================================================

echo [net] Menghentikan Tailscale (service + node)...
powershell -NoProfile -Command "Stop-Service -Name 'Tailscale' -Force -ErrorAction SilentlyContinue; $ts='C:\Program Files\Tailscale\tailscale.exe'; if(Test-Path $ts){ & $ts down 2>$null }; Get-Service *tailscale* -ErrorAction SilentlyContinue | ForEach-Object { Write-Host ('[net]   ' + $_.Name + ' = ' + $_.Status) }"

echo [net] Mematikan IPv6 di semua adapter...
powershell -NoProfile -Command "Get-NetAdapter | Disable-NetAdapterBinding -ComponentID ms_tcpip6 -ErrorAction SilentlyContinue"

echo [net] Menonaktifkan adapter sampah (Tailscale / 169.254 / WiFi-Direct), kecuali default-route...
powershell -NoProfile -Command "$keep=(Get-NetRoute -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue | Sort-Object RouteMetric | Select-Object -First 1).InterfaceIndex; $apipa=@(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -like '169.254.*' } | Select-Object -ExpandProperty InterfaceIndex); $bad=Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object { $_.ifIndex -ne $keep -and ( $_.Name -like '*Tailscale*' -or $_.InterfaceDescription -like '*Tailscale*' -or $_.Name -like 'Local Area Connection*' -or ($apipa -contains $_.ifIndex) ) }; foreach($a in $bad){ Write-Host ('[net]   disable ' + $a.Name + ' (ifIndex ' + $a.ifIndex + ')'); Disable-NetAdapter -InputObject $a -Confirm:$false -ErrorAction SilentlyContinue }; Write-Host ('[net] default-route ifIndex dipertahankan = ' + $keep)"

echo [net] IPv4 yang tersisa (idealnya satu, di subnet hotspot/LAN):
powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.IPAddress -notlike '169.254.*' } | Format-Table IPAddress,InterfaceAlias -AutoSize"
