#include "fixture.h"
#include "tharness.h"

static uint8_t f[2048];

static size_t echo_from_vm1(uint32_t dst, uint8_t ttl) {
    return make_echo(f, MAC_P1, MAC_VM1, VM1_IP, dst, ttl, ICMP_ECHO_REQUEST, 7, 1);
}

static void check_invariants(void) {
    CHECK_EQ_U64(stats_reason_sum(), g_stats.drop_total);
}

static const uint8_t *tx1(int pn, size_t idx, size_t *len) {
    return fx_tx(pn, idx, len);
}

TEST(forward_vm1_to_vm3_rewrites_l2_and_ttl) {
    fx_setup(1);
    size_t n = echo_from_vm1(VM3_IP, 64);
    fx_rx(1, f, n);
    CHECK_EQ_U64(fx_tx_count(3), 1);
    size_t len;
    const uint8_t *o = tx1(3, 0, &len);
    CHECK_EQ_U64(len, n);
    CHECK_MEM_EQ(o, MAC_VM3, 6);
    CHECK_MEM_EQ(o + 6, MAC_P3, 6);
    CHECK_EQ_U64(o[ETH_HDR_LEN + 8], 63);
    CHECK_EQ_U64(pkt_csum16(o + ETH_HDR_LEN, IPV4_HDR_LEN), 0);
    CHECK_EQ_U64(g_stats.port[0].rx_pkts, 1);
    CHECK_EQ_U64(g_stats.port[2].tx_pkts, 1);
    CHECK_EQ_U64(g_stats.drop_total, 0);
    CHECK_EQ_U64(mbuf_in_use(), 0);
    fx_teardown();
}

TEST(forward_vm1_to_vm2_across_ports_of_one_subnet) {
    fx_setup(1);
    size_t n = echo_from_vm1(VM2_IP, 64);
    fx_rx(1, f, n);
    CHECK_EQ_U64(fx_tx_count(2), 1);
    size_t len;
    const uint8_t *o = tx1(2, 0, &len);
    CHECK_MEM_EQ(o, MAC_VM2, 6);
    CHECK_MEM_EQ(o + 6, MAC_P2, 6);
    CHECK_EQ_U64(fx_tx_count(1), 0);
    fx_teardown();
}

TEST(ethernet_padding_is_stripped) {
    fx_setup(1);
    size_t n = echo_from_vm1(VM3_IP, 64);
    memset(f + n, 0, 20);
    fx_rx(1, f, n + 20);
    size_t len;
    tx1(3, 0, &len);
    CHECK_EQ_U64(len, n);
    fx_teardown();
}

TEST(arp_for_gateway_known_vm_and_unknown) {
    fx_setup(1);
    size_t len;
    size_t n = make_arp_request(f, MAC_VM1, VM1_IP, GW1_IP);
    fx_rx(1, f, n);
    CHECK_EQ_U64(fx_tx_count(1), 1);
    const uint8_t *o = tx1(1, 0, &len);
    CHECK_MEM_EQ(o, MAC_VM1, 6);
    CHECK_MEM_EQ(o + 6, MAC_P1, 6);
    CHECK_EQ_U64(rd16(o + ETH_HDR_LEN + 6), ARP_OP_REPLY);
    CHECK_MEM_EQ(o + ETH_HDR_LEN + 8, MAC_P1, 6);

    fx_clear_tx();
    n = make_arp_request(f, MAC_VM1, VM1_IP, VM2_IP); /* proxy for vm2 */
    fx_rx(1, f, n);
    o = tx1(1, 0, &len);
    CHECK(o != NULL);
    CHECK_EQ_U64(rd32(o + ETH_HDR_LEN + 14), VM2_IP);
    CHECK_MEM_EQ(o + ETH_HDR_LEN + 8, MAC_P1, 6);
    CHECK_EQ_U64(fx_tx_count(2), 0); /* nothing leaks to vm2's port */

    fx_clear_tx();
    n = make_arp_request(f, MAC_VM1, VM1_IP, IP4(10, 0, 1, 99));
    fx_rx(1, f, n);
    CHECK_EQ_U64(fx_tx_count(1), 0);
    CHECK_EQ_U64(g_stats.reasons[DROP_ARP_UNKNOWN_TARGET], 1);
    CHECK_EQ_U64(g_stats.arp_replies_sent, 2);
    check_invariants();
    fx_teardown();
}

