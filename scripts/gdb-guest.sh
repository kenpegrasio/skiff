#!/usr/bin/env bash
# Attach gdb to the guest kernel through QEMU's gdb stub (see run-qemu.sh).
# Attaching pauses the whole VM; "continue" resumes it.
set -euo pipefail

KBUILD=${KBUILD:-$HOME/skiff-work/build}

# gdb only auto-loads vmlinux-gdb.py (the lx-* commands) from trusted paths.
exec gdb -q \
	-iex "add-auto-load-safe-path $KBUILD" \
	-ex "target remote 127.0.0.1:1234" \
	"$@" "$KBUILD/vmlinux"
