#!/usr/bin/env bash
# Configure and build the Skiff debug guest kernel.
#
#   scripts/build-kernel.sh             build (configures first if needed)
#   scripts/build-kernel.sh --reconfig  start the .config over from defconfig
#   JOBS=8 scripts/build-kernel.sh      fewer parallel jobs if RAM runs out
#
# The kernel source stays untouched: all output goes to $KBUILD (make O=...).
# That build directory is also the KDIR you build module/ against later.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
KSRC=${KSRC:-$HOME/skiff-work/linux-6.18.58}
KBUILD=${KBUILD:-$HOME/skiff-work/build}
FRAGMENT=$REPO/scripts/kernel/skiff-debug.config
JOBS=${JOBS:-$(nproc)}

kmake() { make -C "$KSRC" O="$KBUILD" "$@"; }

# Every line of the fragment must survive into the final .config.
check_fragment() {
	local bad=0 line sym
	while IFS= read -r line; do
		case $line in
		CONFIG_*=*)
			if ! grep -qxF "$line" "$KBUILD/.config"; then
				echo "  not applied: $line"; bad=1
			fi ;;
		"# CONFIG_"*" is not set")
			sym=${line#\# }; sym=${sym%% *}
			if grep -q "^$sym=" "$KBUILD/.config"; then
				echo "  still enabled: $sym"; bad=1
			fi ;;
		esac
	done < "$FRAGMENT"
	return $bad
}

[[ -f $KSRC/Makefile ]] || { echo "kernel source not found: $KSRC" >&2; exit 1; }
[[ ${1:-} == --reconfig ]] && rm -f "$KBUILD/.config"

# Configure from scratch when there is no .config yet or the fragment changed.
if [[ ! -f $KBUILD/.config || $FRAGMENT -nt $KBUILD/.config ]]; then
	mkdir -p "$KBUILD"
	rm -f "$KBUILD/.config"
	kmake x86_64_defconfig kvm_guest.config
	"$KSRC/scripts/kconfig/merge_config.sh" -m -O "$KBUILD" "$KBUILD/.config" "$FRAGMENT"
	kmake olddefconfig
fi

if ! check_fragment; then
	echo "Kconfig dropped options above (unmet dependency?). Fix $FRAGMENT and rerun." >&2
	exit 1
fi

kmake -j"$JOBS"

echo
echo "kernel:  $KBUILD/arch/x86/boot/bzImage"
echo "symbols: $KBUILD/vmlinux"
echo "gdb:     $KBUILD/vmlinux-gdb.py"