TEST(echo_to_the_gateway_is_answered_locally) {
    fx_setup(1);
    size_t n = echo_from_vm1(GW1_IP, 64);
    fx_rx(1, f, n);
    CHECK_EQ_U64(fx_tx_count(1), 1);
    size_t len;
    const uint8_t *o = tx1(1, 0, &len);
    CHECK_MEM_EQ(o, MAC_VM1, 6);
    CHECK_MEM_EQ(o + 6, MAC_P1, 6);
    CHECK_EQ_U64(rd32(o + ETH_HDR_LEN + 12), GW1_IP);
    CHECK_EQ_U64(o[ETH_HDR_LEN + IPV4_HDR_LEN], ICMP_ECHO_REPLY);
    CHECK_EQ_U64(g_stats.drop_total, 0);
    fx_teardown();
}

static void check_icmp_on_p1(uint8_t type, uint8_t code) {
    CHECK_EQ_U64(fx_tx_count(1), 1);
    size_t len;
    const uint8_t *o = tx1(1, 0, &len);
    if (!o)
        return;
    CHECK_MEM_EQ(o, MAC_VM1, 6);
    CHECK_EQ_U64(rd32(o + ETH_HDR_LEN + 12), GW1_IP);
    CHECK_EQ_U64(rd32(o + ETH_HDR_LEN + 16), VM1_IP);
    CHECK_EQ_U64(o[ETH_HDR_LEN + IPV4_HDR_LEN], type);
    CHECK_EQ_U64(o[ETH_HDR_LEN + IPV4_HDR_LEN + 1], code);
}

TEST(no_route_gives_net_unreachable) {
    fx_setup(1);
    fx_rx(1, f, echo_from_vm1(IP4(10, 9, 9, 9), 64));
    check_icmp_on_p1(ICMP_UNREACH, ICMP_UNREACH_NET);
    CHECK_EQ_U64(g_stats.reasons[DROP_NO_ROUTE], 1);
    check_invariants();
    fx_teardown();
}

TEST(shared_subnet_without_host_route_gives_host_unreachable) {
    fx_setup(1);
    fx_rx(1, f, echo_from_vm1(IP4(10, 0, 1, 99), 64));
    check_icmp_on_p1(ICMP_UNREACH, ICMP_UNREACH_HOST);
    CHECK_EQ_U64(g_stats.reasons[DROP_NO_ROUTE], 1);
    fx_teardown();
}

TEST(ttl_one_gives_time_exceeded) {
    fx_setup(1);
    fx_rx(1, f, echo_from_vm1(VM3_IP, 1));
    check_icmp_on_p1(ICMP_TIME_EXCEEDED, 0);
    CHECK_EQ_U64(g_stats.reasons[DROP_TTL_EXCEEDED], 1);
    CHECK_EQ_U64(fx_tx_count(3), 0);
    fx_teardown();
}

TEST(oversize_with_df_gives_frag_needed) {
    fx_setup(1);
    static uint8_t big[1700];
    size_t n = make_ipv4(f, MAC_P1, MAC_VM1, VM1_IP, VM3_IP, 64, 17, big, 1580); /* total 1600 */
    wr16(f + ETH_HDR_LEN + 6, IP_DF);
    pkt_ipv4_set_csum(f + ETH_HDR_LEN);
    fx_rx(1, f, n);
    check_icmp_on_p1(ICMP_UNREACH, ICMP_UNREACH_FRAG_NEEDED);
    size_t len;
    const uint8_t *o = tx1(1, 0, &len);
    CHECK_EQ_U64(rd16(o + ETH_HDR_LEN + IPV4_HDR_LEN + 6), 1500);
    CHECK_EQ_U64(g_stats.reasons[DROP_MTU_EXCEEDED], 1);

    fx_clear_tx(); /* without DF: dropped, no ICMP */
    n = make_ipv4(f, MAC_P1, MAC_VM1, VM1_IP, VM3_IP, 64, 17, big, 1580);
    fx_rx(1, f, n);
    CHECK_EQ_U64(fx_tx_count(1), 0);
    CHECK_EQ_U64(g_stats.reasons[DROP_MTU_EXCEEDED], 2);
    check_invariants();
    fx_teardown();
}

TEST(non_echo_to_own_ip_is_unsupported) {
    fx_setup(1);
    size_t n = make_ipv4(f, MAC_P1, MAC_VM1, VM1_IP, GW1_IP, 64, 17, "abcdefgh", 8);
    fx_rx(1, f, n);
    CHECK_EQ_U64(g_stats.reasons[DROP_LOCAL_UNSUPPORTED], 1);
    CHECK_EQ_U64(fx_tx_count(1), 0);
    fx_teardown();
}

