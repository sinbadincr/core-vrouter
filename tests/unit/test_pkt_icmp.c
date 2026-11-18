#include "minivr/pkt.h"
#include "tharness.h"
#include "vectors/frames.h"

TEST(echo_reply_in_place) {
    uint8_t f[256];
    size_t n = make_echo(f, MAC_P1, MAC_VM1, VM1_IP, GW1_IP, 64, ICMP_ECHO_REQUEST, 0x4242, 7);
    uint8_t orig[256];
    memcpy(orig, f, n);
    CHECK(pkt_icmp_echo_reply(f, n) == 0);
    const uint8_t *ip = f + ETH_HDR_LEN;
    CHECK_MEM_EQ(f, MAC_VM1, 6);
    CHECK_MEM_EQ(f + 6, MAC_P1, 6);
    CHECK_EQ_U64(rd32(ip + 12), GW1_IP);
    CHECK_EQ_U64(rd32(ip + 16), VM1_IP);
    CHECK_EQ_U64(pkt_csum16(ip, IPV4_HDR_LEN), 0);
    const uint8_t *icmp = ip + IPV4_HDR_LEN;
    CHECK_EQ_U64(icmp[0], ICMP_ECHO_REPLY);
    CHECK_EQ_U64(rd16(icmp + 4), 0x4242); /* id and seq preserved */
    CHECK_EQ_U64(rd16(icmp + 6), 7);
    CHECK_MEM_EQ(icmp + 8, orig + ETH_HDR_LEN + IPV4_HDR_LEN + 8, 16); /* payload preserved */
    CHECK_EQ_U64(pkt_csum16(icmp, ICMP_HDR_LEN + 16), 0);
}

TEST(echo_reply_rejects_short_icmp) {
    uint8_t f[128];
    size_t n = make_ipv4(f, MAC_P1, MAC_VM1, VM1_IP, GW1_IP, 64, IPPROTO_ICMP_, "\x08\x00\x00", 3);
    CHECK(pkt_icmp_echo_reply(f, n) == -1);
}

static void check_error(uint8_t type, uint8_t code, uint16_t mtu) {
    uint8_t orig[256], out[ICMP_ERR_MAX + 8];
    size_t n = make_echo(orig, MAC_P1, MAC_VM1, VM1_IP, VM3_IP, 1, ICMP_ECHO_REQUEST, 1, 1);
    size_t len = pkt_icmp_build_error(out, orig, n, MAC_P1, GW1_IP, type, code, mtu);
    CHECK_EQ_U64(len, ICMP_ERR_MAX);
    CHECK_MEM_EQ(out, MAC_VM1, 6);
    CHECK_MEM_EQ(out + 6, MAC_P1, 6);
    const uint8_t *ip = out + ETH_HDR_LEN;
    CHECK_EQ_U64(rd32(ip + 12), GW1_IP);
    CHECK_EQ_U64(rd32(ip + 16), VM1_IP);
    CHECK_EQ_U64(ip[9], IPPROTO_ICMP_);
    CHECK_EQ_U64(rd16(ip + 2), len - ETH_HDR_LEN);
    CHECK_EQ_U64(pkt_csum16(ip, IPV4_HDR_LEN), 0);
    const uint8_t *icmp = ip + IPV4_HDR_LEN;
    CHECK_EQ_U64(icmp[0], type);
    CHECK_EQ_U64(icmp[1], code);
    CHECK_EQ_U64(rd16(icmp + 6), (type == 3 && code == 4) ? mtu : 0);
    CHECK_MEM_EQ(icmp + ICMP_HDR_LEN, orig + ETH_HDR_LEN, IPV4_HDR_LEN + 8); /* quoted */
    CHECK_EQ_U64(pkt_csum16(icmp, len - ETH_HDR_LEN - IPV4_HDR_LEN), 0);
}

TEST(error_builders) {
    check_error(ICMP_UNREACH, ICMP_UNREACH_NET, 0);
    check_error(ICMP_UNREACH, ICMP_UNREACH_HOST, 0);
    check_error(ICMP_UNREACH, ICMP_UNREACH_FRAG_NEEDED, 1400);
    check_error(ICMP_TIME_EXCEEDED, 0, 0);
}

TEST(short_original_is_quoted_as_far_as_it_goes) {
    uint8_t orig[64], out[ICMP_ERR_MAX];
    size_t n = make_ipv4(orig, MAC_P1, MAC_VM1, VM1_IP, VM3_IP, 1, 17, NULL, 0); /* header only */
    size_t len = pkt_icmp_build_error(out, orig, n, MAC_P1, GW1_IP, ICMP_TIME_EXCEEDED, 0, 0);
    CHECK_EQ_U64(len, ETH_HDR_LEN + IPV4_HDR_LEN + ICMP_HDR_LEN + IPV4_HDR_LEN);
    CHECK_EQ_U64(pkt_csum16(out + ETH_HDR_LEN + IPV4_HDR_LEN, ICMP_HDR_LEN + IPV4_HDR_LEN), 0);
}

TEST(no_errors_about_errors_or_bad_sources) {
    uint8_t f[256];
    size_t n = make_echo(f, MAC_P1, MAC_VM1, VM1_IP, VM3_IP, 1, ICMP_ECHO_REQUEST, 1, 1);
    CHECK(pkt_icmp_error_allowed(f, n));
    size_t m = make_echo(f, MAC_P1, MAC_VM1, VM1_IP, VM3_IP, 1, ICMP_TIME_EXCEEDED, 1, 1);
    CHECK(!pkt_icmp_error_allowed(f, m));
    n = make_echo(f, MAC_P1, MAC_VM1, 0, VM3_IP, 1, ICMP_ECHO_REQUEST, 1, 1);
    CHECK(!pkt_icmp_error_allowed(f, n));
    n = make_echo(f, MAC_P1, MAC_VM1, IP4(224, 0, 0, 1), VM3_IP, 1, ICMP_ECHO_REQUEST, 1, 1);
    CHECK(!pkt_icmp_error_allowed(f, n));
    n = make_echo(f, MAC_P1, MAC_VM1, IP4(127, 0, 0, 1), VM3_IP, 1, ICMP_ECHO_REQUEST, 1, 1);
    CHECK(!pkt_icmp_error_allowed(f, n));
}
