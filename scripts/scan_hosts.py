#!/usr/bin/env python3
import argparse
import concurrent.futures
import ipaddress
import os
import socket
import subprocess
import sys


def local_subnets():
    networks = []
    seen = set()
    try:
        _, _, addresses = socket.gethostbyname_ex(socket.gethostname())
    except OSError:
        addresses = []
    for address in addresses:
        try:
            ip = ipaddress.ip_address(address)
        except ValueError:
            continue
        if ip.version != 4 or ip.is_loopback or ip.is_link_local:
            continue
        network = ipaddress.ip_network(f"{ip}/24", strict=False)
        if str(network) not in seen:
            seen.add(str(network))
            networks.append(network)
    return networks


def ping_host(host, timeout_ms):
    if os.name == "nt":
        command = ["ping", "-n", "1", "-w", str(timeout_ms), str(host)]
    else:
        seconds = max(1, int((timeout_ms + 999) / 1000))
        command = ["ping", "-c", "1", "-W", str(seconds), str(host)]
    try:
        result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=max(1, timeout_ms / 1000.0 + 1))
        return result.returncode == 0
    except Exception:
        return False


def scan_targets(networks, timeout_ms, workers):
    targets = []
    for network in networks:
        targets.extend(str(host) for host in network.hosts())
    found = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        future_to_host = {executor.submit(ping_host, host, timeout_ms): host for host in targets}
        for future in concurrent.futures.as_completed(future_to_host):
            host = future_to_host[future]
            if future.result():
                found.append(host)
    return sorted(found, key=lambda value: tuple(int(part) for part in value.split(".")))


def write_hostfile(path, hosts, include_localhost):
    lines = []
    if include_localhost:
        lines.append(f"localhost slots={os.cpu_count() or 1}")
    for host in hosts:
        lines.append(f"{host} slots=1")
    content = "\n".join(lines) + ("\n" if lines else "")
    if path:
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(content)
    else:
        print(content, end="")


def main():
    parser = argparse.ArgumentParser(description="Scan local LAN hosts and print an MPI hostfile.")
    parser.add_argument("--subnet", action="append", help="CIDR subnet to scan, e.g. 192.168.1.0/24. May be repeated.")
    parser.add_argument("--timeout-ms", type=int, default=300)
    parser.add_argument("--workers", type=int, default=64)
    parser.add_argument("--output", help="Optional hostfile output path.")
    parser.add_argument("--no-localhost", action="store_true", help="Do not add localhost to the hostfile.")
    args = parser.parse_args()

    if args.subnet:
        networks = [ipaddress.ip_network(value, strict=False) for value in args.subnet]
    else:
        networks = local_subnets()

    if not networks and not args.no_localhost:
        write_hostfile(args.output, [], True)
        return 0
    if not networks:
        print("No local IPv4 subnet found. Pass --subnet manually.", file=sys.stderr)
        return 1

    hosts = scan_targets(networks, args.timeout_ms, max(1, args.workers))
    write_hostfile(args.output, hosts, not args.no_localhost)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
