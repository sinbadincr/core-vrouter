#include "minivr/cmd.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minivr/clock.h"
#include "minivr/mbuf.h"
#include "minivr/neigh.h"
#include "minivr/port.h"
#include "minivr/ratelimit.h"
#include "minivr/route.h"
#include "minivr/stats.h"

#define MINIVR_VERSION "0.1.0"
#define PROTOCOL_VERSION 1

static uint64_t start_ns;

void cmd_init(void) {
    start_ns = clock_now_ns();
}

int cmd_fail(struct cmd_err *err, const char *code, const char *fmt, ...) {
    err->code = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof err->message, fmt, ap);
    va_end(ap);
    return -1;
}

/* ---- tokenizer and typed parsers ---- */

int cmd_tokenize(char *line, char **argv, int max) {
    int n = 0;
    char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            return n;
        if (n == max)
            return -1;
        argv[n++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (!*p)
            return n;
        *p++ = 0;
    }
}

/* Parses a decimal in [0, max] consuming only digits; returns pointer after it or NULL. */
static const char *scan_dec(const char *s, unsigned max, unsigned *out) {
    if (*s < '0' || *s > '9')
        return NULL;
    unsigned long v = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (unsigned long)(*s - '0');
        if (++digits > 10 || v > max)
            return NULL;
        s++;
    }
    *out = (unsigned)v;
    return s;
}

static int parse_quad(const char *s, const char **end, uint32_t *ip) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        unsigned o;
        const char *e = scan_dec(s, 255, &o);
        if (!e || e - s > 3)
            return -1;
        v = (v << 8) | o;
        s = e;
        if (i < 3) {
            if (*s != '.')
                return -1;
            s++;
        }
    }
    *ip = v;
    *end = s;
    return 0;
}

const char *cmd_parse_ipv4(const char *s, uint32_t *ip) {
    const char *end;
    if (parse_quad(s, &end, ip) != 0 || *end)
        return "bad-address";
    return NULL;
}

static const char *parse_cidr(const char *s, uint32_t *ip, uint8_t *len, int allow_host_bits) {
    const char *end;
    uint32_t a;
    unsigned l;
    if (parse_quad(s, &end, &a) != 0 || *end != '/')
        return "bad-prefix";
    const char *e2 = scan_dec(end + 1, 32, &l);
    if (!e2 || *e2)
        return "bad-prefix";
    uint32_t mask = l == 0 ? 0 : ~0U << (32 - l);
    if (!allow_host_bits && (a & ~mask))
        return "bad-prefix";
    *ip = a;
    *len = (uint8_t)l;
    return NULL;
}

const char *cmd_parse_prefix(const char *s, uint32_t *ip, uint8_t *len) {
    return parse_cidr(s, ip, len, 0);
}

const char *cmd_parse_mac(const char *s, uint8_t mac[6]) {
    if (strlen(s) != 17)
        return "bad-mac";
    for (int i = 0; i < 6; i++) {
        unsigned v = 0;
        for (int j = 0; j < 2; j++) {
            char c = s[i * 3 + j];
            unsigned d;
            if (c >= '0' && c <= '9')
                d = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f')
                d = (unsigned)(c - 'a') + 10;
            else if (c >= 'A' && c <= 'F')
                d = (unsigned)(c - 'A') + 10;
            else
                return "bad-mac";
            v = v * 16 + d;
        }
        if (i < 5 && s[i * 3 + 2] != ':')
            return "bad-mac";
        mac[i] = (uint8_t)v;
    }
    return NULL;
}

const char *cmd_parse_uint(const char *s, uint64_t min, uint64_t max, uint64_t *out) {
    if (*s < '0' || *s > '9')
        return "bad-syntax";
    uint64_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return "bad-syntax";
        unsigned d = (unsigned)(*s - '0');
        if (v > (UINT64_MAX - d) / 10)
            return "bad-syntax";
        v = v * 10 + d;
    }
    if (v < min || v > max)
        return "bad-syntax";
    *out = v;
    return NULL;
}

