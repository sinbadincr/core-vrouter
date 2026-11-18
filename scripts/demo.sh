#!/usr/bin/env bash
# Scripted demo of minivr: lab, forwarding, live route change, malformed traffic, counters.
# Needs root. Record with:  sudo asciinema rec -c scripts/demo.sh docs/demo.cast
set -u
cd "$(dirname "$0")/.."
BUILD="${MINIVR_BUILD:-debug}"
export MINIVR_SOCK=/run/mvr-cn1/minivr.sock
VRCTL="build/$BUILD/vrctl"
[ "$(id -u)" -eq 0 ] || { echo "run as root: sudo scripts/demo.sh" >&2; exit 2; }

say() { printf '\n# %s\n' "$*"; }
run() { printf '$ %s\n' "$*"; "$@"; }
vm() { local ns=$1; shift; printf '$ [%s] %s\n' "$ns" "$*"; ip netns exec "$ns" "$@"; }

trap 'lab/lab.sh down >/dev/null 2>&1' EXIT
lab/lab.sh down >/dev/null 2>&1

say "1. one command builds the lab: one compute node, three VMs"
run lab/lab.sh up
say "2. start the forwarder (lab/cn1.conf); it drops to nobody once it is set up"
run lab/lab.sh run
printf '$ grep -E "^(Uid|CapEff|NoNewPrivs)" /proc/<minivrd pid>/status\n'
grep -E '^(Uid|CapEff|NoNewPrivs)' "/proc/$(cat build/lab/minivrd.pid)/status"

say "3. VMs reach each other through the forwarder (TTL 63 = one hop)"
vm mvr-vm1 ping -c3 -W1 10.0.2.13
vm mvr-vm1 ping -c2 -W1 10.0.1.12
say "   the gateway answers for itself, and ARP is answered by the forwarder"
vm mvr-vm1 ping -c1 -W1 10.0.1.1

say "4. inspect state"
run "$VRCTL" route show
run "$VRCTL" neigh show
run "$VRCTL" port show

say "5. change routes while traffic flows (no restart)"
vm mvr-vm3 ip addr add 10.0.9.1/32 dev lo
run lab/lab.sh run --config lab/cn1-nobind.conf
run "$VRCTL" route add 10.0.9.0/24 dev p3 via 10.0.2.13
vm mvr-vm1 ping -c1 -W2 10.0.9.1
run "$VRCTL" route del 10.0.9.0/24
vm mvr-vm1 ping -c1 -W1 10.0.9.1
run "$VRCTL" route add 10.0.9.0/24 dev p3 via 10.0.2.13
vm mvr-vm1 ping -c1 -W1 10.0.9.1

say "6. malformed traffic: every frame lands in a named drop reason"
run "$VRCTL" stats clear
run .venv/bin/python tests/e2e/corpus.py --ns mvr-vm1 --count 3
printf '$ %s -j stats show | jq .data.drops\n' "$VRCTL"
"$VRCTL" -j stats show | jq -c '.data.drops | {total, reasons: (.reasons | with_entries(select(.value > 0)))}'

say "7. the forwarder is still up and still forwarding"
vm mvr-vm1 ping -c1 -W1 10.0.9.1
run "$VRCTL" version

say "8. one command removes everything; the host network is untouched"
run lab/lab.sh down
