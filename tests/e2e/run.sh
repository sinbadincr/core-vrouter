#!/usr/bin/env bash
# One-command end-to-end verification (US5): prerequisites -> build -> venv -> pytest -> cleanup.
# Prints PASS/FAIL per check, always tears the lab down, keeps artifacts on failure.
set -u
cd "$(dirname "$0")/../.."
ROOT=$PWD
BUILD="${MINIVR_BUILD:-debug}"
OUT="$ROOT/build/e2e"
STAMP=$(date +%Y%m%d-%H%M%S)
export MINIVR_E2E_ARTIFACTS="$OUT/artifacts/$STAMP"
export MINIVR_BUILD="$BUILD"

cleanup() { "$ROOT/lab/lab.sh" down >/dev/null 2>&1 || true; }
trap cleanup EXIT INT TERM

[ "$(id -u)" -eq 0 ] || { echo "e2e needs root: run 'sudo make e2e'" >&2; exit 2; }
scripts/check-prereqs.sh --e2e || exit 2

mkdir -p "$OUT"
case "$BUILD" in
    sandbox*) ;; # hand-built binaries in build/<name>/ (no preset)
    *)
        { cmake --preset "$BUILD" && cmake --build --preset "$BUILD" -j; } >"$OUT/build.log" 2>&1 \
            || { echo "build failed, see $OUT/build.log" >&2; exit 2; }
        ;;
esac

if [ ! -x .venv/bin/pytest ]; then
    if ! python3 -m venv .venv >/dev/null 2>&1; then
        rm -rf .venv
        command -v uv >/dev/null 2>&1 || { echo "cannot create .venv: install python3-venv (apt) or uv" >&2; exit 2; }
        uv venv .venv -q
        uv pip install -q -p .venv/bin/python -r tests/e2e/requirements.txt || exit 2
    else
        .venv/bin/pip install -q -r tests/e2e/requirements.txt || exit 2
    fi
fi

cleanup
rm -f "$OUT/report.xml"
.venv/bin/pytest -v --junitxml="$OUT/report.xml" -o junit_family=xunit2 tests/e2e
rc=$?

echo
.venv/bin/python - "$OUT/report.xml" <<'PY'
import sys, xml.etree.ElementTree as ET
root = ET.parse(sys.argv[1]).getroot()
for tc in root.iter("testcase"):
    state = "PASS"
    if tc.find("failure") is not None or tc.find("error") is not None:
        state = "FAIL"
    elif tc.find("skipped") is not None:
        state = "SKIP"
    print(f"{state}  {tc.get('classname', '').split('.')[-1]}::{tc.get('name')}")
PY
if [ $rc -ne 0 ]; then
    echo "e2e FAILED (artifacts: $MINIVR_E2E_ARTIFACTS)" >&2
else
    echo "e2e PASSED"
fi
exit $rc
