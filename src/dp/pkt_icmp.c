#include "minivr/pkt.h"

int pkt_icmp_echo_reply(uint8_t *frame, size_t len) {
    uint8_t *ip = frame + ETH_HDR_LEN;
    size_t total = rd16(ip + 2);
    if (len < ETH_HDR_LEN + total || total < IPV4_HDR_LEN + ICMP_HDR_LEN)
        return -1;
    uint8_t tmp[ETH_ALEN];
    memcpy(tmp, frame, ETH_ALEN);
    memcpy(frame, frame + 6, ETH_ALEN);
    memcpy(frame + 6, tmp, ETH_ALEN);
    uint8_t a[4];
    memcpy(a, ip + 12, 4);
    memcpy(ip + 12, ip + 16, 4);
    memcpy(ip + 16, a, 4);
    ip[8] = 64;
    pkt_ipv4_set_csum(ip);
    uint8_t *icmp = ip + IPV4_HDR_LEN;
    icmp[0] = ICMP_ECHO_REPLY;
    wr16(icmp + 2, 0);
    wr16(icmp + 2, pkt_csum16(icmp, total - IPV4_HDR_LEN));
    return 0;
}

int pkt_icmp_error_allowed(const uint8_t *frame, size_t len) {
    if (len < ETH_HDR_LEN + IPV4_HDR_LEN)
        return 0;
    const uint8_t *ip = frame + ETH_HDR_LEN;
    uint32_t src = rd32(ip + 12);
    if (src == 0 || (src >> 24) == 127 || (src >> 24) >= 224)
        return 0;
    if (ip[9] == IPPROTO_ICMP_ && len >= ETH_HDR_LEN + IPV4_HDR_LEN + 1) {
        uint8_t t = ip[IPV4_HDR_LEN];
        if (t == 3 || t == 4 || t == 5 || t == 11 || t == 12)
            return 0; /* never answer an ICMP error with an ICMP error */
    }
    return 1;
}

size_t pkt_icmp_build_error(uint8_t *out, const uint8_t *orig, size_t orig_len,
                            const uint8_t eth_src[ETH_ALEN], uint32_t src_ip, uint8_t type,
                            uint8_t code, uint16_t mtu) {
    size_t quote = IPV4_HDR_LEN + 8;
    size_t have = orig_len - ETH_HDR_LEN;
    if (quote > have)
        quote = have;
    size_t icmp_len = ICMP_HDR_LEN + quote;
    size_t total = IPV4_HDR_LEN + icmp_len;

    memcpy(out, orig + 6, ETH_ALEN); /* back to the sender */
    memcpy(out + 6, eth_src, ETH_ALEN);
    wr16(out + 12, ETHERTYPE_IPV4);

    uint8_t *ip = out + ETH_HDR_LEN;
    ip[0] = 0x45;
    ip[1] = 0;
    wr16(ip + 2, (uint16_t)total);
    wr16(ip + 4, 0);
    wr16(ip + 6, 0);
    ip[8] = 64;
    ip[9] = IPPROTO_ICMP_;
    wr32(ip + 12, src_ip);
    memcpy(ip + 16, orig + ETH_HDR_LEN + 12, 4); /* original source */
    pkt_ipv4_set_csum(ip);

    uint8_t *icmp = ip + IPV4_HDR_LEN;
    icmp[0] = type;
    icmp[1] = code;
    wr16(icmp + 2, 0);
    wr16(icmp + 4, 0);
    wr16(icmp + 6, (type == ICMP_UNREACH && code == ICMP_UNREACH_FRAG_NEEDED) ? mtu : 0);
    memcpy(icmp + ICMP_HDR_LEN, orig + ETH_HDR_LEN, quote);
    wr16(icmp + 2, pkt_csum16(icmp, icmp_len));
    return ETH_HDR_LEN + total;
}
