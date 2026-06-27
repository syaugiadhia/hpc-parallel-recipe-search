@echo off
:: Set MSMPI_NETMASK ke subnet /24 cluster, lalu echo IP node ini.
:: Dipanggil SEBELUM start smpd supaya daemon smpd mewarisi env ini dan HANYA memakai subnet
:: hotspot (cegah error 1726: smpd advertise adapter salah seperti 169.254.x / 192.168.x / IPv6).
::
:: Sumber subnet (urutan prioritas):
::   1) %S1_IP% (IP slave dari _config.bat) -> paling andal, terutama kalau master multi-homed
::      (punya internet di adapter lain). Subnet diturunkan dari subnet slave.
::   2) IP default-route node ini (fallback).
:: Pakai python (sudah pasti ada karena GUI butuh python) agar tidak ribet escaping.

set "ALCHEMY_SUBNET="
set "ALCHEMY_NODE_IP="

:: 1) Dari S1_IP (kalau diisi di _config.bat) -> subnet /24 slave.
if defined S1_IP for /f "delims=" %%I in ('python -c "import ipaddress,sys; print(ipaddress.ip_network(sys.argv[1].strip()+'/24',strict=False).network_address)" "%S1_IP%"') do set "ALCHEMY_SUBNET=%%I"

:: IP lokal node ini yang se-subnet dengan slave (kalau ada) untuk ditampilkan; else default-route.
for /f "delims=" %%I in ('python "%~dp0_pickip.py" "%S1_IP%"') do set "ALCHEMY_NODE_IP=%%I"

:: 2) Fallback subnet dari IP default-route node ini.
if not defined ALCHEMY_SUBNET if defined ALCHEMY_NODE_IP for /f "delims=" %%I in ('python -c "import ipaddress,sys; print(ipaddress.ip_network(sys.argv[1].strip()+'/24',strict=False).network_address)" "%ALCHEMY_NODE_IP%"') do set "ALCHEMY_SUBNET=%%I"

if defined ALCHEMY_SUBNET (
    set "MSMPI_NETMASK=%ALCHEMY_SUBNET%/255.255.255.0"
    echo [net] MSMPI_NETMASK=%ALCHEMY_SUBNET%/255.255.255.0
) else (
    echo [net] MSMPI_NETMASK tidak diset ^(tidak bisa deteksi subnet^).
)
if defined ALCHEMY_NODE_IP echo [net] IP PC ini di subnet cluster: %ALCHEMY_NODE_IP%
