#ifndef MINIVR_PORT_H
#define MINIVR_PORT_H
#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "ratelimit.h"
#include "stats.h"

struct pio_ops;

struct port {
    uint8_t id;
    bool in_use;
    char name[16];
    char ifname[16];
    uint8_t mac[6];
    uint32_t ip; /* host byte order */
    uint8_t prefix_len;
    uint16_t mtu;
    bool admin_up, oper_up;
    struct token_bucket icmp_bucket; /* ICMP error limiter (FR-011a) */
    void *pio; /* backend handle */
};

/* Select the packet I/O backend used by port_add (daemon: AF_PACKET, tests: fake). */
void port_set_backend(const struct pio_ops *ops);
const struct pio_ops *port_backend(void);

/* Config-time creation. Returns the port or NULL with err set:
 * table-full (>16), exists (name taken, or address shared with a different prefix),
 * bad-syntax (name/ifname too long), not-found (interface cannot be opened). */
struct port *port_add(const char *name, const char *ifname, uint32_t ip, uint8_t prefix_len,
                      struct cmd_err *err);
struct port *port_find(const char *name);
struct port *port_by_ip(uint32_t ip); /* first port whose gateway address is ip */
struct port *port_get(uint8_t id);
unsigned port_count(void);
uint32_t port_netmask(const struct port *p);
void port_close_all(void);
void port_reset(void); /* tests: close and forget every port */
#endif
