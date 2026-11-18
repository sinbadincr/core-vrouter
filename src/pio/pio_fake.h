#ifndef MINIVR_PIO_FAKE_H
#define MINIVR_PIO_FAKE_H
#include <stddef.h>
#include <stdint.h>

#include "pio.h"

/* In-memory backend for unit tests. */
extern const struct pio_ops pio_fake_ops;

void pio_fake_reset(void); /* forget all fake interfaces (closes nothing) */
/* Make `ifname` exist with the given MAC/MTU; without this, open() fails. */
void pio_fake_add_if(const char *ifname, const uint8_t mac[6], int mtu);
/* Queue a received frame on the interface (copied into an mbuf by rx_burst). */
int pio_fake_inject(void *h, const void *frame, size_t len);
/* Same, but flagged as if the kernel had stripped an 802.1Q tag (PACKET_AUXDATA). */
int pio_fake_inject_vlan(void *h, const void *frame, size_t len);
/* Number of frames transmitted so far, and access to them (valid until pio_fake_clear_tx). */
size_t pio_fake_tx_count(void *h);
const uint8_t *pio_fake_tx_frame(void *h, size_t idx, size_t *len);
void pio_fake_clear_tx(void *h);
#endif
