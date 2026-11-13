#!/usr/bin/env bash
# Hand the experiment NIC to skiff, or give it back. Runs the sysfs writes in
# the guest over SSH (guest sudo is passwordless). Load skiff.ko first.
#   scripts/bind-nic.sh             unbind 0000:00:04.0, override=skiff, re-probe
#   scripts/bind-nic.sh --release   unbind, clear the override, re-probe
#                                   (virtio-pci takes the card back)
#   DEV=0000:00:04.0 DRIVER=skiff   defaults; override either one
set -euo pipefail

DEV=${DEV:-0000:00:04.0}
DRIVER=${DRIVER:-skiff}

case "${1:-}" in
"")        want=$DRIVER ;;
--release) want="" ;;
*)         echo "usage: $0 [--release]" >&2; exit 2 ;;
esac

# The script body goes over stdin. ssh joins arguments into one string, so an
# empty $want arrives as a missing $2; the remote side reads it as ${2:-}.
"$(dirname "$0")/ssh-guest.sh" sudo sh -s -- "$DEV" "$want" <<'EOF'
set -eu
dev=$1
want=${2:-}
sys=/sys/bus/pci/devices/$dev

[ -e "$sys" ] || { echo "no PCI device $dev in the guest" >&2; exit 1; }
if [ -n "$want" ] && [ ! -d "/sys/bus/pci/drivers/$want" ]; then
	echo "note: no PCI driver named '$want' is loaded right now" >&2
fi

# 1. unbind from whichever driver holds it now (virtio-pci, or skiff)
if [ -e "$sys/driver" ]; then
	echo "$dev" > "$sys/driver/unbind"
fi
# 2. override: only a driver with this name may take the card ("\n" clears it)
echo "$want" > "$sys/driver_override"
# 3. re-probe: ask the PCI core to look for a driver for the card now
echo "$dev" > /sys/bus/pci/drivers_probe

drv=none
[ -e "$sys/driver" ] && drv=$(basename "$(readlink "$sys/driver")")
echo "$dev: driver_override=$(cat "$sys/driver_override"), driver in use=$drv"
EOF
