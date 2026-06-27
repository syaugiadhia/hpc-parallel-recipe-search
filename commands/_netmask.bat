@echo off
:: Set MSMPI_NETMASK ke subnet /24 dari IP default-route node ini.
:: Dipanggil SEBELUM start smpd supaya daemon smpd mewarisi env ini dan bind manager-nya
:: ke adapter yang benar (cegah error 1726 di host multi-homed / hostname resolve ke ::1).
:: Pakai python (sudah pasti ada karena GUI butuh python) agar tidak ribet escaping.

set "ALCHEMY_SUBNET="
for /f "delims=" %%I in ('python -c "import socket,ipaddress; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.connect(('8.8.8.8',80)); ip=s.getsockname()[0]; s.close(); print(ipaddress.ip_network(ip+'/24',strict=False).network_address)"') do set "ALCHEMY_SUBNET=%%I"

if defined ALCHEMY_SUBNET (
    set "MSMPI_NETMASK=%ALCHEMY_SUBNET%/255.255.255.0"
    echo [net] MSMPI_NETMASK=%ALCHEMY_SUBNET%/255.255.255.0
) else (
    echo [net] MSMPI_NETMASK tidak diset ^(tidak bisa deteksi IP default-route^).
)
