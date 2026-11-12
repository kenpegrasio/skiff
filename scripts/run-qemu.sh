#!/usr/bin/env bash
# Boot the Skiff guest: our debug kernel + the Ubuntu disk from make-guest.sh.
#
#   scripts/run-qemu.sh          boot; this terminal becomes the guest's serial console
#   scripts/run-qemu.sh --wait   freeze the CPU before the first instruction, until
#                                gdb attaches and says "continue"
#
# Console keys: Ctrl-a x quits QEMU (like pulling the power), Ctrl-a c toggles the
# QEMU monitor. From other terminals: scripts/ssh-guest.sh, scripts/gdb-guest.sh.
set -euo pipefail

WORK=${WORK:-$HOME/skiff-work}
KBUILD=${KBUILD:-$WORK/build}
GUEST=${GUEST:-$WORK/guest}
MEM=${MEM:-2G}
CPUS=${CPUS:-2}
TAP=${TAP:-skiff0}
EXP_MAC=${EXP_MAC:-52:54:00:5c:1f:01}

[[ -f $GUEST/guest.qcow2 ]] || { echo "no guest disk: run scripts/make-guest.sh first" >&2; exit 1; }
ip link show "$TAP" &>/dev/null || { echo "no tap $TAP: run sudo scripts/tap-up.sh first" >&2; exit 1; }

wait_gdb=()
[[ ${1:-} == --wait ]] && wait_gdb=(-S)

args=(
	# Machine: classic PC (i440FX, plain PCI bus), real CPU via KVM.
	# 2+ CPUs so races and lockdep reports can actually happen.
	-machine pc -accel kvm -cpu host -smp "$CPUS" -m "$MEM"

	# Kernel: boot our bzImage directly; the disk's GRUB and Ubuntu kernel are skipped.
	# root=/dev/vda1: first virtio disk, partition 1. console=ttyS0: log to serial.
	-kernel "$KBUILD/arch/x86/boot/bzImage"
	-append "root=/dev/vda1 rw console=ttyS0"

	# Disks, in this order: vda = guest root (overlay), vdb = cloud-init seed
	-drive "file=$GUEST/guest.qcow2,if=virtio,format=qcow2"
	-drive "file=$GUEST/seed.img,if=virtio,format=raw"

	# Management NIC: e1000 on QEMU's built-in user networking (NAT, no root needed).
	# Host 127.0.0.1:2222 is forwarded to guest port 22 (SSH).
	-netdev user,id=mgmt,hostfwd=tcp:127.0.0.1:2222-:22
	-device e1000,netdev=mgmt

	# Experiment NIC: the card skiff drives. disable-modern=on leaves only the legacy
	# virtio interface: PCI ID 1af4:1000, registers in I/O-port BAR0. Its cable is
	# the host tap from tap-up.sh (script=no: QEMU leaves the tap's setup alone).
	# Listed after the e1000 so the e1000 keeps its PCI slot and interface name.
	-netdev "tap,id=exp,ifname=$TAP,script=no,downscript=no"
	-device "virtio-net-pci,netdev=exp,disable-modern=on,mac=$EXP_MAC"

	# Second experiment NIC (optional): same legacy card, its own MAC.
	# user-net "cable" so no second tap or sudo is needed; restrict=on keeps it
	# off the internet. Probe/remove don't care what the cable is.
	-netdev user,id=exp2,restrict=on
	-device "virtio-net-pci,netdev=exp2,disable-modern=on,mac=52:54:00:5c:1f:02"

	# No window: the guest's serial port is this terminal.
	-nographic

	# gdb stub, reachable from this machine only
	-gdb tcp:127.0.0.1:1234 "${wait_gdb[@]}"
)
exec qemu-system-x86_64 "${args[@]}"
