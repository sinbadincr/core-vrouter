# Incident 0001: forced crash drill

| | |
|---|---|
| Date | 2026-11-18 |
| Kind | **Drill**: a deliberate crash to prove the post-mortem tooling works (SC-009) |
| Severity | none (lab only) |
| Build | `debug` preset (`-O0 -g3`, `MINIVR_DEBUG`, linked with `-rdynamic`) |
| Status | closed: the drill met SC-009; no product bug found |
| Regression test | `tests/e2e/test_crash.py` |

This file is also the **template** for real post-mortems: copy it to `incidents/NNNN-short-name.md`
and replace every section. Keep the headings.

## Symptom

`minivrd` stopped answering `vrctl`; the client printed `vrctl: no response from forwarder` and
exited 3. `ip netns exec mvr-vm1 ping 10.0.2.13` stopped being answered. (Here the symptom was
caused on purpose with `vrctl debug crash`.)

## Timeline

| Time (UTC) | Event |
|---|---|
| 19:12:40 | `minivrd` started (`ports=3`), log `build/lab/minivrd.log` |
| 19:12:40 | `vrctl debug crash` sent over the control socket |
| 19:12:40 | `minivrd` received SIGSEGV; the handler wrote a backtrace to its log, then re-raised the signal with the default action; the kernel wrote `/var/tmp/minivr/core.<pid>` (15 MB) |
| 19:12:47 | the next test run restarted the forwarder; the lab was never left in a bad state |

## Reproduction

```bash
cmake --preset debug && cmake --build --preset debug
sudo make lab && sudo make lab-run
export MINIVR_SOCK=/run/mvr-cn1/minivr.sock
sudo -E build/debug/vrctl debug crash            # exit 3, the daemon is gone
ls -l /var/tmp/minivr/                            # core (or core.<pid>)
```

The core pattern on the host must be a relative name (`cat /proc/sys/kernel/core_pattern` ->
`core`). With `systemd-coredump` use `coredumpctl gdb minivrd` instead.

## Logs

`build/lab/minivrd.log` (the `FATAL` block is written by the signal handler, `src/daemon/crash.c`):

```
FATAL: fatal signal, backtrace follows
build/debug/minivrd(+0xd11e) [0x558ff075511e]
/lib/x86_64-linux-gnu/libc.so.6(+0x3fd70) [0x7f022c2e8d70]
build/debug/minivrd(cmd_debug_crash+0x10) [0x558ff07537af]
build/debug/minivrd(+0xb7ee) [0x558ff07537ee]
build/debug/minivrd(cmd_exec+0x25f) [0x558ff0753d35]
build/debug/minivrd(+0xc30c) [0x558ff075430c]
build/debug/minivrd(+0xc5cd) [0x558ff07545cd]
build/debug/minivrd(ev_wait+0xc4) [0x558ff07554f3]
build/debug/minivrd(main+0x544) [0x558ff0756177]
```

Static functions show only as `+0x...` here (the dynamic symbol table has the exported ones);
the core gives the full picture.

## Backtrace from the core

```bash
gdb -batch -ex bt build/debug/minivrd /var/tmp/minivr/core
```

```
Program terminated with signal SIGSEGV, Segmentation fault.
#0  cmd_debug_crash () at src/ctl/cmd.c:470
470	    *p = 1;
#1  0x... in cmd_debug_crash_handler (c=..., argc=2, argv=...) at src/ctl/cmd.c:477
#2  0x... in cmd_exec (line=0x... "debug crash", where=CMD_CLI, out=..., err=...) at src/ctl/cmd.c:598
#3  0x... in handle_request (c=..., p=... "debug crash", len=11) at src/ctl/ctl_server.c:83
#4  0x... in client_cb (s=..., events=1) at src/ctl/ctl_server.c:133
#5  0x... in ev_wait (timeout_ms=1000) at src/daemon/evloop.c:49
#6  0x... in main (argc=5, argv=...) at src/daemon/main.c:165
```

Interactive follow-up: `frame 0`, `info locals`, `print *c`, `info registers`.

## Root cause

`cmd_debug_crash()` writes through a NULL pointer on purpose. It is compiled only into debug
builds (`MINIVR_DEBUG`) and reachable only through the control socket, which is `0600` and
`SO_PEERCRED`-checked. For a real incident, state here which invariant was broken and why the
existing tests did not catch it.

## Fix

None needed. For a real incident: the commit that fixes it (`fix:` Conventional Commit).

## Regression test

`tests/e2e/test_crash.py::test_forced_crash_produces_a_symbolized_core_and_log` asserts that a
core appears in `/var/tmp/minivr/`, that `gdb -batch -ex bt` names `cmd_debug_crash` and `main`,
and that the log contains the `FATAL` block. For a real incident, add a failing-first test for
the bug itself (constitution III).

## What went well / what to improve

- The core landed in `/var/tmp/minivr/` although the daemon had dropped to `nobody`: the
  directory is created and chowned before the drop, and `PR_SET_DUMPABLE` is set again after it.
- Improve: the log backtrace is unsymbolized for static functions; `addr2line -e
  build/debug/minivrd -f <addr>` resolves them.
