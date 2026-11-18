#ifndef MINIVR_ROUTE_H
#define MINIVR_ROUTE_H
#include <stdint.h>

#include "cmd.h"
#include "lpm.h"
#include "port.h"

#define ROUTE_NO_PORT (-1)

enum route_origin { ROUTE_CONNECTED, ROUTE_HOST, ROUTE_STATIC };

struct route {
    uint32_t prefix;  /* host bits zero */
    uint8_t len;
    int8_t port;      /* egress port id, or ROUTE_NO_PORT for a multi-port connected subnet */
    uint32_t gateway; /* 0 = none */
    enum route_origin origin;
    uint8_t used;
    uint32_t next_free;
};

typedef void (*route_walk_fn)(const struct route *r, void *arg);

const char *route_origin_name(enum route_origin o);

/* Connected route for a port's subnet; a second port on the same prefix makes it egress-less. */
int route_add_connected(const struct port *p, struct cmd_err *err);
int route_add_host(uint32_t ip, uint8_t port, struct cmd_err *err);
int route_del_host(uint32_t ip);
/* Errors: unknown-port, bad-prefix, gw-not-connected, exists, table-full. */
int route_add_static(uint32_t prefix, uint8_t len, uint8_t port, uint32_t gw, struct cmd_err *err);
/* Errors: not-found, immutable (connected/host). */
int route_del(uint32_t prefix, uint8_t len, struct cmd_err *err);

const struct route *route_lookup(uint32_t ip); /* LPM; NULL on miss */
const struct route *route_find(uint32_t prefix, uint8_t len);
unsigned route_count(void);
void route_walk(route_walk_fn fn, void *arg); /* prefix order */
void route_reset(void);
#endif
