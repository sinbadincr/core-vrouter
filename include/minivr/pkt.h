#ifndef MINIVR_PKT_H
#define MINIVR_PKT_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stats.h"

/* Wire-format helpers and packet codecs. Every parser is bounds-checked against the
 * actual buffer length (FR-013). Addresses passed around are host byte order. */

#define ETH_ALEN 6
#define ETH_HDR_LEN 14
#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP 0x0806
#define IPV4_HDR_LEN 20
#define IPPROTO_ICMP_ 1
#define ICMP_HDR_LEN 8
#define ARP_PKT_LEN 28
#define ARP_FRAME_LEN (ETH_HDR_LEN + ARP_PKT_LEN)
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2
#define IP_DF 0x4000
#define IP_MF 0x2000
#define IP_OFFMASK 0x1fff

#define ICMP_ECHO_REPLY 0
#define ICMP_UNREACH 3
#define ICMP_ECHO_REQUEST 8
#define ICMP_TIME_EXCEEDED 11
#define ICMP_UNREACH_NET 0
#define ICMP_UNREACH_HOST 1
#define ICMP_UNREACH_FRAG_NEEDED 4

static inline uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static inline uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static inline void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* ---- Ethernet (pkt_eth.c) ---- */
enum eth_class { ETH_C_IPV4, ETH_C_ARP, ETH_C_DROP };
/* Order: truncated -> destination MAC (ingress MAC, or broadcast only for ARP) -> ethertype.
 * On ETH_C_DROP, *reason says why. */
enum eth_class pkt_eth_classify(const uint8_t *frame, size_t len, const uint8_t port_mac[ETH_ALEN],
                                enum drop_reason *reason);

/* ---- IPv4 (pkt_ipv4.c); `frame` is the whole Ethernet frame ---- */
uint16_t pkt_csum16(const void *data, size_t len); /* folded one's-complement checksum (to store) */
/* Order: truncated, version, IHL<5 or beyond frame, options, total length, checksum, fragment. */
int pkt_ipv4_validate(const uint8_t *frame, size_t len, enum drop_reason *reason);
/* TTL-- with RFC 1624 incremental checksum update; ip points at the IPv4 header. */
void pkt_ipv4_dec_ttl(uint8_t *ip);
void pkt_ipv4_set_csum(uint8_t *ip); /* recompute header checksum (IHL 5) */

/* ---- ARP (pkt_arp.c) ---- */
struct arp_pkt {
    uint16_t op;
    uint8_t sha[ETH_ALEN];
    uint32_t spa;
    uint8_t tha[ETH_ALEN];
    uint32_t tpa;
};
int pkt_arp_parse(const uint8_t *frame, size_t len, struct arp_pkt *out); /* 0, or -1 = bad_arp */
/* Turn a received ARP request into the reply, in place: sender = `mac` claiming the asked IP. */
void pkt_arp_make_reply(uint8_t *frame, const uint8_t mac[ETH_ALEN]);
/* Broadcast request "who has target_ip, tell src_ip" into out[ARP_FRAME_LEN]. */
void pkt_arp_build_request(uint8_t *out, const uint8_t src_mac[ETH_ALEN], uint32_t src_ip,
                           uint32_t target_ip);

/* ---- ICMP (pkt_icmp.c) ---- */
/* In place: Ethernet/IP swapped, type 0, TTL 64, checksums recomputed. frame is a validated
 * IPv4 echo request addressed to us; returns -1 if the ICMP part is shorter than 8 bytes. */
int pkt_icmp_echo_reply(uint8_t *frame, size_t len);
/* Whether an ICMP error may be generated for this (validated) IPv4 frame (RFC 1812 4.3.2.7). */
int pkt_icmp_error_allowed(const uint8_t *frame, size_t len);
/* Build an error into out (cap >= ICMP_ERR_MAX): Ethernet back to the sender, IP from src_ip,
 * quoting the original IP header + 8 bytes. mtu only used for type 3 code 4. Returns frame length. */
#define ICMP_ERR_MAX (ETH_HDR_LEN + IPV4_HDR_LEN + ICMP_HDR_LEN + IPV4_HDR_LEN + 8)
size_t pkt_icmp_build_error(uint8_t *out, const uint8_t *orig, size_t orig_len,
                            const uint8_t eth_src[ETH_ALEN], uint32_t src_ip, uint8_t type,
                            uint8_t code, uint16_t mtu);
#endif
