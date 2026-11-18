/* Test fixture: three fake ports configured like lab/cn1.conf. */
#ifndef FIXTURE_H
#define FIXTURE_H
#include <stdio.h>
#include "minivr/clock.h"
#include "minivr/cmd.h"
#include "minivr/forward.h"
#include "minivr/mbuf.h"
#include "minivr/neigh.h"
#include "minivr/port.h"
#include "minivr/ratelimit.h"
#include "minivr/route.h"
#include "minivr/stats.h"
#include "pio_fake.h"
#include "vectors/frames.h"

static inline int fx_cmd(const char *line) {
    char buf[1024];
    struct jw w;
    struct cmd_err e;
    jw_init(&w, buf, sizeof buf);
    int rc = cmd_exec(line, CMD_CONFIG, &w, &e);
    if (rc != 0)
        fprintf(stderr, "  fx_cmd('%s') failed: %s: %s\n", line, e.code, e.message);
    return rc;
}

/* bind3 = 0 gives lab/cn1-nobind.conf (p3 has no static neighbour). */
static inline void fx_setup(int bind3) {
    clock_set_fake(1000 * NS_PER_SEC);
    mbuf_pool_init(MBUF_POOL_SIZE);
    stats_clear();
    port_reset();
    route_reset();
    neigh_reset();
    pio_fake_reset();
    ratelimit_configure(RL_DEFAULT_RATE, RL_DEFAULT_BURST);
    pio_fake_add_if("p1", MAC_P1, 1500);
    pio_fake_add_if("p2", MAC_P2, 1500);
    pio_fake_add_if("p3", MAC_P3, 1500);
    port_set_backend(&pio_fake_ops);
    fx_cmd("port add p1 ifname p1 ip 10.0.1.1/24");
    fx_cmd("port add p2 ifname p2 ip 10.0.1.1/24");
    fx_cmd("port add p3 ifname p3 ip 10.0.2.1/24");
    fx_cmd("neigh add 10.0.1.11 lladdr 02:00:00:00:01:01 dev p1");
    fx_cmd("neigh add 10.0.1.12 lladdr 02:00:00:00:01:02 dev p2");
    if (bind3)
        fx_cmd("neigh add 10.0.2.13 lladdr 02:00:00:00:01:03 dev p3");
}

static inline void fx_teardown(void) {
    neigh_reset();
    port_reset();
    route_reset();
    if (mbuf_pool_total())
        mbuf_pool_fini();
    clock_use_real();
}

/* Deliver a frame to port pN (1-based) and run one burst through the pipeline. */
static inline void fx_rx(int pn, const uint8_t *frame, size_t len) {
    struct port *p = port_find(pn == 1 ? "p1" : pn == 2 ? "p2" : "p3");
    pio_fake_inject(p->pio, frame, len);
    struct mbuf *burst[PIO_BURST];
    int n = port_backend()->rx_burst(p->pio, burst, PIO_BURST);
    forward_burst(p, burst, n);
}

static inline void *fx_pio(int pn) {
    return port_find(pn == 1 ? "p1" : pn == 2 ? "p2" : "p3")->pio;
}
static inline size_t fx_tx_count(int pn) {
    return pio_fake_tx_count(fx_pio(pn));
}
static inline const uint8_t *fx_tx(int pn, size_t idx, size_t *len) {
    return pio_fake_tx_frame(fx_pio(pn), idx, len);
}
static inline void fx_clear_tx(void) {
    for (int i = 1; i <= 3; i++)
        pio_fake_clear_tx(fx_pio(i));
}
#endif
