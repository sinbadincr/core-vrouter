#include "minivr/port.h"

#include <string.h>

#include "minivr/clock.h"
#include "minivr/route.h"
#include "pio.h"

static struct port ports[MAX_PORTS];
static const struct pio_ops *backend;

void port_set_backend(const struct pio_ops *ops) {
    backend = ops;
}

const struct pio_ops *port_backend(void) {
    return backend;
}

struct port *port_get(uint8_t id) {
    return (id < MAX_PORTS && ports[id].in_use) ? &ports[id] : NULL;
}

struct port *port_find(const char *name) {
    for (unsigned i = 0; i < MAX_PORTS; i++)
        if (ports[i].in_use && strcmp(ports[i].name, name) == 0)
            return &ports[i];
    return NULL;
}

struct port *port_by_ip(uint32_t ip) {
    for (unsigned i = 0; i < MAX_PORTS; i++)
        if (ports[i].in_use && ports[i].ip == ip)
            return &ports[i];
    return NULL;
}

unsigned port_count(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < MAX_PORTS; i++)
        n += ports[i].in_use;
    return n;
}

uint32_t port_netmask(const struct port *p) {
    return p->prefix_len == 0 ? 0 : ~0U << (32 - p->prefix_len);
}

struct port *port_add(const char *name, const char *ifname, uint32_t ip, uint8_t prefix_len,
                      struct cmd_err *err) {
    if (!backend) {
        cmd_fail(err, "not-found", "no packet I/O backend configured");
        return NULL;
    }
    if (strlen(name) == 0 || strlen(name) > 15 || strlen(ifname) == 0 || strlen(ifname) > 15) {
        cmd_fail(err, "bad-syntax", "name and ifname must be 1..15 characters");
        return NULL;
    }
    if (port_find(name)) {
        cmd_fail(err, "exists", "port %s already exists", name);
        return NULL;
    }
    struct port *slot = NULL;
    for (unsigned i = 0; i < MAX_PORTS; i++) {
        struct port *p = &ports[i];
        if (!p->in_use) {
            if (!slot)
                slot = p;
            continue;
        }
        if (strcmp(p->ifname, ifname) == 0) {
            cmd_fail(err, "exists", "interface %s already used by port %s", ifname, p->name);
            return NULL;
        }
        if (p->ip == ip && p->prefix_len != prefix_len) {
            cmd_fail(err, "exists", "address shared with port %s but with a different prefix",
                     p->name);
            return NULL;
        }
    }
    if (!slot) {
        cmd_fail(err, "table-full", "at most %d ports", MAX_PORTS);
        return NULL;
    }
    void *h = backend->open(ifname);
    if (!h) {
        cmd_fail(err, "not-found", "cannot open interface %s", ifname);
        return NULL;
    }
    uint8_t mac[6];
    int mtu = backend->get_mtu(h);
    if (backend->get_mac(h, mac) != 0 || mtu <= 0 || mtu > 0xffff) {
        backend->close(h);
        cmd_fail(err, "not-found", "cannot read MAC/MTU of %s", ifname);
        return NULL;
    }
    memset(slot, 0, sizeof *slot);
    slot->id = (uint8_t)(slot - ports);
    slot->in_use = true;
    strncpy(slot->name, name, sizeof slot->name - 1);
    strncpy(slot->ifname, ifname, sizeof slot->ifname - 1);
    memcpy(slot->mac, mac, 6);
    slot->ip = ip;
    slot->prefix_len = prefix_len;
    slot->mtu = (uint16_t)mtu;
    slot->admin_up = true;
    slot->oper_up = true;
    slot->pio = h;
    tb_init(&slot->icmp_bucket, ratelimit_rate(), ratelimit_burst(), clock_now_ns());
    if (route_add_connected(slot, err) != 0) {
        backend->close(h);
        memset(slot, 0, sizeof *slot);
        return NULL;
    }
    return slot;
}

void port_close_all(void) {
    for (unsigned i = 0; i < MAX_PORTS; i++) {
        if (ports[i].in_use && backend) {
            backend->close(ports[i].pio);
            ports[i].pio = NULL;
        }
    }
}

void port_reset(void) {
    port_close_all();
    memset(ports, 0, sizeof ports);
}
