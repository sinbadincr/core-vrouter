#include "minivr/pkt.h"

int pkt_arp_parse(const uint8_t *frame, size_t len, struct arp_pkt *out) {
    if (len < ARP_FRAME_LEN)
        return -1;
    const uint8_t *a = frame + ETH_HDR_LEN;
    if (rd16(a) != 1 || rd16(a + 2) != ETHERTYPE_IPV4 || a[4] != ETH_ALEN || a[5] != 4)
        return -1;
    uint16_t op = rd16(a + 6);
    if (op != ARP_OP_REQUEST && op != ARP_OP_REPLY)
        return -1;
    out->op = op;
    memcpy(out->sha, a + 8, ETH_ALEN);
    out->spa = rd32(a + 14);
    memcpy(out->tha, a + 18, ETH_ALEN);
    out->tpa = rd32(a + 24);
    return 0;
}

void pkt_arp_make_reply(uint8_t *frame, const uint8_t mac[ETH_ALEN]) {
    uint8_t *a = frame + ETH_HDR_LEN;
    uint8_t req_sha[ETH_ALEN], req_spa[4], req_tpa[4];
    memcpy(req_sha, a + 8, ETH_ALEN);
    memcpy(req_spa, a + 14, 4);
    memcpy(req_tpa, a + 24, 4);
    memcpy(frame, req_sha, ETH_ALEN);     /* eth dst = requester */
    memcpy(frame + 6, mac, ETH_ALEN);     /* eth src = us */
    wr16(a + 6, ARP_OP_REPLY);
    memcpy(a + 8, mac, ETH_ALEN);         /* sender = us ... */
    memcpy(a + 14, req_tpa, 4);           /* ... claiming the asked-for IP */
    memcpy(a + 18, req_sha, ETH_ALEN);    /* target = requester */
    memcpy(a + 24, req_spa, 4);
}

void pkt_arp_build_request(uint8_t *out, const uint8_t src_mac[ETH_ALEN], uint32_t src_ip,
                           uint32_t target_ip) {
    memset(out, 0xff, ETH_ALEN);
    memcpy(out + 6, src_mac, ETH_ALEN);
    wr16(out + 12, ETHERTYPE_ARP);
    uint8_t *a = out + ETH_HDR_LEN;
    wr16(a, 1);
    wr16(a + 2, ETHERTYPE_IPV4);
    a[4] = ETH_ALEN;
    a[5] = 4;
    wr16(a + 6, ARP_OP_REQUEST);
    memcpy(a + 8, src_mac, ETH_ALEN);
    wr32(a + 14, src_ip);
    memset(a + 18, 0, ETH_ALEN);
    wr32(a + 24, target_ip);
}
