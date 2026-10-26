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

[[ -f $GUEST/guest.qcow2 ]] || { echo "no guest disk: run scripts/make-guest.sh first" >&2; exit 1; }

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

	# No window: the guest's serial port is this terminal.
	-nographic

	# gdb stub, reachable from this machine only
	-gdb tcp:127.0.0.1:1234 "${wait_gdb[@]}"
)
exec qemu-system-x86_64 "${args[@]}"
