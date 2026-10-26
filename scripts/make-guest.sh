#!/usr/bin/env bash
# Create the guest's disk and cloud-init seed. Run once.
#
#   scripts/make-guest.sh           create the guest
#   scripts/make-guest.sh --force   wipe it and start over (the "factory reset")
#
# Everything lands in $GUEST (default ~/skiff-work/guest):
#   guest.qcow2   overlay on the downloaded cloud image. All guest writes go here;
#                 the downloaded image itself is never modified.
#   seed.img      tiny disk cloud-init reads on first boot: user, password, SSH key
#   id_ed25519    SSH key used only for this guest
set -euo pipefail

WORK=${WORK:-$HOME/skiff-work}
BASE=${BASE:-$WORK/images/noble-server-cloudimg-amd64.img}
GUEST=${GUEST:-$WORK/guest}
GUEST_USER=${GUEST_USER:-skiff}
DISK_SIZE=${DISK_SIZE:-20G}

[[ -f $BASE ]] || { echo "cloud image not found: $BASE" >&2; exit 1; }
if [[ -e $GUEST/guest.qcow2 ]]; then
	[[ ${1:-} == --force ]] || { echo "guest exists; use --force to wipe it" >&2; exit 1; }
	rm -f "$GUEST/guest.qcow2" "$GUEST/known_hosts"
fi
mkdir -p "$GUEST"

[[ -f $GUEST/id_ed25519 ]] || ssh-keygen -q -t ed25519 -N '' -C skiff-guest -f "$GUEST/id_ed25519"

# Copy-on-write overlay: reads fall through to $BASE, writes stay in guest.qcow2.
# The virtual size is bigger than the image; cloud-init grows the root partition.
qemu-img create -q -f qcow2 -F qcow2 -b "$BASE" "$GUEST/guest.qcow2" "$DISK_SIZE"

cat > "$GUEST/user-data" <<EOF
#cloud-config
hostname: skiff-guest
users:
  - name: $GUEST_USER
    shell: /bin/bash
    sudo: ALL=(ALL) NOPASSWD:ALL
    lock_passwd: false
    ssh_authorized_keys:
      - $(cat "$GUEST/id_ed25519.pub")
# Password for the serial console only (SSH accepts the key, not passwords)
chpasswd:
  expire: false
  users:
    - {name: $GUEST_USER, password: $GUEST_USER, type: text}
ssh_pwauth: false
EOF
cloud-localds "$GUEST/seed.img" "$GUEST/user-data"

echo "guest ready in $GUEST (console login: $GUEST_USER / $GUEST_USER)"
