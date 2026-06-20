@echo off
:: =====================================================================
:: NETWORK FIX untuk MS-MPI di hotspot/workgroup.
:: Penyebab error 1726 "SMPD Manager Instance": IPv6 didahulukan +
:: adapter multi-homed (Tailscale, 169.254 APIPA) membuat smpd manager
:: bind/connect ke alamat yang salah. Di sini kita paksa IPv4 tunggal.
:: Reversible lewat deactivate.bat.
:: Dipanggil "call" oleh run_master/run_slaveN.bat SEBELUM smpd start.
:: =====================================================================

echo [net] Mematikan IPv6 di semua adapter...
powershell -NoProfile -Command "Get-NetAdapter | Disable-NetAdapterBinding -ComponentID ms_tcpip6 -ErrorAction SilentlyContinue"

echo [net] Menonaktifkan adapter Tailscale (kalau ada)...
powershell -NoProfile -Command "Disable-NetAdapter -InterfaceAlias 'Tailscale' -Confirm:$false -ErrorAction SilentlyContinue"

echo [net] Menonaktifkan adapter ber-IP 169.254.x (APIPA/mati)...
powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like '169.254.*' } | ForEach-Object { Disable-NetAdapter -InterfaceIndex $_.InterfaceIndex -Confirm:$false -ErrorAction SilentlyContinue }"

echo [net] IPv4 yang tersisa (idealnya hanya satu, 10.190.116.x):
powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -ne '127.0.0.1' } | Format-Table IPAddress,InterfaceAlias -AutoSize"
