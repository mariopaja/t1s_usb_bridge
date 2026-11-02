#!/bin/bash
# Copyright (c) 2026 Mario Paja
# SPDX-License-Identifier: Apache-2.0
#
# Put the UNI Clicker USB CDC NCM link (and the 10BASE-T1S segment bridged
# behind it) into the Wi-Fi LAN of this host, without bridging the Wi-Fi
# interface: parprouted answers ARP on both sides and adds host routes,
# dhcp-helper relays DHCP from the USB side to the LAN DHCP server.
#
# Usage (as root): t1s-lan-proxy.sh up|down|run|status
#   run: stay in the foreground and bring the proxy up whenever the
#        UNI Clicker interface (re)appears; used by t1s-lan-proxy.service
# Requires: apt install parprouted dhcp-helper
#           systemctl disable --now dhcp-helper   (the script starts its own)
#
# Start at boot (as root):
#   install -m 755 t1s-lan-proxy.sh /usr/local/sbin/
#   install -m 644 t1s-lan-proxy.service /etc/systemd/system/
#   systemctl daemon-reload && systemctl enable --now t1s-lan-proxy

set -euo pipefail

WIFI=${WIFI:-wlx5c628b25765e}
USB=${USB:-enx00005e005301}
DHCP_SERVER=${DHCP_SERVER:-10.10.193.1}
PIDDIR=/run/t1s-lan-proxy

# Docker sets the FORWARD policy to DROP; DOCKER-USER is evaluated first
fwd_chain() {
	if iptables -n -L DOCKER-USER >/dev/null 2>&1; then
		echo DOCKER-USER
	else
		echo FORWARD
	fi
}

fwd_rules() {
	local op=$1 chain
	chain=$(fwd_chain)
	iptables "$op" "$chain" -i "$USB" -o "$WIFI" -j ACCEPT
	iptables "$op" "$chain" -i "$WIFI" -o "$USB" -j ACCEPT
}

up() {
	local addr

	if pgrep -f "^parprouted $WIFI $USB" >/dev/null; then
		echo "already up (run: $0 down)"
		exit 1
	fi

	for tool in parprouted dhcp-helper; do
		command -v "$tool" >/dev/null || { echo "missing: apt install $tool"; exit 1; }
	done
	ip link show "$USB" >/dev/null || { echo "$USB not present (UNI Clicker connected?)"; exit 1; }

	addr=$(ip -4 -o addr show dev "$WIFI" | awk '{print $4}' | cut -d/ -f1 | head -1)
	[ -n "$addr" ] || { echo "$WIFI has no IPv4 address"; exit 1; }

	# NetworkManager must not run its own DHCP client on the USB interface
	nmcli device set "$USB" managed no 2>/dev/null || true

	# Same address as Wi-Fi (/32): parprouted needs an address on both sides,
	# and dhcp-helper uses it as the relay address (giaddr)
	ip link set "$USB" up
	ip addr replace "$addr/32" dev "$USB"

	sysctl -q -w net.ipv4.ip_forward=1
	fwd_rules -I

	mkdir -p "$PIDDIR"
	parprouted "$WIFI" "$USB"
	dhcp-helper -s "$DHCP_SERVER" -i "$USB" -r "$PIDDIR/dhcp-helper.pid"

	echo "up: $USB <-> $WIFI ($addr), DHCP relayed to $DHCP_SERVER"
}

down() {
	if [ -f "$PIDDIR/dhcp-helper.pid" ]; then
		kill "$(cat "$PIDDIR/dhcp-helper.pid")" 2>/dev/null || true
		rm -f "$PIDDIR/dhcp-helper.pid"
	fi
	pkill -f "^parprouted $WIFI $USB" 2>/dev/null || true
	# parprouted removes its routes before exiting; wait so that up can follow
	for _ in $(seq 50); do
		pgrep -f "^parprouted $WIFI $USB" >/dev/null || break
		sleep 0.1
	done
	fwd_rules -D 2>/dev/null || true
	ip -4 addr flush dev "$USB" 2>/dev/null || true
	echo "down"
}

# Keep the proxy up while the UNI Clicker is present: the interface is
# recreated (and the address lost) on every board reset or reflash
run() {
	trap 'down; exit 0' INT TERM

	while true; do
		if ip link show "$USB" >/dev/null 2>&1; then
			if ! pgrep -f "^parprouted $WIFI $USB" >/dev/null ||
			   [ -z "$(ip -4 -o addr show dev "$USB")" ]; then
				down >/dev/null
				(up) || true
			fi
		elif pgrep -f "^parprouted $WIFI $USB" >/dev/null; then
			down
		fi
		sleep 2
	done
}

status() {
	ip -br -4 addr show dev "$USB" || true
	pgrep -a -x parprouted || echo "parprouted not running"
	pgrep -a -x dhcp-helper || echo "dhcp-helper not running"
	echo "host routes via $USB:"
	ip -4 route show dev "$USB" | grep -v "^$" || true
	ip neigh show dev "$USB" || true
}

case "${1:-}" in
up) up ;;
down) down ;;
run) run ;;
status) status ;;
*) echo "usage: $0 up|down|run|status"; exit 2 ;;
esac
