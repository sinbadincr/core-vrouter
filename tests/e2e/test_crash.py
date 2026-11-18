"""US4-4 / SC-009: a forced crash leaves a core and a log that point at the faulting function."""
import glob
import os
import subprocess
import time

import pytest

from conftest import BUILD, ROOT, forwarder_pid, sh, vrctl_raw

pytestmark = pytest.mark.skipif(BUILD not in ("debug", "sandbox"),
                                reason="needs a plain debug build (vrctl debug crash, symbols); "
                                       "ASan replaces the crash handler and suppresses core dumps")

DUMP_DIR = "/var/tmp/minivr"


def cores():
    return set(glob.glob(f"{DUMP_DIR}/core*"))


def test_forced_crash_produces_a_symbolized_core_and_log(forwarder):
    pattern = open("/proc/sys/kernel/core_pattern").read().strip()
    if pattern.startswith("|") or "/" in pattern:
        pytest.skip(f"core_pattern is '{pattern}'; this test expects a relative 'core' pattern "
                    "(see incidents/0001-forced-crash-drill.md for coredumpctl)")
    for c in cores():
        os.unlink(c)
    pid = forwarder_pid()
    r = vrctl_raw("debug", "crash")
    assert r.returncode == 3  # the daemon died mid-request

    end = time.monotonic() + 10
    while time.monotonic() < end and (os.path.exists(f"/proc/{pid}") or not cores()):
        time.sleep(0.1)
    found = sorted(cores())
    assert found, f"no core file in {DUMP_DIR}"
    time.sleep(0.5)  # let the kernel finish writing

    bt = sh("gdb", "-batch", "-ex", "bt", str(ROOT / "build" / BUILD / "minivrd"), found[0],
            timeout=120).stdout
    assert "cmd_debug_crash" in bt and "main" in bt, bt

    log = (ROOT / "build" / "lab" / "minivrd.log").read_text()
    assert "FATAL" in log and "cmd_debug_crash" in log, log[-2000:]
    for c in cores():  # a drill core is ~15 MB: do not leave it behind
        os.unlink(c)
