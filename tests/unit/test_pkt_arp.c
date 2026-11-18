#include "minivr/pkt.h"
#include "tharness.h"
#include "vectors/frames.h"

TEST(parse_request) {
    struct arp_pkt a;
    CHECK(pkt_arp_parse(VEC_ARP_REQ, sizeof VEC_ARP_REQ, &a) == 0);
    CHECK_EQ_U64(a.op, ARP_OP_REQUEST);
    CHECK_MEM_EQ(a.sha, MAC_VM1, 6);
    CHECK_EQ_U64(a.spa, VM1_IP);
    CHECK_EQ_U64(a.tpa, GW1_IP);
}

TEST(malformed_arp_is_rejected) {
    struct arp_pkt a;
    uint8_t f[64];
    for (size_t len = 0; len < ARP_FRAME_LEN; len++)
        CHECK(pkt_arp_parse(VEC_ARP_REQ, len, &a) == -1);
    struct {
        int off;
        uint8_t val;
    } muts[] = {{15, 6},   /* htype != 1 */
                {16, 0x86}, /* ptype != 0x0800 */
                {18, 8},   /* hlen != 6 */
                {19, 6},   /* plen != 4 */
                {21, 0},   /* op 0 */
                {21, 3}};  /* op 3 */
    for (size_t i = 0; i < sizeof muts / sizeof *muts; i++) {
        memcpy(f, VEC_ARP_REQ, sizeof VEC_ARP_REQ);
        f[muts[i].off] = muts[i].val;
        CHECK(pkt_arp_parse(f, sizeof VEC_ARP_REQ, &a) == -1);
    }
}

TEST(proxy_reply_is_byte_exact) {
    uint8_t f[64];
    memcpy(f, VEC_ARP_REQ, sizeof VEC_ARP_REQ);
    pkt_arp_make_reply(f, MAC_P1);
    CHECK_MEM_EQ(f, VEC_ARP_REPLY, sizeof VEC_ARP_REPLY);
    struct arp_pkt a;
    CHECK(pkt_arp_parse(f, sizeof VEC_ARP_REPLY, &a) == 0);
    CHECK_EQ_U64(a.op, ARP_OP_REPLY);
    CHECK_MEM_EQ(a.sha, MAC_P1, 6); /* the ingress port MAC answers for the asked IP */
}

TEST(proxy_reply_for_another_hosts_ip) {
    uint8_t f[64];
    make_arp_request(f, MAC_VM1, VM1_IP, VM2_IP);
    pkt_arp_make_reply(f, MAC_P1);
    struct arp_pkt a;
    CHECK(pkt_arp_parse(f, ARP_FRAME_LEN, &a) == 0);
    CHECK_EQ_U64(a.spa, VM2_IP); /* claims 10.0.1.12 ... */
    CHECK_MEM_EQ(a.sha, MAC_P1, 6); /* ... with the port's MAC */
    CHECK_EQ_U64(a.tpa, VM1_IP);
    CHECK_MEM_EQ(a.tha, MAC_VM1, 6);
    CHECK_MEM_EQ(f, MAC_VM1, 6);
    CHECK_MEM_EQ(f + 6, MAC_P1, 6);
}

TEST(request_builder_is_byte_exact) {
    uint8_t f[64];
    pkt_arp_build_request(f, MAC_VM1, VM1_IP, GW1_IP);
    CHECK_MEM_EQ(f, VEC_ARP_REQ, sizeof VEC_ARP_REQ);
}
