/* vrctl: send one command to minivrd over the control socket (contracts/cli.md). */
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "json_in.h"
#include "minivr/frame.h"

#define EXIT_REJECTED 1
#define EXIT_USAGE 2
#define EXIT_UNREACHABLE 3
#define PROTOCOL 1

static const char *const known[] = {
    "port add", "port show", "port set", "route show", "route add", "route del", "route lookup",
    "neigh show", "neigh add", "neigh del", "stats show", "stats clear", "icmp-ratelimit",
    "version", "debug crash",
};

static int is_known(const char *w0, const char *w1) {
    for (size_t i = 0; i < sizeof known / sizeof *known; i++) {
        const char *k = known[i];
        const char *sp = strchr(k, ' ');
        size_t n0 = sp ? (size_t)(sp - k) : strlen(k);
        if (strlen(w0) != n0 || strncmp(w0, k, n0) != 0)
            continue;
        if (!sp)
            return 1;
        if (w1 && strcmp(w1, sp + 1) == 0)
            return 1;
    }
    return 0;
}

static int send_all(int fd, const void *p, size_t n) {
    const char *c = p;
    while (n) {
        ssize_t w = send(fd, c, n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        c += w;
        n -= (size_t)w;
    }
    return 0;
}

static struct frame_rx rx;

/* Returns the JSON payload (malloc'ed, NUL-terminated) of the next response, or NULL. */
static char *recv_frame(int fd, size_t *len_out) {
    frame_rx_init(&rx);
    for (;;) {
        const uint8_t *p;
        size_t len;
        int g = frame_rx_get(&rx, &p, &len);
        if (g < 0)
            return NULL;
        if (g == 1) {
            char *s = malloc(len + 1);
            if (!s)
                return NULL;
            memcpy(s, p, len);
            s[len] = 0;
            *len_out = len;
            return s;
        }
        uint8_t buf[4096];
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return NULL;
        size_t off = 0;
        while (off < (size_t)n) {
            size_t used = frame_rx_feed(&rx, buf + off, (size_t)n - off);
            if (used == 0)
                break;
            off += used;
        }
    }
}

static int request(int fd, const char *cmd, char **resp, size_t *rlen) {
    uint8_t *out = malloc(FRAME_HDR + strlen(cmd));
    if (!out)
        return -1;
    int n = frame_encode(cmd, strlen(cmd), out, FRAME_HDR + strlen(cmd));
    int rc = n < 0 ? -1 : send_all(fd, out, (size_t)n);
    free(out);
    (void)rc; /* a failed send may still be followed by a server-sent error frame */
    *resp = recv_frame(fd, rlen);
    return *resp ? 0 : -1;
}

static const char *sval(const struct jnode *o, const char *k, const char *dflt) {
    const struct jnode *n = json_get(o, k);
    return n && n->type == J_STR && n->str ? n->str : dflt;
}

static unsigned long long uval(const struct jnode *o, const char *k) {
    const struct jnode *n = json_get(o, k);
    return n && n->type == J_NUM ? (unsigned long long)n->u : 0ULL;
}

static void render_route_row(const struct jnode *r) {
    printf("%-16s%-6s%-12s%s\n", sval(r, "prefix", "?"), sval(r, "port", "-"),
           sval(r, "gateway", "-"), sval(r, "origin", "?"));
}

static void render_table(const char *cmd, const struct jnode *d) {
    if (strcmp(cmd, "route show") == 0) {
        printf("%-16s%-6s%-12s%s\n", "PREFIX", "PORT", "GATEWAY", "ORIGIN");
        for (const struct jnode *r = json_get(d, "routes") ? json_get(d, "routes")->child : NULL; r;
             r = r->next)
            render_route_row(r);
    } else if (strcmp(cmd, "route lookup") == 0) {
        printf("%-16s%-6s%-12s%s\n", "PREFIX", "PORT", "GATEWAY", "ORIGIN");
        render_route_row(json_get(d, "route"));
    } else if (strcmp(cmd, "port show") == 0) {
        printf("%-6s%-8s%-19s%-17s%-6s%-6s%-6s%10s%12s%10s%12s%8s\n", "NAME", "IFNAME", "MAC", "IP",
               "MTU", "ADMIN", "OPER", "RX_PKTS", "RX_BYTES", "TX_PKTS", "TX_BYTES", "DROPS");
        for (const struct jnode *r = json_get(d, "ports") ? json_get(d, "ports")->child : NULL; r;
             r = r->next)
            printf("%-6s%-8s%-19s%-17s%-6llu%-6s%-6s%10llu%12llu%10llu%12llu%8llu\n",
                   sval(r, "name", "?"), sval(r, "ifname", "?"), sval(r, "mac", "?"),
                   sval(r, "ip", "?"), uval(r, "mtu"), sval(r, "admin", "?"), sval(r, "oper", "?"),
                   uval(r, "rx_pkts"), uval(r, "rx_bytes"), uval(r, "tx_pkts"), uval(r, "tx_bytes"),
                   uval(r, "drops"));
    } else if (strcmp(cmd, "neigh show") == 0) {
        printf("%-16s%-19s%-6s%-9s%-12s%s\n", "IP", "MAC", "PORT", "KIND", "STATE", "HELD");
        for (const struct jnode *r =
                 json_get(d, "neighbours") ? json_get(d, "neighbours")->child : NULL;
             r; r = r->next)
            printf("%-16s%-19s%-6s%-9s%-12s%llu\n", sval(r, "ip", "?"), sval(r, "mac", "-"),
                   sval(r, "port", "?"), sval(r, "kind", "?"), sval(r, "state", "?"),
                   uval(r, "held"));
    } else if (strcmp(cmd, "stats show") == 0) {
        printf("%-6s%10s%12s%10s%12s%8s\n", "PORT", "RX_PKTS", "RX_BYTES", "TX_PKTS", "TX_BYTES",
               "DROPS");
        for (const struct jnode *r = json_get(d, "ports") ? json_get(d, "ports")->child : NULL; r;
             r = r->next)
            printf("%-6s%10llu%12llu%10llu%12llu%8llu\n", sval(r, "name", "?"), uval(r, "rx_pkts"),
                   uval(r, "rx_bytes"), uval(r, "tx_pkts"), uval(r, "tx_bytes"), uval(r, "drops"));
        const struct jnode *dr = json_get(d, "drops");
        printf("\nDROPS total %llu\n", uval(dr, "total"));
        const struct jnode *reasons = json_get(dr, "reasons");
        for (const struct jnode *r = reasons ? reasons->child : NULL; r; r = r->next)
            printf("  %-24s%llu\n", r->key ? r->key : "?", (unsigned long long)r->u);
        const struct jnode *ev = json_get(d, "events");
        printf("\nEVENTS\n");
        for (const struct jnode *r = ev ? ev->child : NULL; r; r = r->next)
            printf("  %-24s%llu\n", r->key ? r->key : "?", (unsigned long long)r->u);
        const struct jnode *mb = json_get(d, "mbuf");
        printf("\nMBUF in_use %llu / %llu\n", uval(mb, "in_use"), uval(mb, "total"));
    } else {
        puts("ok");
    }
}

static int render(const char *cmd, const struct jnode *root, const char *raw, int json) {
    const struct jnode *ok = json_get(root, "ok");
    if (!ok || ok->type != J_BOOL) {
        fprintf(stderr, "vrctl: malformed response\n");
        return EXIT_UNREACHABLE;
    }
    if (json) {
        puts(raw);
        return ok->b ? 0 : EXIT_REJECTED;
    }
    if (!ok->b) {
        const struct jnode *e = json_get(root, "error");
        const struct jnode *code = json_get(e, "code"), *msg = json_get(e, "message");
        fprintf(stderr, "%s: %s\n", code && code->str ? code->str : "error",
                msg && msg->str ? msg->str : "");
        return EXIT_REJECTED;
    }
    const struct jnode *d = json_get(root, "data");
    char key[32];
    {
        const char *sp = strchr(cmd, ' ');
        const char *sp2 = sp ? strchr(sp + 1, ' ') : NULL;
        size_t n = sp2 ? (size_t)(sp2 - cmd) : strlen(cmd);
        snprintf(key, sizeof key, "%.*s", (int)(n < sizeof key - 1 ? n : sizeof key - 1), cmd);
        cmd = key;
    }
    if (strcmp(cmd, "version") == 0) {
        const struct jnode *v = json_get(d, "version"), *b = json_get(d, "build"),
                           *p = json_get(d, "protocol"), *u = json_get(d, "uptime_s");
        printf("minivrd %s (%s build) protocol %llu uptime %llus\n", v && v->str ? v->str : "?",
               b && b->str ? b->str : "?", p ? (unsigned long long)p->u : 0ULL,
               u ? (unsigned long long)u->u : 0ULL);
        return 0;
    }
    (void)raw;
    render_table(cmd, d);
    return 0;
}

int main(int argc, char **argv) {
    const char *sock = getenv("MINIVR_SOCK");
    if (!sock || !*sock)
        sock = "/run/minivr/minivr.sock";
    int json = 0, o;
    while ((o = getopt(argc, argv, "+s:j")) != -1) {
        if (o == 's')
            sock = optarg;
        else if (o == 'j')
            json = 1;
        else {
            fprintf(stderr, "usage: vrctl [-s <socket>] [-j] <command> [args...]\n");
            return EXIT_USAGE;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "usage: vrctl [-s <socket>] [-j] <command> [args...]\n");
        return EXIT_USAGE;
    }
    if (!is_known(argv[optind], optind + 1 < argc ? argv[optind + 1] : NULL)) {
        fprintf(stderr, "vrctl: unknown command '%s'\n", argv[optind]);
        return EXIT_USAGE;
    }
    char cmd[1024];
    size_t n = 0;
    for (int i = optind; i < argc; i++) {
        size_t l = strlen(argv[i]);
        if (n + l + 2 > sizeof cmd) {
            fprintf(stderr, "vrctl: command too long\n");
            return EXIT_USAGE;
        }
        if (i > optind)
            cmd[n++] = ' ';
        memcpy(cmd + n, argv[i], l);
        n += l;
    }
    cmd[n] = 0;

    struct sockaddr_un addr;
    if (strlen(sock) >= sizeof addr.sun_path) {
        fprintf(stderr, "vrctl: socket path too long\n");
        return EXIT_USAGE;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, sock);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        fprintf(stderr, "vrctl: cannot connect to %s: %s\n", sock, strerror(errno));
        return EXIT_UNREACHABLE;
    }
    struct timeval tv = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    /* Handshake: refuse a server speaking another protocol version. */
    char *resp;
    size_t rlen;
    if (request(fd, "version", &resp, &rlen) != 0) {
        fprintf(stderr, "vrctl: no response from forwarder\n");
        return EXIT_UNREACHABLE;
    }
    struct jnode *root = json_parse(resp, rlen);
    const struct jnode *ok = json_get(root, "ok");
    if (!root || !ok || ok->type != J_BOOL) {
        fprintf(stderr, "vrctl: malformed response from forwarder\n");
        return EXIT_UNREACHABLE;
    }
    if (!ok->b) { /* e.g. not-permitted, busy */
        const struct jnode *e = json_get(root, "error");
        const struct jnode *code = json_get(e, "code"), *msg = json_get(e, "message");
        fprintf(stderr, "vrctl: forwarder refused the connection: %s: %s\n",
                code && code->str ? code->str : "error", msg && msg->str ? msg->str : "");
        return EXIT_UNREACHABLE;
    }
    const struct jnode *proto = json_get(json_get(root, "data"), "protocol");
    if (!proto || !proto->is_int || proto->u != PROTOCOL) {
        fprintf(stderr, "vrctl: protocol mismatch (client speaks %d)\n", PROTOCOL);
        return EXIT_UNREACHABLE;
    }
    int rc;
    if (strcmp(cmd, "version") == 0) {
        rc = render(cmd, root, resp, json);
    } else {
        json_free(root);
        free(resp);
        if (request(fd, cmd, &resp, &rlen) != 0) {
            fprintf(stderr, "vrctl: no response from forwarder\n");
            return EXIT_UNREACHABLE;
        }
        root = json_parse(resp, rlen);
        if (!root) {
            fprintf(stderr, "vrctl: malformed response from forwarder\n");
            return EXIT_UNREACHABLE;
        }
        rc = render(cmd, root, resp, json);
    }
    json_free(root);
    free(resp);
    close(fd);
    return rc;
}