TEST(egress_down_is_dropped) {
    fx_setup(1);
    port_find("p3")->oper_up = false;
    fx_rx(1, f, echo_from_vm1(VM3_IP, 64));
    CHECK_EQ_U64(g_stats.reasons[DROP_PORT_DOWN], 1);
    CHECK_EQ_U64(fx_tx_count(3), 0);
    port_find("p3")->oper_up = true;
    port_find("p3")->admin_up = false;
    fx_rx(1, f, echo_from_vm1(VM3_IP, 64));
    CHECK_EQ_U64(g_stats.reasons[DROP_PORT_DOWN], 2);
    fx_teardown();
}

TEST(hairpin_is_forwarded) {
    fx_setup(1);
    struct cmd_err e;
    CHECK(route_add_static(IP4(10, 0, 9, 0), 24, port_find("p1")->id, VM1_IP, &e) == 0);
    fx_rx(1, f, echo_from_vm1(IP4(10, 0, 9, 5), 64));
    CHECK_EQ_U64(fx_tx_count(1), 1);
    size_t len;
    const uint8_t *o = tx1(1, 0, &len);
    CHECK_MEM_EQ(o, MAC_VM1, 6);
    CHECK_EQ_U64(o[ETH_HDR_LEN + 8], 63);
    fx_teardown();
}

TEST(spoofed_sources_are_dropped_before_anything_else) {
    fx_setup(1);
    size_t n = make_echo(f, MAC_P1, MAC_VM1, VM2_IP, VM3_IP, 64, ICMP_ECHO_REQUEST, 1, 1);
    fx_rx(1, f, n);
    n = make_echo(f, MAC_P1, MAC_VM1, GW1_IP, VM3_IP, 64, ICMP_ECHO_REQUEST, 1, 1);
    fx_rx(1, f, n);
    n = make_arp_request(f, MAC_VM1, VM2_IP, GW1_IP);
    fx_rx(1, f, n);
    CHECK_EQ_U64(g_stats.reasons[DROP_SOURCE_SPOOFED], 3);
    CHECK_EQ_U64(g_stats.drop_total, 3);
    CHECK_EQ_U64(fx_tx_count(3) + fx_tx_count(1) + fx_tx_count(2), 0);
    fx_teardown();
}

TEST(malformed_frames_are_counted_once_each) {
    fx_setup(1);
    size_t n = echo_from_vm1(VM3_IP, 64);
    fx_rx(1, f, 10);                    /* truncated */
    f[ETH_HDR_LEN] = 0x65;              /* version 6 */
    fx_rx(1, f, n);
    n = make_ipv4(f, MAC_BCAST, MAC_VM1, VM1_IP, VM3_IP, 64, 17, "x", 1);
    fx_rx(1, f, n);                     /* broadcast non-ARP */
    n = make_ipv4(f, MAC_P2, MAC_VM1, VM1_IP, VM3_IP, 64, 17, "x", 1);
    fx_rx(1, f, n);                     /* not for us */
    n = echo_from_vm1(VM3_IP, 64);
    wr16(f + 12, 0x86dd);
    fx_rx(1, f, n);                     /* IPv6 ethertype */
    CHECK_EQ_U64(g_stats.reasons[DROP_TRUNCATED], 1);
    CHECK_EQ_U64(g_stats.reasons[DROP_BAD_IP_VERSION], 1);
    CHECK_EQ_U64(g_stats.reasons[DROP_NON_ARP_BCAST_MCAST], 1);
    CHECK_EQ_U64(g_stats.reasons[DROP_NOT_FOR_US_L2], 1);
    CHECK_EQ_U64(g_stats.reasons[DROP_UNSUPPORTED_ETHERTYPE], 1);
    CHECK_EQ_U64(g_stats.drop_total, 5);
    CHECK_EQ_U64(g_stats.port[0].drops, 5);
    check_invariants();
    CHECK_EQ_U64(mbuf_in_use(), 0);
    fx_teardown();
}

