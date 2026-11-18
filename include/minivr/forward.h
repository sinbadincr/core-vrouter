#ifndef MINIVR_FORWARD_H
#define MINIVR_FORWARD_H
#include <stdint.h>

#include "mbuf.h"
#include "port.h"

/* Process one received burst: classify, forward, drop. Takes ownership of every mbuf. */
void forward_burst(struct port *in, struct mbuf **m, int n);

/* Helpers used by the neighbour table (also usable outside a burst); each transmits at once. */
void fwd_tx_now(uint8_t port, struct mbuf *m);              /* takes ownership */
void fwd_send_arp_request(uint8_t port, uint32_t target_ip);
void fwd_icmp_host_unreach(const struct mbuf *held);        /* rate-limited; does not free */
#endif
