#include "minivr/pkt.h"

static const uint8_t bcast[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

enum eth_class pkt_eth_classify(const uint8_t *frame, size_t len, const uint8_t port_mac[ETH_ALEN],
                                enum drop_reason *reason) {
    if (len < ETH_HDR_LEN) {
        *reason = DROP_TRUNCATED;
        return ETH_C_DROP;
    }
    uint16_t et = rd16(frame + 12);
    if (memcmp(frame, bcast, ETH_ALEN) == 0) {
        if (et != ETHERTYPE_ARP) {
            *reason = DROP_NON_ARP_BCAST_MCAST;
            return ETH_C_DROP;
        }
    } else if (frame[0] & 1) { /* multicast */
        *reason = DROP_NON_ARP_BCAST_MCAST;
        return ETH_C_DROP;
    } else if (memcmp(frame, port_mac, ETH_ALEN) != 0) {
        *reason = DROP_NOT_FOR_US_L2;
        return ETH_C_DROP;
    }
    if (et == ETHERTYPE_IPV4)
        return ETH_C_IPV4;
    if (et == ETHERTYPE_ARP)
        return ETH_C_ARP;
    *reason = DROP_UNSUPPORTED_ETHERTYPE;
    return ETH_C_DROP;
}
