#!/usr/bin/env bash
# Verify the tools needed to build, test and run the lab.
set -u
need="gcc clang cmake gdb ip arping tcpdump iperf3 socat jq python3"
# `--e2e`: only what `make e2e` itself needs (the suite crafts packets with scapy)
[ "${1:-}" = "--e2e" ] && need="gcc cmake ip socat jq python3"
missing=""
for t in $need; do
    command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
done
if [ -n "$missing" ]; then
    echo "missing tools:$missing" >&2
    echo "install with:" >&2
    echo "  sudo apt install build-essential cmake clang gdb iproute2 iputils-ping arping \\" >&2
    echo "                   tcpdump iperf3 socat jq python3-venv" >&2
    exit 1
fi
echo "prerequisites OK"
