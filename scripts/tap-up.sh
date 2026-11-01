#!/usr/bin/env bash
# Create the host end of the experiment link: a tap device that run-qemu.sh wires
# to the guest's legacy virtio-net NIC. Needs root; run once per host boot:
#
#   sudo scripts/tap-up.sh          create skiff0 (owned by you) with 192.168.77.1/24
#   sudo scripts/tap-up.sh --down   delete it
#
# A tap is a fake NIC on the host whose "cable" is a file QEMU holds open: every
# frame the guest's NIC sends comes out of skiff0, and every frame the host sends
# to skiff0 arrives at the guest's NIC. So host ping/tcpdump see exactly what the
# driver put on the wire.
set -euo pipefail

TAP=skiff0
HOST_IP=192.168.77.1/24
OWNER=${SUDO_USER:-$USER}

[[ $EUID -eq 0 ]] || { echo "needs root: sudo $0 $*" >&2; exit 1; }

if [[ ${1:-} == --down ]]; then
	ip link del "$TAP"
	echo "deleted $TAP"
	exit 0
fi

if ip link show "$TAP" &>/dev/null; then
	echo "$TAP already exists"
else
	# user: lets QEMU, running as you (not root), attach to the tap
	ip tuntap add dev "$TAP" mode tap user "$OWNER"
	ip addr add "$HOST_IP" dev "$TAP"
	ip link set "$TAP" up
	echo "created $TAP ($HOST_IP), owner $OWNER"
fi
ip -br addr show "$TAP"
