#!/usr/bin/env bash
# SSH into the running guest. Extra arguments run as a command:
#   scripts/ssh-guest.sh              interactive shell
#   scripts/ssh-guest.sh uname -r     run one command
set -euo pipefail

GUEST=${GUEST:-${WORK:-$HOME/skiff-work}/guest}

# A guest-only key and known_hosts file: make-guest.sh --force creates new host
# keys, and this keeps that from clashing with ~/.ssh/known_hosts.
exec ssh -p 2222 -i "$GUEST/id_ed25519" -o IdentitiesOnly=yes \
	-o UserKnownHostsFile="$GUEST/known_hosts" -o StrictHostKeyChecking=accept-new \
	"${GUEST_USER:-skiff}@127.0.0.1" "$@"
