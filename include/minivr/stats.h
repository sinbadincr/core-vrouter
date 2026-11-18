#ifndef MINIVR_STATS_H
#define MINIVR_STATS_H
#include <stdint.h>

#include "json_out.h"

#define MAX_PORTS 16

#define DROP_REASONS(X)                                                                            \
    X(TRUNCATED, "truncated")                                                                      \
    X(BAD_IP_VERSION, "bad_ip_version")                                                            \
    X(BAD_IP_HDR_LEN, "bad_ip_hdr_len")                                                            \
    X(IP_OPTIONS, "ip_options")                                                                    \
    X(BAD_IP_TOTAL_LEN, "bad_ip_total_len")                                                        \
    X(BAD_IP_CHECKSUM, "bad_ip_checksum")                                                          \
    X(IP_FRAGMENT, "ip_fragment")                                                                  \
    X(UNSUPPORTED_ETHERTYPE, "unsupported_ethertype")                                              \
    X(NON_ARP_BCAST_MCAST, "non_arp_bcast_mcast")                                                  \
    X(NOT_FOR_US_L2, "not_for_us_l2")                                                              \
    X(BAD_ARP, "bad_arp")                                                                          \
    X(ARP_UNKNOWN_TARGET, "arp_unknown_target")                                                    \
    X(SOURCE_SPOOFED, "source_spoofed")                                                            \
    X(NO_ROUTE, "no_route")                                                                        \
    X(TTL_EXCEEDED, "ttl_exceeded")                                                                \
    X(MTU_EXCEEDED, "mtu_exceeded")                                                                \
    X(NEIGHBOUR_UNRESOLVED, "neighbour_unresolved")                                                \
    X(PORT_DOWN, "port_down")                                                                      \
    X(LOCAL_UNSUPPORTED, "local_unsupported")                                                      \
    X(NOBUF, "nobuf")                                                                              \
    X(TX_ERROR, "tx_error")

enum drop_reason {
#define X(id, name) DROP_##id,
    DROP_REASONS(X)
#undef X
        DROP_COUNT
};

struct port_stats {
    uint64_t rx_pkts, rx_bytes, tx_pkts, tx_bytes, drops;
};

struct stats {
    uint64_t drop_total;
    uint64_t reasons[DROP_COUNT];
    struct port_stats port[MAX_PORTS];
    uint64_t icmp_ratelimited, neighbour_table_full, arp_replies_sent, icmp_sent;
};

/* One global instance owned by the forwarding thread. In phase 1 a single writer means plain
 * increments and no padding are needed; the per-port blocks are separate 40-byte structs so phase 4
 * can turn them into per-core copies, padded to cache lines, summed on read. */
extern struct stats g_stats;

const char *stats_reason_name(enum drop_reason r);
void stats_drop(enum drop_reason r);                      /* counts total + reason */
void stats_drop_port(uint8_t port, enum drop_reason r);   /* also the ingress port's drops */
void stats_clear(void);                                   /* counters only, not gauges */
uint64_t stats_reason_sum(void);
void stats_json_drops(struct jw *w);  /* "drops":{total,reasons:{all 21}} */
void stats_json_events(struct jw *w); /* "events":{...} */
#endif
