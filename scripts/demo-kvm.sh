#!/usr/bin/env bash
# Scripted demo of KVM lab mode. Needs sudo from your own user (QEMU never runs as root).
# Record with:  sudo asciinema rec -c scripts/demo-kvm.sh docs/demo-kvm.cast
set -u
cd "$(dirname "$0")/.."
BUILD="${MINIVR_BUILD:-debug}"
export MINIVR_SOCK=/run/mvr-cn1/minivr.sock
[ "$(id -u)" -eq 0 ] && [ -n "${SUDO_UID:-}" ] || { echo "run: sudo scripts/demo-kvm.sh" >&2; exit 2; }
say() { printf '\n# %s\n' "$*"; }
run() { printf '$ %s\n' "$*"; "$@"; }
trap 'lab/lab.sh down >/dev/null 2>&1' EXIT
lab/lab.sh down >/dev/null 2>&1

say "1. one command: three KVM guests on TAP ports p1-p3"
run lab/lab.sh up --kvm
say "2. the unchanged forwarder"
run lab/lab.sh run
run lab/lab.sh status
say "3. guest to guest through minivrd (TTL 63 = one hop)"
run lab/lab.sh ssh vm1 sudo ping -c3 -W1 10.0.2.13
say "4. TTL 1 expires at the forwarder; unknown network is unreachable"
run lab/lab.sh ssh vm1 ping -c1 -t1 -W2 10.0.2.13
run lab/lab.sh ssh vm1 ping -c1 -W2 10.9.9.9
say "5. counters"
run "build/$BUILD/vrctl" port show
run "build/$BUILD/vrctl" stats show
say "6. teardown"
run lab/lab.sh down
