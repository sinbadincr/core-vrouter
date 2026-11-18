#include "minivr/pkt.h"

uint16_t pkt_csum16(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint32_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)(p[0] << 8);
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

int pkt_ipv4_validate(const uint8_t *frame, size_t len, enum drop_reason *reason) {
    if (len < ETH_HDR_LEN + IPV4_HDR_LEN) {
        *reason = DROP_TRUNCATED;
        return -1;
    }
    const uint8_t *ip = frame + ETH_HDR_LEN;
    size_t avail = len - ETH_HDR_LEN;
    if ((ip[0] >> 4) != 4) {
        *reason = DROP_BAD_IP_VERSION;
        return -1;
    }
    size_t ihl = (size_t)(ip[0] & 0x0f) * 4;
    if (ihl < IPV4_HDR_LEN || ihl > avail) {
        *reason = DROP_BAD_IP_HDR_LEN;
        return -1;
    }
    if (ihl > IPV4_HDR_LEN) {
        *reason = DROP_IP_OPTIONS;
        return -1;
    }
    size_t total = rd16(ip + 2);
    if (total < ihl || total > avail) {
        *reason = DROP_BAD_IP_TOTAL_LEN;
        return -1;
    }
    if (pkt_csum16(ip, ihl) != 0) {
        *reason = DROP_BAD_IP_CHECKSUM;
        return -1;
    }
    if (rd16(ip + 6) & (IP_MF | IP_OFFMASK)) {
        *reason = DROP_IP_FRAGMENT;
        return -1;
    }
    return 0;
}

void pkt_ipv4_dec_ttl(uint8_t *ip) {
    /* RFC 1624: HC' = ~(~HC + ~m + m'), m = old (TTL, protocol) word */
    uint16_t old = rd16(ip + 8);
    ip[8]--;
    uint16_t neu = rd16(ip + 8);
    uint32_t sum = (uint16_t)~rd16(ip + 10);
    sum += (uint16_t)~old;
    sum += neu;
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    wr16(ip + 10, (uint16_t)~sum);
}

void pkt_ipv4_set_csum(uint8_t *ip) {
    wr16(ip + 10, 0);
    wr16(ip + 10, pkt_csum16(ip, IPV4_HDR_LEN));
}
