@echo off
:: =====================================================================
:: NETWORK FIX untuk MS-MPI di hotspot/workgroup.
:: Penyebab error 1726 "SMPD Manager Instance": IPv6 didahulukan +
:: adapter multi-homed (Tailscale, 169.254 APIPA) membuat smpd manager
:: bind/connect ke alamat yang salah. Di sini kita paksa IPv4 tunggal.
:: AUTO: adapter pemegang default-route TIDAK PERNAH di-disable (biar
:: koneksi aktif tidak putus). Reversible lewat deactivate.bat.
:: Dipanggil "call" oleh run_master/run_slaveN.bat SEBELUM smpd start.
:: =====================================================================

echo [net] Mematikan IPv6 di semua adapter...
powershell -NoProfile -Command "Get-NetAdapter | Disable-NetAdapterBinding -ComponentID ms_tcpip6 -ErrorAction SilentlyContinue"

echo [net] Menonaktifkan Tailscale + adapter 169.254.x (kecuali default-route)...
powershell -NoProfile -Command "$keep=(Get-NetRoute -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue | Sort-Object RouteMetric | Select-Object -First 1).InterfaceIndex; Get-NetAdapter | Where-Object { $_.InterfaceAlias -like '*Tailscale*' -and $_.ifIndex -ne $keep } | Disable-NetAdapter -Confirm:$false -ErrorAction SilentlyContinue; Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like '169.254.*' -and $_.InterfaceIndex -ne $keep } | ForEach-Object { Disable-NetAdapter -InterfaceIndex $_.InterfaceIndex -Confirm:$false -ErrorAction SilentlyContinue }; Write-Host ('[net] default-route ifIndex dipertahankan = ' + $keep)"

echo [net] IPv4 yang tersisa (idealnya satu, di subnet hotspot/LAN):
powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.IPAddress -notlike '169.254.*' } | Format-Table IPAddress,InterfaceAlias -AutoSize"