/* ---- handlers ---- */

struct cmd_ctx {
    struct jw *out;
    struct cmd_err *err;
};

typedef int (*cmd_fn)(struct cmd_ctx *c, int argc, char **argv);

static int usage(struct cmd_ctx *c, const char *u) {
    return cmd_fail(c->err, "bad-syntax", "usage: %s", u);
}

static int cmd_port_add(struct cmd_ctx *c, int argc, char **argv) {
    /* port add <name> ifname <if> ip <a.b.c.d/len> */
    static const char *u = "port add <name> ifname <if> ip <a.b.c.d/len>";
    if (argc != 7 || strcmp(argv[3], "ifname") != 0 || strcmp(argv[5], "ip") != 0)
        return usage(c, u);
    uint32_t ip;
    uint8_t len;
    const char *e = parse_cidr(argv[6], &ip, &len, 1);
    if (e)
        return cmd_fail(c->err, e, "invalid interface address '%s'", argv[6]);
    if (!port_add(argv[2], argv[4], ip, len, c->err))
        return -1;
    return 0;
}

static int cmd_neigh_add(struct cmd_ctx *c, int argc, char **argv) {
    static const char *u = "neigh add <ip> lladdr <mac> dev <port>";
    if (argc != 7 || strcmp(argv[3], "lladdr") != 0 || strcmp(argv[5], "dev") != 0)
        return usage(c, u);
    uint32_t ip;
    uint8_t mac[6];
    const char *e = cmd_parse_ipv4(argv[2], &ip);
    if (e)
        return cmd_fail(c->err, e, "invalid address '%s'", argv[2]);
    if ((e = cmd_parse_mac(argv[4], mac)))
        return cmd_fail(c->err, e, "invalid MAC '%s'", argv[4]);
    struct port *p = port_find(argv[6]);
    if (!p)
        return cmd_fail(c->err, "unknown-port", "no such port '%s'", argv[6]);
    return neigh_add_static(ip, mac, p->id, c->err);
}

static int cmd_route_add(struct cmd_ctx *c, int argc, char **argv) {
    static const char *u = "route add <prefix> dev <port> [via <gw>]";
    if ((argc != 5 && argc != 7) || strcmp(argv[3], "dev") != 0 ||
        (argc == 7 && strcmp(argv[5], "via") != 0))
        return usage(c, u);
    uint32_t prefix, gw = 0;
    uint8_t len;
    const char *e = cmd_parse_prefix(argv[2], &prefix, &len);
    if (e)
        return cmd_fail(c->err, e, "invalid prefix '%s' (host bits must be zero)", argv[2]);
    struct port *p = port_find(argv[4]);
    if (!p)
        return cmd_fail(c->err, "unknown-port", "no such port '%s'", argv[4]);
    if (argc == 7 && (e = cmd_parse_ipv4(argv[6], &gw)))
        return cmd_fail(c->err, e, "invalid gateway '%s'", argv[6]);
    return route_add_static(prefix, len, p->id, gw, c->err);
}

static int cmd_icmp_ratelimit(struct cmd_ctx *c, int argc, char **argv) {
    static const char *u = "icmp-ratelimit rate <1..100000> burst <1..10000>";
    if (argc != 5 || strcmp(argv[1], "rate") != 0 || strcmp(argv[3], "burst") != 0)
        return usage(c, u);
    uint64_t rate, burst;
    const char *e = cmd_parse_uint(argv[2], 1, RL_MAX_RATE, &rate);
    if (e)
        return cmd_fail(c->err, e, "rate must be 1..%d", RL_MAX_RATE);
    if ((e = cmd_parse_uint(argv[4], 1, RL_MAX_BURST, &burst)))
        return cmd_fail(c->err, e, "burst must be 1..%d", RL_MAX_BURST);
    e = ratelimit_configure(rate, burst);
    return e ? cmd_fail(c->err, e, "invalid rate limit") : 0;
}

