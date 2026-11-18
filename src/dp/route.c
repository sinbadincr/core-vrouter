#include "minivr/route.h"

#include <string.h>

#include "minivr/neigh.h"

static struct lpm table;
/* Route entries live in a flat array indexed by the trie's value, so a lookup is the trie walk
 * (see lpm.h) plus exactly one entry read. Entries are ~28 bytes and unpadded: nothing writes
 * them on the packet path, so there is no false sharing; mutation happens between bursts on the
 * same thread. Phase 4 will make updates RCU-style; the layout can then be reconsidered. */
static struct route routes[LPM_MAX_RULES];
static uint32_t free_head; /* index+1 of the first free slot, 0 = none */
static uint32_t used_count;
static int inited;

static void ensure_init(void) {
    if (inited)
        return;
    inited = 1;
    lpm_init(&table);
    memset(routes, 0, sizeof routes);
    free_head = 0;
    for (uint32_t i = LPM_MAX_RULES; i-- > 0;) {
        routes[i].next_free = free_head;
        free_head = i + 1;
    }
    used_count = 0;
}

void route_reset(void) {
    inited = 0;
    ensure_init();
}

const char *route_origin_name(enum route_origin o) {
    switch (o) {
    case ROUTE_CONNECTED: return "connected";
    case ROUTE_HOST: return "host";
    default: return "static";
    }
}

static uint32_t mask_of(uint8_t len) {
    return len == 0 ? 0 : ~0U << (32 - len);
}

static struct route *slot_alloc(uint32_t *idx) {
    if (!free_head)
        return NULL;
    uint32_t i = free_head - 1;
    free_head = routes[i].next_free;
    memset(&routes[i], 0, sizeof routes[i]);
    routes[i].used = 1;
    used_count++;
    *idx = i;
    return &routes[i];
}

static void slot_free(uint32_t idx) {
    routes[idx].used = 0;
    routes[idx].next_free = free_head;
    free_head = idx + 1;
    used_count--;
}

static int insert(uint32_t prefix, uint8_t len, int port, uint32_t gw, enum route_origin origin,
                  struct cmd_err *err) {
    ensure_init();
    uint32_t idx;
    struct route *r = slot_alloc(&idx);
    if (!r)
        return cmd_fail(err, "table-full", "route table full (%d)", LPM_MAX_RULES);
    r->prefix = prefix;
    r->len = len;
    r->port = (int8_t)port;
    r->gateway = gw;
    r->origin = origin;
    int rc = lpm_add(&table, prefix, len, idx);
    if (rc != 0) {
        slot_free(idx);
        return rc == -1 ? cmd_fail(err, "exists", "route already exists")
                        : cmd_fail(err, "table-full", "route table full (%d)", LPM_MAX_RULES);
    }
    return 0;
}

static const struct route *exact(uint32_t prefix, uint8_t len) {
    ensure_init();
    uint32_t idx;
    return lpm_get(&table, prefix, len, &idx) ? &routes[idx] : NULL;
}

const struct route *route_find(uint32_t prefix, uint8_t len) {
    return exact(prefix, len);
}

int route_add_connected(const struct port *p, struct cmd_err *err) {
    ensure_init();
    uint32_t prefix = p->ip & port_netmask(p);
    const struct route *existing = exact(prefix, p->prefix_len);
    if (existing) {
        if (existing->origin == ROUTE_CONNECTED) {
            ((struct route *)existing)->port = ROUTE_NO_PORT; /* multi-port subnet */
            return 0;
        }
        return cmd_fail(err, "exists", "a route for the port subnet already exists");
    }
    return insert(prefix, p->prefix_len, p->id, 0, ROUTE_CONNECTED, err);
}

int route_add_host(uint32_t ip, uint8_t port, struct cmd_err *err) {
    return insert(ip, 32, port, 0, ROUTE_HOST, err);
}

int route_del_host(uint32_t ip) {
    const struct route *r = exact(ip, 32);
    if (!r || r->origin != ROUTE_HOST)
        return -1;
    uint32_t idx = (uint32_t)(r - routes);
    lpm_del(&table, ip, 32);
    slot_free(idx);
    return 0;
}

int route_add_static(uint32_t prefix, uint8_t len, uint8_t port, uint32_t gw, struct cmd_err *err) {
    struct port *p = port_get(port);
    if (!p)
        return cmd_fail(err, "unknown-port", "no such port");
    if (len > 32 || (prefix & ~mask_of(len)))
        return cmd_fail(err, "bad-prefix", "host bits set in prefix");
    if (gw && (gw & port_netmask(p)) != (p->ip & port_netmask(p)))
        return cmd_fail(err, "gw-not-connected", "gateway is not in the subnet of port %s",
                        p->name);
    return insert(prefix, len, port, gw, ROUTE_STATIC, err);
}

int route_del(uint32_t prefix, uint8_t len, struct cmd_err *err) {
    const struct route *r = exact(prefix, len);
    if (!r)
        return cmd_fail(err, "not-found", "no such route");
    if (r->origin != ROUTE_STATIC)
        return cmd_fail(err, "immutable", "%s routes cannot be deleted", route_origin_name(r->origin));
    uint32_t gw = r->gateway;
    uint32_t idx = (uint32_t)(r - routes);
    lpm_del(&table, prefix, len);
    slot_free(idx);
    if (gw)
        neigh_gateway_removed(gw);
    return 0;
}

const struct route *route_lookup(uint32_t ip) {
    ensure_init();
    uint32_t idx;
    if (!lpm_lookup(&table, ip, &idx, NULL))
        return NULL;
    return &routes[idx];
}

unsigned route_count(void) {
    return used_count;
}

struct walk_ctx {
    route_walk_fn fn;
    void *arg;
};

static void walk_cb(uint32_t prefix, uint8_t len, uint32_t value, void *arg) {
    (void)prefix;
    (void)len;
    struct walk_ctx *c = arg;
    c->fn(&routes[value], c->arg);
}

void route_walk(route_walk_fn fn, void *arg) {
    ensure_init();
    struct walk_ctx c = {fn, arg};
    lpm_walk(&table, walk_cb, &c);
}
