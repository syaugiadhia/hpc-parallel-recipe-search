"""Cetak IP lokal node ini yang dipakai untuk cluster MPI.

Argumen opsional: IP peer (slave). Kalau diberi, pilih IP lokal yang SE-/24 dengan peer
(paling andal di host multi-homed: master bisa punya internet di adapter lain). Kalau tidak
ada peer / tidak ada yang cocok, pakai IP default-route (UDP-connect trick, tanpa sorting).
APIPA 169.254.x selalu dibuang. Dipakai oleh _netmask.bat dan _test_2pc.bat.
"""
import ipaddress
import os
import re
import socket
import subprocess
import sys


def default_route_ipv4():
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("8.8.8.8", 80))
            value = sock.getsockname()[0]
        ip = ipaddress.ip_address(value)
        if ip.version == 4 and not ip.is_loopback and not ip.is_link_local:
            return value
    except OSError:
        pass
    return None


def local_ipv4_addresses():
    addresses = set()
    try:
        _, _, values = socket.gethostbyname_ex(socket.gethostname())
        addresses.update(values)
    except OSError:
        pass
    route = default_route_ipv4()
    if route:
        addresses.add(route)
    if os.name == "nt":
        try:
            output = subprocess.check_output(["ipconfig"], text=True, encoding="utf-8", errors="ignore", timeout=3)
            for value in re.findall(r"IPv4[^:\n]*:\s*([0-9]+(?:\.[0-9]+){3})", output):
                addresses.add(value)
        except Exception:
            pass
    cleaned = []
    for value in addresses:
        try:
            ip = ipaddress.ip_address(value)
        except ValueError:
            continue
        if ip.version == 4 and not ip.is_loopback and not ip.is_link_local:
            cleaned.append(value)
    return cleaned


def subnet_base_24(address):
    return str(ipaddress.ip_network(f"{address}/24", strict=False).network_address)


def main():
    peer = sys.argv[1].strip() if len(sys.argv) > 1 else ""
    if peer:
        try:
            ipaddress.ip_address(peer)
            peer_base = subnet_base_24(peer)
            for address in local_ipv4_addresses():
                if subnet_base_24(address) == peer_base:
                    print(address)
                    return
        except ValueError:
            pass
    route = default_route_ipv4()
    if route:
        print(route)
        return
    for address in local_ipv4_addresses():
        print(address)
        return
    print("")


if __name__ == "__main__":
    main()