TEST(icmp_errors_are_rate_limited_but_drops_are_still_counted) {
    fx_setup(1);
    for (int i = 0; i < 25; i++)
        fx_rx(1, f, echo_from_vm1(IP4(10, 9, 9, 9), 64));
    CHECK_EQ_U64(g_stats.reasons[DROP_NO_ROUTE], 25);
    CHECK_EQ_U64(g_stats.icmp_sent, 20); /* burst */
    CHECK_EQ_U64(g_stats.icmp_ratelimited, 5);
    CHECK_EQ_U64(fx_tx_count(1), 20);
    check_invariants();
    fx_teardown();
}

TEST(unbound_port_learns_and_releases_the_held_packet) {
    fx_setup(0); /* vm3 unbound */
    size_t n = echo_from_vm1(VM3_IP, 64);
    fx_rx(1, f, n);
    /* the packet is held; the forwarder asked who has 10.0.2.13 on p3 */
    CHECK_EQ_U64(mbuf_in_use(), 1);
    CHECK_EQ_U64(fx_tx_count(3), 1);
    size_t len;
    const uint8_t *arp = tx1(3, 0, &len);
    CHECK_EQ_U64(rd16(arp + ETH_HDR_LEN + 6), ARP_OP_REQUEST);
    CHECK_EQ_U64(rd32(arp + ETH_HDR_LEN + 24), VM3_IP);

    fx_clear_tx();
    n = make_arp_reply(f, MAC_P3, MAC_VM3, VM3_IP, GW3_IP);
    fx_rx(3, f, n);
    CHECK_EQ_U64(fx_tx_count(3), 1); /* the held echo request, now with vm3's MAC */
    const uint8_t *o = tx1(3, 0, &len);
    CHECK_MEM_EQ(o, MAC_VM3, 6);
    CHECK_EQ_U64(o[ETH_HDR_LEN + 8], 63);
    const struct neigh *ne = neigh_lookup(VM3_IP);
    CHECK(ne && ne->kind == NEIGH_LEARNED && ne->state == NEIGH_REACHABLE);
    CHECK_EQ_U64(mbuf_in_use(), 0);
    CHECK_EQ_U64(g_stats.drop_total, 0);
    fx_teardown();
}

TEST(unbound_port_learns_from_arp_requests_too) {
    fx_setup(0);
    size_t n = make_arp_request(f, MAC_VM3, VM3_IP, GW3_IP);
    fx_rx(3, f, n);
    CHECK_EQ_U64(fx_tx_count(3), 1); /* reply for the gateway */
    CHECK(neigh_lookup(VM3_IP) != NULL);
    fx_teardown();
}

TEST(gratuitous_arp_is_consumed_silently) {
    fx_setup(0);
    size_t n = make_arp_request(f, MAC_VM3, VM3_IP, VM3_IP);
    fx_rx(3, f, n);
    CHECK_EQ_U64(fx_tx_count(3), 0);
    CHECK_EQ_U64(g_stats.drop_total, 0);
    CHECK(neigh_lookup(VM3_IP) != NULL);
    fx_teardown();
}

TEST(burst_of_many_frames_is_batched_without_leaks) {
    fx_setup(1);
    struct port *p1 = port_find("p1");
    for (int i = 0; i < 32; i++) {
        size_t n = echo_from_vm1(VM3_IP, 64);
        pio_fake_inject(p1->pio, f, n);
    }
    struct mbuf *burst[PIO_BURST];
    int n = port_backend()->rx_burst(p1->pio, burst, PIO_BURST);
    CHECK_EQ_U64(n, 32);
    forward_burst(p1, burst, n);
    CHECK_EQ_U64(fx_tx_count(3), 32);
    CHECK_EQ_U64(mbuf_in_use(), 0);
    CHECK_EQ_U64(g_stats.port[0].rx_pkts, 32);
    CHECK_EQ_U64(g_stats.port[2].tx_pkts, 32);
    fx_teardown();
}

TEST(kernel_stripped_vlan_tag_is_unsupported_ethertype) {
    fx_setup(1);
    struct port *p1 = port_find("p1");
    size_t n = echo_from_vm1(VM3_IP, 64); /* looks like plain IPv4 because the tag is gone */
    pio_fake_inject_vlan(p1->pio, f, n);
    struct mbuf *burst[PIO_BURST];
    int k = port_backend()->rx_burst(p1->pio, burst, PIO_BURST);
    forward_burst(p1, burst, k);
    CHECK_EQ_U64(g_stats.reasons[DROP_UNSUPPORTED_ETHERTYPE], 1);
    CHECK_EQ_U64(fx_tx_count(3), 0);
    CHECK_EQ_U64(mbuf_in_use(), 0);
    fx_teardown();
}
