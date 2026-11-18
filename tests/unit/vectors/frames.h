/* Byte-vector builders for good and malformed frames (valid by construction; tests mutate). */
#ifndef FRAMES_H
#define FRAMES_H
#include <stdint.h>
#include <string.h>

#include "minivr/pkt.h"

#define IP4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (d))

static const uint8_t MAC_P1[6] = {2, 0, 0, 0, 0, 1};
static const uint8_t MAC_P2[6] = {2, 0, 0, 0, 0, 2};
static const uint8_t MAC_P3[6] = {2, 0, 0, 0, 0, 3};
static const uint8_t MAC_VM1[6] = {2, 0, 0, 0, 1, 1};
static const uint8_t MAC_VM2[6] = {2, 0, 0, 0, 1, 2};
static const uint8_t MAC_VM3[6] = {2, 0, 0, 0, 1, 3};
static const uint8_t MAC_BCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t MAC_MCAST[6] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0x01};

#define VM1_IP IP4(10, 0, 1, 11)
#define VM2_IP IP4(10, 0, 1, 12)
#define VM3_IP IP4(10, 0, 2, 13)
#define GW1_IP IP4(10, 0, 1, 1)
#define GW3_IP IP4(10, 0, 2, 1)

/* Valid Ethernet + IPv4 (no options, DF clear) + payload. Returns the frame length. */
static inline size_t make_ipv4(uint8_t *f, const uint8_t *dmac, const uint8_t *smac, uint32_t src,
                               uint32_t dst, uint8_t ttl, uint8_t proto, const void *payload,
                               size_t plen) {
    memcpy(f, dmac, 6);
    memcpy(f + 6, smac, 6);
    wr16(f + 12, ETHERTYPE_IPV4);
    uint8_t *ip = f + ETH_HDR_LEN;
    ip[0] = 0x45;
    ip[1] = 0;
    wr16(ip + 2, (uint16_t)(IPV4_HDR_LEN + plen));
    wr16(ip + 4, 0x1234);
    wr16(ip + 6, 0);
    ip[8] = ttl;
    ip[9] = proto;
    wr32(ip + 12, src);
    wr32(ip + 16, dst);
    if (plen)
        memcpy(ip + IPV4_HDR_LEN, payload, plen);
    pkt_ipv4_set_csum(ip);
    return ETH_HDR_LEN + IPV4_HDR_LEN + plen;
}

/* ICMP echo request/reply with a 16-byte payload and a valid ICMP checksum. */
static inline size_t make_echo(uint8_t *f, const uint8_t *dmac, const uint8_t *smac, uint32_t src,
                               uint32_t dst, uint8_t ttl, uint8_t type, uint16_t id, uint16_t seq) {
    uint8_t icmp[ICMP_HDR_LEN + 16];
    memset(icmp, 0, sizeof icmp);
    icmp[0] = type;
    wr16(icmp + 4, id);
    wr16(icmp + 6, seq);
    for (int i = 0; i < 16; i++)
        icmp[ICMP_HDR_LEN + i] = (uint8_t)(0xa0 + i);
    wr16(icmp + 2, pkt_csum16(icmp, sizeof icmp));
    return make_ipv4(f, dmac, smac, src, dst, ttl, IPPROTO_ICMP_, icmp, sizeof icmp);
}

/* ARP request frame (broadcast) "who has tpa, tell spa". */
static inline size_t make_arp_request(uint8_t *f, const uint8_t *smac, uint32_t spa, uint32_t tpa) {
    pkt_arp_build_request(f, smac, spa, tpa);
    return ARP_FRAME_LEN;
}

static inline size_t make_arp_reply(uint8_t *f, const uint8_t *dmac, const uint8_t *smac,
                                    uint32_t spa, uint32_t tpa) {
    pkt_arp_build_request(f, smac, spa, tpa);
    memcpy(f, dmac, 6);
    wr16(f + ETH_HDR_LEN + 6, ARP_OP_REPLY);
    memcpy(f + ETH_HDR_LEN + 18, dmac, 6);
    return ARP_FRAME_LEN;
}

/* Literal vectors, so byte-exactness does not depend on the builders. */
static const uint8_t VEC_ARP_REQ[42] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02, 0x00, 0x00, 0x00, 0x01, 0x01, 0x08, 0x06,
    0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x0a, 0x00, 0x01, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x01, 0x01};
static const uint8_t VEC_ARP_REPLY[42] = {
    0x02, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06,
    0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x0a, 0x00, 0x01, 0x01, 0x02, 0x00, 0x00, 0x00, 0x01, 0x01, 0x0a, 0x00, 0x01, 0x0b};
#endif