/* ---- show / set / delete commands (US3) ---- */

static void ip_str(uint32_t ip, char out[16]) {
    snprintf(out, 16, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

static void mac_str(const uint8_t m[6], char out[18]) {
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void prefix_str(uint32_t ip, uint8_t len, char out[20]) {
    char a[16];
    ip_str(ip, a);
    snprintf(out, 20, "%s/%u", a, len);
}

static void json_route(struct jw *w, const char *key, const struct route *r) {
    char b[20], g[16];
    jw_obj_begin(w, key);
    prefix_str(r->prefix, r->len, b);
    jw_str(w, "prefix", b);
    struct port *p = r->port == ROUTE_NO_PORT ? NULL : port_get((uint8_t)r->port);
    if (p)
        jw_str(w, "port", p->name);
    else
        jw_null(w, "port");
    if (r->gateway) {
        ip_str(r->gateway, g);
        jw_str(w, "gateway", g);
    } else {
        jw_null(w, "gateway");
    }
    jw_str(w, "origin", route_origin_name(r->origin));
    jw_obj_end(w);
}

#define SHOW_DEFAULT_LIMIT 500 /* ~45 KB of JSON: always fits one 64 KiB response */

struct page {
    struct jw *w;
    uint64_t index, offset, limit, emitted;
    int more; /* an entry exists beyond this page */
};

/* "[offset <n>] [limit <n>]" starting at argv[2]. */
static int parse_page(struct cmd_ctx *c, int argc, char **argv, const char *usage_line,
                      uint64_t *offset, uint64_t *limit) {
    *offset = 0;
    *limit = SHOW_DEFAULT_LIMIT;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc)
            return usage(c, usage_line);
        uint64_t *dst = strcmp(argv[i], "offset") == 0  ? offset
                        : strcmp(argv[i], "limit") == 0 ? limit
                                                         : NULL;
        if (!dst)
            return usage(c, usage_line);
        const char *e = cmd_parse_uint(argv[i + 1], dst == limit ? 1 : 0, 1000000, dst);
        if (e)
            return cmd_fail(c->err, e, "%s must be a number (limit >= 1)", argv[i]);
    }
    return 0;
}

static void page_header(struct page *pg, uint64_t total) {
    jw_u64(pg->w, "total", total);
    jw_u64(pg->w, "offset", pg->offset);
}

static void page_footer(struct page *pg) {
    /* next_offset is null on the last page: vrctl follows it to print everything */
    if (pg->more)
        jw_u64(pg->w, "next_offset", pg->offset + pg->limit);
    else
        jw_null(pg->w, "next_offset");
}

static void route_show_cb(const struct route *r, void *arg) {
    struct page *pg = arg;
    uint64_t i = pg->index++;
    if (i < pg->offset)
        return;
    if (pg->emitted >= pg->limit) {
        pg->more = 1;
        return;
    }
    pg->emitted++;
    json_route(pg->w, NULL, r);
}

static int cmd_route_show(struct cmd_ctx *c, int argc, char **argv) {
    struct page pg = {.w = c->out};
    if (parse_page(c, argc, argv, "route show [offset <n>] [limit <n>]", &pg.offset, &pg.limit))
        return -1;
    page_header(&pg, route_count());
    jw_arr_begin(c->out, "routes");
    route_walk(route_show_cb, &pg);
    jw_arr_end(c->out);
    page_footer(&pg);
    return 0;
}

static int cmd_route_del(struct cmd_ctx *c, int argc, char **argv) {
    if (argc != 3)
        return usage(c, "route del <prefix>");
    uint32_t prefix;
    uint8_t len;
    const char *e = cmd_parse_prefix(argv[2], &prefix, &len);
    if (e)
        return cmd_fail(c->err, e, "invalid prefix '%s' (host bits must be zero)", argv[2]);
    return route_del(prefix, len, c->err);
}

static int cmd_route_lookup(struct cmd_ctx *c, int argc, char **argv) {
    if (argc != 3)
        return usage(c, "route lookup <ip>");
    uint32_t ip;
    const char *e = cmd_parse_ipv4(argv[2], &ip);
    if (e)
        return cmd_fail(c->err, e, "invalid address '%s'", argv[2]);
    const struct route *r = route_lookup(ip);
    if (!r)
        return cmd_fail(c->err, "not-found", "no route to %s", argv[2]);
    json_route(c->out, "route", r);
    return 0;
}

static int cmd_port_show(struct cmd_ctx *c, int argc, char **argv) {
    (void)argv;
    if (argc != 2)
        return usage(c, "port show");
    jw_arr_begin(c->out, "ports");
    for (unsigned i = 0; i < MAX_PORTS; i++) {
        const struct port *p = port_get((uint8_t)i);
        if (!p)
            continue;
        char mac[18], ip[20];
        mac_str(p->mac, mac);
        prefix_str(p->ip, p->prefix_len, ip);
        const struct port_stats *s = &g_stats.port[i];
        jw_obj_begin(c->out, NULL);
        jw_str(c->out, "name", p->name);
        jw_str(c->out, "ifname", p->ifname);
        jw_str(c->out, "mac", mac);
        jw_str(c->out, "ip", ip);
        jw_u64(c->out, "mtu", p->mtu);
        jw_str(c->out, "admin", p->admin_up ? "up" : "down");
        jw_str(c->out, "oper", p->oper_up ? "up" : "down");
        jw_u64(c->out, "rx_pkts", s->rx_pkts);
        jw_u64(c->out, "rx_bytes", s->rx_bytes);
        jw_u64(c->out, "tx_pkts", s->tx_pkts);
        jw_u64(c->out, "tx_bytes", s->tx_bytes);
        jw_u64(c->out, "drops", s->drops);
        jw_obj_end(c->out);
    }
    jw_arr_end(c->out);
    return 0;
}

static int cmd_port_set(struct cmd_ctx *c, int argc, char **argv) {
    static const char *u = "port set <name> admin up|down";
    if (argc != 5 || strcmp(argv[3], "admin") != 0)
        return usage(c, u);
    struct port *p = port_find(argv[2]);
    if (!p)
        return cmd_fail(c->err, "unknown-port", "no such port '%s'", argv[2]);
    if (strcmp(argv[4], "up") == 0) {
        p->admin_up = true;
    } else if (strcmp(argv[4], "down") == 0) {
        p->admin_up = false;
        neigh_port_down(p->id);
    } else {
        return usage(c, u);
    }
    return 0;
}

struct neigh_row {
    uint32_t ip;
    const struct neigh *n;
};
static struct neigh_row rows[NEIGH_CAPACITY];
static unsigned nrows;

static void neigh_collect(const struct neigh *n, void *arg) {
    (void)arg;
    if (nrows < NEIGH_CAPACITY) {
        rows[nrows].ip = n->ip;
        rows[nrows].n = n;
        nrows++;
    }
}

static int row_cmp(const void *a, const void *b) {
    uint32_t x = ((const struct neigh_row *)a)->ip, y = ((const struct neigh_row *)b)->ip;
    return x < y ? -1 : x > y;
}

static int cmd_neigh_show(struct cmd_ctx *c, int argc, char **argv) {
    struct page pg = {.w = c->out};
    if (parse_page(c, argc, argv, "neigh show [offset <n>] [limit <n>]", &pg.offset, &pg.limit))
        return -1;
    nrows = 0;
    neigh_walk(neigh_collect, NULL);
    qsort(rows, nrows, sizeof rows[0], row_cmp);
    page_header(&pg, nrows);
    pg.more = nrows > pg.offset + pg.limit;
    jw_arr_begin(c->out, "neighbours");
    for (uint64_t i = pg.offset; i < nrows && i < pg.offset + pg.limit; i++) {
        const struct neigh *n = rows[i].n;
        char ip[16], mac[18];
        ip_str(n->ip, ip);
        jw_obj_begin(c->out, NULL);
        jw_str(c->out, "ip", ip);
        if (n->state == NEIGH_REACHABLE) {
            mac_str(n->mac, mac);
            jw_str(c->out, "mac", mac);
        } else {
            jw_null(c->out, "mac");
        }
        const struct port *p = port_get(n->port);
        jw_str(c->out, "port", p ? p->name : "?");
        jw_str(c->out, "kind", n->kind == NEIGH_STATIC ? "static" : "learned");
        jw_str(c->out, "state", n->state == NEIGH_REACHABLE   ? "REACHABLE"
                                : n->state == NEIGH_INCOMPLETE ? "INCOMPLETE"
                                                               : "FAILED");
        jw_u64(c->out, "held", n->nhold);
        jw_obj_end(c->out);
    }
    jw_arr_end(c->out);
    page_footer(&pg);
    return 0;
}

static int cmd_neigh_del(struct cmd_ctx *c, int argc, char **argv) {
    if (argc != 3)
        return usage(c, "neigh del <ip>");
    uint32_t ip;
    const char *e = cmd_parse_ipv4(argv[2], &ip);
    if (e)
        return cmd_fail(c->err, e, "invalid address '%s'", argv[2]);
    return neigh_del(ip, c->err);
}

static int cmd_stats_show(struct cmd_ctx *c, int argc, char **argv) {
    (void)argv;
    if (argc != 2)
        return usage(c, "stats show");
    jw_arr_begin(c->out, "ports");
    for (unsigned i = 0; i < MAX_PORTS; i++) {
        const struct port *p = port_get((uint8_t)i);
        if (!p)
            continue;
        const struct port_stats *s = &g_stats.port[i];
        jw_obj_begin(c->out, NULL);
        jw_str(c->out, "name", p->name);
        jw_u64(c->out, "rx_pkts", s->rx_pkts);
        jw_u64(c->out, "rx_bytes", s->rx_bytes);
        jw_u64(c->out, "tx_pkts", s->tx_pkts);
        jw_u64(c->out, "tx_bytes", s->tx_bytes);
        jw_u64(c->out, "drops", s->drops);
        jw_obj_end(c->out);
    }
    jw_arr_end(c->out);
    stats_json_drops(c->out);
    stats_json_events(c->out);
    jw_obj_begin(c->out, "mbuf");
    jw_u64(c->out, "total", mbuf_pool_total());
    jw_u64(c->out, "in_use", mbuf_in_use());
    jw_obj_end(c->out);
    return 0;
}

static int cmd_stats_clear(struct cmd_ctx *c, int argc, char **argv) {
    (void)argv;
    if (argc != 2)
        return usage(c, "stats clear");
    stats_clear();
    return 0;
}

#ifdef MINIVR_DEBUG
/* Deliberate NULL write for the crash drill (SC-009); kept out of line so it shows in backtraces. */
void cmd_debug_crash(void); /* external linkage so -rdynamic names it in backtraces */
__attribute__((noinline)) void cmd_debug_crash(void) {
    volatile int *volatile p = NULL;
    *p = 1;
}

static int cmd_debug_crash_handler(struct cmd_ctx *c, int argc, char **argv) {
    (void)argv;
    if (argc != 2)
        return usage(c, "debug crash");
    cmd_debug_crash();
    return 0;
}
#endif

static int cmd_version(struct cmd_ctx *c, int argc, char **argv) {
    (void)argv;
    if (argc != 1)
        return usage(c, "version");
    jw_u64(c->out, "protocol", PROTOCOL_VERSION);
    jw_str(c->out, "version", MINIVR_VERSION);
#ifdef MINIVR_DEBUG
    jw_str(c->out, "build", "debug");
#else
    jw_str(c->out, "build", "release");
#endif
    jw_u64(c->out, "uptime_s", (clock_now_ns() - start_ns) / NS_PER_SEC);
    return 0;
}

/* ---- dispatch ---- */

struct cmd_def {
    const char *words; /* leading words, e.g. "port add" */
    cmd_fn fn;
    enum cmd_where where;
    int debug_only;
};

static const struct cmd_def cmds[] = {
    {"port add", cmd_port_add, CMD_CONFIG, 0},
    {"neigh add", cmd_neigh_add, CMD_BOTH, 0},
    {"route add", cmd_route_add, CMD_BOTH, 0},
    {"icmp-ratelimit", cmd_icmp_ratelimit, CMD_BOTH, 0},
    {"port show", cmd_port_show, CMD_CLI, 0},
    {"port set", cmd_port_set, CMD_CLI, 0},
    {"route show", cmd_route_show, CMD_CLI, 0},
    {"route del", cmd_route_del, CMD_CLI, 0},
    {"route lookup", cmd_route_lookup, CMD_CLI, 0},
    {"neigh show", cmd_neigh_show, CMD_CLI, 0},
    {"neigh del", cmd_neigh_del, CMD_CLI, 0},
    {"stats show", cmd_stats_show, CMD_CLI, 0},
    {"stats clear", cmd_stats_clear, CMD_CLI, 0},
#ifdef MINIVR_DEBUG
    {"debug crash", cmd_debug_crash_handler, CMD_CLI, 1},
#endif
    {"version", cmd_version, CMD_CLI, 0},
};

static const struct cmd_def *lookup(int argc, char **argv) {
    for (size_t i = 0; i < sizeof cmds / sizeof *cmds; i++) {
        const char *w = cmds[i].words;
        int k = 0;
        int match = 1;
        while (*w) {
            const char *sp = strchr(w, ' ');
            size_t n = sp ? (size_t)(sp - w) : strlen(w);
            if (k >= argc || strlen(argv[k]) != n || strncmp(argv[k], w, n) != 0) {
                match = 0;
                break;
            }
            k++;
            w += n;
            if (*w == ' ')
                w++;
        }
        if (match)
            return &cmds[i];
    }
    return NULL;
}

static void write_error(struct jw *out, const struct cmd_err *err) {
    jw_reset(out);
    jw_obj_begin(out, NULL);
    jw_bool(out, "ok", false);
    jw_obj_begin(out, "error");
    jw_str(out, "code", err->code);
    jw_str(out, "message", err->message);
    jw_obj_end(out);
    jw_obj_end(out);
}

int cmd_exec(const char *line, enum cmd_where where, struct jw *out, struct cmd_err *err) {
    char buf[1024];
    char *argv[CMD_MAX_ARGS];
    struct cmd_ctx ctx = {out, err};
    err->code = NULL;
    err->message[0] = 0;

    if (strlen(line) >= sizeof buf) {
        cmd_fail(err, "bad-syntax", "command too long");
        goto fail;
    }
    strcpy(buf, line);
    int argc = cmd_tokenize(buf, argv, CMD_MAX_ARGS);
    if (argc <= 0) {
        cmd_fail(err, "bad-syntax", argc == 0 ? "empty command" : "too many arguments");
        goto fail;
    }
    const struct cmd_def *d = lookup(argc, argv);
    if (!d
#ifndef MINIVR_DEBUG
        || d->debug_only
#endif
    ) {
        cmd_fail(err, "bad-syntax", "unknown command '%s'", argv[0]);
        goto fail;
    }
    if (!(d->where & where)) {
        if (where == CMD_CLI)
            cmd_fail(err, "config-only", "'%s' is only valid in the config file", d->words);
        else
            cmd_fail(err, "bad-syntax", "'%s' is not valid in the config file", d->words);
        goto fail;
    }

    jw_reset(out);
    jw_obj_begin(out, NULL);
    jw_bool(out, "ok", true);
    jw_obj_begin(out, "data");
    if (d->fn(&ctx, argc, argv) != 0)
        goto fail;
    jw_obj_end(out);
    jw_obj_end(out);
    return 0;

fail:
    write_error(out, err);
    return -1;
}
