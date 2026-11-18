"""US4-7 / FR-022a / quickstart V18: the running forwarder holds no privileges."""
import os
import re
import subprocess

import pytest

from conftest import BUILD, ROOT, forwarder_pid, lab_sh, ns_exec, sh, vrctl

pytestmark = pytest.mark.skipif(
    BUILD.startswith("sandbox"), reason="sandbox build stubs privdrop (user namespace cannot map `nobody`)")


def status(pid):
    fields = {}
    for line in open(f"/proc/{pid}/status"):
        k, _, v = line.partition(":")
        fields[k] = v.split()
    return fields


def nobody_uid():
    import pwd
    return str(pwd.getpwnam("nobody").pw_uid)


def test_v18_forwarder_runs_as_nobody_with_no_capabilities(forwarder):
    st = status(forwarder_pid())
    assert st["Uid"] == [nobody_uid()] * 4
    for cap in ("CapEff", "CapPrm", "CapAmb", "CapInh", "CapBnd"):
        if cap in ("CapEff", "CapPrm", "CapAmb"):
            assert st[cap] == ["0000000000000000"], (cap, st[cap])
    assert st["NoNewPrivs"] == ["1"]
    log = (ROOT / "build" / "lab" / "minivrd.log").read_text()
    assert 'msg="privileges dropped"' in log


def test_forwarding_and_control_still_work_after_the_drop(forwarder):
    r = ns_exec("mvr-vm1", "ping", "-c", "3", "-W", "1", "10.0.2.13", check=False)
    assert " 0% packet loss" in r.stdout
    assert vrctl("version")["ok"]


def test_unknown_user_is_refused_before_any_traffic(lab):
    env = dict(os.environ, MINIVR_ARGS="--user no_such_user_xyz")
    r = subprocess.run([str(ROOT / "lab" / "lab.sh"), "run"], env=env, capture_output=True, text=True)
    assert r.returncode != 0
    pid = forwarder_pid()
    assert pid is None or not os.path.exists(f"/proc/{pid}")
    assert "unknown user" in (ROOT / "build" / "lab" / "minivrd.log").read_text()
