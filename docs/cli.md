# `vrctl` reference

`vrctl` sends one command to a running `minivrd` over its control socket and prints the result.
The same command grammar, one command per line, is the start-up config file format (`#` starts
a comment).

```
vrctl [-s <socket>] [-j] <command> [args...]
```

| Option | Default | Meaning |
|---|---|---|
| `-s <socket>` | `/run/minivr/minivr.sock`, overridden by `$MINIVR_SOCK` | control socket path |
| `-j` | off | print the raw JSON response instead of a table |

In the lab the socket is `/run/mvr-cn1/minivr.sock`: `export MINIVR_SOCK=/run/mvr-cn1/minivr.sock`.
The socket is mode `0600`, so run `vrctl` as the user that started the forwarder, or as root.

On every invocation `vrctl` first asks the server for its `version` and refuses to continue
(exit 3) if the protocol number is not 1.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | the forwarder rejected the command (`error.code: message` on stderr) |
| 2 | usage error detected locally (unknown command, missing argument) |
| 3 | forwarder unreachable: socket missing, connection refused, permission denied, protocol mismatch |

## Commands

Address syntax: IPv4 dotted quad; prefix `a.b.c.d/len` (host bits must be zero); MAC `xx:xx:xx:xx:xx:xx`.

| Command | Where | Effect |
|---|---|---|
| `port add <name> ifname <if> ip <a.b.c.d/len>` | config only | create a port and its connected route; at most 16 ports |
| `icmp-ratelimit rate <1..100000> burst <1..10000>` | config, CLI | ICMP error limiter applied to every port (defaults 100 / 20) |
| `port show` | CLI | name, ifname, mac, ip/len, mtu, admin, oper, rx/tx packets and bytes, drops |
| `port set <name> admin up\|down` | CLI | change admin state; going down releases held packets |
| `route show` | CLI | prefix, port, gateway, origin, in prefix order |
| `route add <prefix> dev <port> [via <gw>]` | config, CLI | add a static route; `gw` must be inside the egress port's subnet |
| `route del <prefix>` | CLI | delete a static route; `connected` and `host` routes are `immutable` |
| `route lookup <ip>` | CLI | the route the longest-prefix match selects |
| `neigh show` | CLI | ip, mac, port, kind (`static`/`learned`), state, held packets |
| `neigh add <ip> lladdr <mac> dev <port>` | config, CLI | static neighbour; also installs the `/32` host route and binds the port to that VM (anti-spoofing) |
| `neigh del <ip>` | CLI | delete a neighbour (and its host route and binding) |
| `stats show` | CLI | per-port counters, every drop reason, event counters, mbuf gauge |
| `stats clear` | CLI | zero all counters (gauges are untouched) |
| `version` | CLI | protocol, version, build type, uptime |
| `debug crash` | CLI, debug builds | deliberate NULL dereference, for the crash drill |

A failed command never changes forwarder state. A response larger than 65,536 bytes (for
example `route show` with thousands of routes) is reported as `table-full`.

## Error codes

| `error.code` | Meaning |
|---|---|
| `bad-syntax` | unknown command, wrong arguments, value out of range |
| `bad-prefix` | malformed prefix, length > 32, or host bits set |
| `bad-address` | malformed IPv4 address |
| `bad-mac` | malformed MAC address |
| `unknown-port` | no port with that name |
| `exists` | duplicate route / neighbour, or the port already has a static neighbour |
| `not-found` | no such route / neighbour |
| `table-full` | route table (10,000), neighbour table (1,024) or response size exceeded |
| `gw-not-connected` | route gateway outside the egress port's subnet |
| `immutable` | deleting a `connected` or `host` route |
| `config-only` | command only valid in the config file |
| `not-permitted` | peer uid is neither the launching user nor root |
| `busy` | more than 8 concurrent clients |

## JSON shapes (`-j`)

Success is `{"ok": true, "data": {...}}`, failure is `{"ok": false, "error": {"code": ..., "message": ...}}`.
Mutating commands return `"data": {}`.

```json
{"ok": true, "data": {"routes": [
  {"prefix": "10.0.1.0/24", "port": null, "gateway": null, "origin": "connected"},
  {"prefix": "10.0.9.0/24", "port": "p3", "gateway": "10.0.2.13", "origin": "static"}]}}

{"ok": true, "data": {"neighbours": [
  {"ip": "10.0.2.13", "mac": "02:00:00:00:01:03", "port": "p3",
   "kind": "static", "state": "REACHABLE", "held": 0}]}}

{"ok": true, "data": {"ports": [
  {"name": "p1", "ifname": "p1", "mac": "02:00:00:00:00:01", "ip": "10.0.1.1/24", "mtu": 1500,
   "admin": "up", "oper": "up", "rx_pkts": 10, "rx_bytes": 980,
   "tx_pkts": 10, "tx_bytes": 980, "drops": 0}]}}

{"ok": true, "data": {"ports": [{"name": "p1", "rx_pkts": 10, "rx_bytes": 980,
   "tx_pkts": 10, "tx_bytes": 980, "drops": 0}],
  "drops": {"total": 3, "reasons": {"no_route": 2, "source_spoofed": 1}},
  "events": {"icmp_ratelimited": 0, "neighbour_table_full": 0,
             "arp_replies_sent": 0, "icmp_sent": 0},
  "mbuf": {"total": 4096, "in_use": 0}}}

{"ok": true, "data": {"protocol": 1, "version": "0.1.0", "build": "debug", "uptime_s": 12}}
```

Every drop reason is always present in `reasons` (zero if unused; the example abbreviates it),
so `sum(reasons) == total` can be asserted. `mac` in `neigh show` is `null` while a neighbour is
still `INCOMPLETE`. For the reasons see `drop-reasons.md`.

## Human output

```
$ vrctl route show
PREFIX          PORT  GATEWAY     ORIGIN
10.0.1.0/24     -     -           connected
10.0.1.11/32    p1    -           host
10.0.2.0/24     p3    -           connected
10.0.9.0/24     p3    10.0.2.13   static
```
