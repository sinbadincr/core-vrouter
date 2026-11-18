#ifndef MINIVR_ANTISPOOF_H
#define MINIVR_ANTISPOOF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pkt.h"
#include "port.h"

/* FR-014a: on a port with a bound VM, Ethernet src MAC, ARP sender MAC/IP and IPv4 src IP must
 * match the binding. Unbound ports pass. Frames too short to carry the field are left to the
 * parsers to drop with their own reason. */
bool antispoof_ok(const struct port *p, enum eth_class cls, const uint8_t *frame, size_t len);
#endif
