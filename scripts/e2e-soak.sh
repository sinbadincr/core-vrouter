#!/usr/bin/env bash
# Run `make e2e` N times (default 10) and report how many passed (SC-008). Needs root.
set -u
cd "$(dirname "$0")/.."
N="${1:-10}"
pass=0
for i in $(seq 1 "$N"); do
    SECONDS=0
    if make -s e2e >"build/e2e-soak-$i.log" 2>&1; then
        pass=$((pass + 1))
        echo "run $i/$N: PASS (${SECONDS}s)"
    else
        echo "run $i/$N: FAIL (see build/e2e-soak-$i.log)"
    fi
done
echo "$pass/$N passed"
[ "$pass" -eq "$N" ]
