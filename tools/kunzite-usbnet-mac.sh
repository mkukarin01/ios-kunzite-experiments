#!/bin/bash
# macOS host side of the phone's USB Ethernet (kernel sweetusb=2, patch 0909).
#   tools/kunzite-usbnet-mac.sh [LOGFILE [udp-log.py options: --tail N, --no-replay]]
# Waits for the phone's CDC-ECM interface (host MAC 02:00:5e:77:ee:01, macOS
# drives CDC-ECM natively), makes sure it has 192.168.7.1/24 (the phone is
# 192.168.7.2) - once, persistently, as the interface's network service set
# to manual (sudo) - then prints the live kernel console the phone streams to
# udp/5140 and appends it to LOGFILE (default temp/ios-m8/usblog/live-DATE.txt).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
mac=02:00:5e:77:ee:01
log=${1:-$here/../../../temp/ios-m8/usblog/live-$(date +%Y%m%d-%H%M%S).txt}
mkdir -p "$(dirname "$log")"

find_if() {
	for i in $(ifconfig -l); do
		ifconfig "$i" 2>/dev/null | grep -qi "ether $mac" && { echo "$i"; return 0; }
	done
	return 1
}
echo "waiting for the phone's USB Ethernet ($mac)..." >&2
until ifname=$(find_if); do sleep 1; done
echo "phone interface: $ifname" >&2
if ! ifconfig "$ifname" | grep -q "inet 192.168.7.1 "; then
	port=$(networksetup -listallhardwareports |
		awk -v d="$ifname" '/^Hardware Port:/ {p = substr($0, 16)} /^Device:/ {if ($2 == d) print p}')
	if [ -n "$port" ] && networksetup -listallnetworkservices | grep -qxF "$port"; then
		echo "setting network service \"$port\" to manual 192.168.7.1/24 (sudo, persistent)" >&2
		sudo networksetup -setmanual "$port" 192.168.7.1 255.255.255.0
	else
		echo "no network service for $ifname; adding 192.168.7.1 as an alias (sudo, until unplug)" >&2
		sudo ifconfig "$ifname" alias 192.168.7.1 netmask 255.255.255.0
	fi
	for _ in $(seq 10); do ifconfig "$ifname" | grep -q "inet 192.168.7.1 " && break; sleep 1; done
fi
ifconfig "$ifname" | grep "inet " >&2 || true
echo "$ifname: 192.168.7.1 <-> phone 192.168.7.2 (ping 192.168.7.2); live log -> $log" >&2
exec python3 "$here/udp-log.py" "$log" "${@:2}"
