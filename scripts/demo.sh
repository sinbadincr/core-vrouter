#!/usr/bin/env bash
# Scripted demo of minivr on KVM guests: lab, forwarding, drops, live route change, counters.
# Needs sudo from your own user (QEMU never runs as root).
# Record with:  sudo asciinema rec --overwrite -c scripts/demo.sh docs/demo.cast
set -u
cd "$(dirname "$0")/.."
BUILD="${MINIVR_BUILD:-debug}"
export MINIVR_SOCK=/run/mvr-cn1/minivr.sock
VRCTL="build/$BUILD/vrctl"
[ "$(id -u)" -eq 0 ] && [ -n "${SUDO_UID:-}" ] && [ "$SUDO_UID" != 0 ] || {
    echo "run: sudo scripts/demo.sh (from your own user)" >&2; exit 2; }

say() { printf '\n# %s\n' "$*"; }
run() { printf '$ %s\n' "$*"; "$@"; }
vm() { local g=$1; shift; printf '$ [%s] %s\n' "$g" "$*"; lab/lab.sh ssh "$g" "$@"; }

trap 'lab/lab.sh down >/dev/null 2>&1' EXIT
lab/lab.sh down >/dev/null 2>&1

say "1. one command: three KVM guests on TAP ports p1-p3"
run lab/lab.sh up || exit $?
say "2. the forwarder (lab/cn1.conf); it drops to nobody once it is set up"
run lab/lab.sh run || exit $?
run lab/lab.sh status

say "3. guests reach each other through the forwarder (TTL 63 = one hop)"
vm vm1 sudo ping -c3 -W1 10.0.2.13
vm vm3 sudo ping -c3 -W1 10.0.1.12

say "4. TTL 1 expires at the forwarder; an unknown network is unreachable"
vm vm1 ping -c1 -t1 -W2 10.0.2.13
vm vm1 ping -c1 -W2 10.9.9.9

say "5. a spoofed source address is dropped and counted"
vm vm1 sudo ip addr add 10.0.1.12/32 dev eth0
vm vm1 ping -c3 -W1 -I 10.0.1.12 10.0.2.13
vm vm1 sudo ip addr del 10.0.1.12/32 dev eth0
printf '$ %s -j stats show | jq .data.drops.reasons.source_spoofed\n' "$VRCTL"
"$VRCTL" -j stats show | jq .data.drops.reasons.source_spoofed

say "6. change routes while traffic flows (no restart; same forwarder pid)"
printf '$ forwarder pid: %s\n' "$(cat build/lab/minivrd.pid)"
vm vm3 sudo ip addr add 10.0.9.1/32 dev lo
run "$VRCTL" route add 10.0.9.0/24 dev p3 via 10.0.2.13
vm vm1 ping -c1 -W2 10.0.9.1
run "$VRCTL" route del 10.0.9.0/24
vm vm1 ping -c1 -W1 10.0.9.1
run "$VRCTL" route add 10.0.9.0/24 dev p3 via 10.0.2.13
vm vm1 ping -c1 -W1 10.0.9.1
printf '$ forwarder pid: %s\n' "$(cat build/lab/minivrd.pid)"

say "7. inspect state"
run "$VRCTL" route show
run "$VRCTL" neigh show
run "$VRCTL" port show
run "$VRCTL" stats show

say "8. one command removes everything; the host network is untouched"
run lab/lab.sh down
