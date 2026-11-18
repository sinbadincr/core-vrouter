#include <stdlib.h>

#include "minivr/pkt.h"
#include "tharness.h"
#include "vectors/frames.h"

static uint8_t f[2048];
static const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};

/* classify + validate; returns the drop reason or -1 if the frame is accepted as IPv4. */
static int verdict(const uint8_t *frame, size_t len) {
    enum drop_reason r;
    enum eth_class c = pkt_eth_classify(frame, len, MAC_P1, &r);
    if (c == ETH_C_DROP)
        return (int)r;
    if (c != ETH_C_IPV4)
        return 1000;
    if (pkt_ipv4_validate(frame, len, &r) != 0)
        return (int)r;
    return -1;
}

static size_t valid(void) {
    return make_ipv4(f, MAC_P1, MAC_VM1, VM1_IP, VM3_IP, 64, 17, payload, sizeof payload);
}

TEST(valid_frame_accepted) {
    size_t n = valid();
    CHECK_EQ_U64(verdict(f, n) + 1, 0);
    memset(f + n, 0, 20); /* Ethernet padding beyond total_len is fine */
    CHECK_EQ_U64(verdict(f, n + 20) + 1, 0);
}

TEST(truncated_at_every_boundary) {
    size_t n = valid();
    for (size_t len = 0; len < ETH_HDR_LEN + IPV4_HDR_LEN; len++)
        CHECK_EQ_U64(verdict(f, len), DROP_TRUNCATED);
    /* header complete but payload cut: total length now exceeds the frame */
    CHECK_EQ_U64(verdict(f, n - 1), DROP_BAD_IP_TOTAL_LEN);
}

TEST(ip_header_field_checks) {
    size_t n = valid();
    uint8_t *ip = f + ETH_HDR_LEN;

    ip[0] = 0x65; /* version 6 */
    CHECK_EQ_U64(verdict(f, n), DROP_BAD_IP_VERSION);

    valid();
    ip[0] = 0x44; /* IHL 4 */
    CHECK_EQ_U64(verdict(f, n), DROP_BAD_IP_HDR_LEN);

    valid();
    ip[0] = 0x4f; /* IHL 15 = 60 bytes, beyond the 28-byte IP part of this frame */
    CHECK_EQ_U64(verdict(f, n), DROP_BAD_IP_HDR_LEN);

    n = make_ipv4(f, MAC_P1, MAC_VM1, VM1_IP, VM3_IP, 64, 17, NULL, 0);
    uint8_t big[ETH_HDR_LEN + 60];
    memset(big, 0, sizeof big);
    memcpy(big, f, n);
    big[ETH_HDR_LEN] = 0x46; /* IHL 6: options present and inside the frame */
    CHECK_EQ_U64(verdict(big, sizeof big), DROP_IP_OPTIONS);

    n = valid();
    wr16(ip + 2, 10); /* total_len < IHL*4 */
    CHECK_EQ_U64(verdict(f, n), DROP_BAD_IP_TOTAL_LEN);
    wr16(ip + 2, (uint16_t)(n - ETH_HDR_LEN + 1)); /* total_len > frame */
    CHECK_EQ_U64(verdict(f, n), DROP_BAD_IP_TOTAL_LEN);

    n = valid();
    ip[10] ^= 0x01; /* checksum */
    CHECK_EQ_U64(verdict(f, n), DROP_BAD_IP_CHECKSUM);
}

TEST(fragments_rejected) {
    size_t n = valid();
    uint8_t *ip = f + ETH_HDR_LEN;
    wr16(ip + 6, IP_MF);
    pkt_ipv4_set_csum(ip);
    CHECK_EQ_U64(verdict(f, n), DROP_IP_FRAGMENT);
    wr16(ip + 6, 0x0005);
    pkt_ipv4_set_csum(ip);
    CHECK_EQ_U64(verdict(f, n), DROP_IP_FRAGMENT);
    wr16(ip + 6, IP_DF); /* DF alone is fine */
    pkt_ipv4_set_csum(ip);
    CHECK_EQ_U64(verdict(f, n) + 1, 0);
}

TEST(ethernet_classification) {
    size_t n = valid();
    wr16(f + 12, 0x8100); /* VLAN */
    CHECK_EQ_U64(verdict(f, n), DROP_UNSUPPORTED_ETHERTYPE);
    wr16(f + 12, 0x86dd); /* IPv6 */
    CHECK_EQ_U64(verdict(f, n), DROP_UNSUPPORTED_ETHERTYPE);

    n = make_ipv4(f, MAC_BCAST, MAC_VM1, VM1_IP, VM3_IP, 64, 17, payload, sizeof payload);
    CHECK_EQ_U64(verdict(f, n), DROP_NON_ARP_BCAST_MCAST);
    n = make_ipv4(f, MAC_MCAST, MAC_VM1, VM1_IP, VM3_IP, 64, 17, payload, sizeof payload);
    CHECK_EQ_U64(verdict(f, n), DROP_NON_ARP_BCAST_MCAST);
    n = make_ipv4(f, MAC_P2, MAC_VM1, VM1_IP, VM3_IP, 64, 17, payload, sizeof payload);
    CHECK_EQ_U64(verdict(f, n), DROP_NOT_FOR_US_L2);

    enum drop_reason r;
    make_arp_request(f, MAC_VM1, VM1_IP, GW1_IP); /* broadcast ARP is accepted */
    CHECK(pkt_eth_classify(f, ARP_FRAME_LEN, MAC_P1, &r) == ETH_C_ARP);
    make_arp_reply(f, MAC_P1, MAC_VM1, VM1_IP, GW1_IP); /* unicast ARP to us too */
    CHECK(pkt_eth_classify(f, ARP_FRAME_LEN, MAC_P1, &r) == ETH_C_ARP);
}

TEST(ttl_decrement_matches_full_recompute) {
    unsigned seed = 12345;
    for (int i = 0; i < 10000; i++) {
        uint8_t h[20];
        for (int k = 0; k < 20; k++) {
            seed = seed * 1103515245u + 12345u;
            h[k] = (uint8_t)(seed >> 16);
        }
        h[0] = 0x45;
        h[8] = (uint8_t)(2 + (h[8] % 254)); /* TTL >= 2 */
        pkt_ipv4_set_csum(h);
        uint8_t inc[20], full[20];
        memcpy(inc, h, 20);
        pkt_ipv4_dec_ttl(inc);
        memcpy(full, h, 20);
        full[8]--;
        pkt_ipv4_set_csum(full);
        if (memcmp(inc, full, 20) != 0) {
            TH_FAIL("iteration %d: incremental != recomputed", i);
            return;
        }
        CHECK_EQ_U64(pkt_csum16(inc, 20), 0);
    }
}
