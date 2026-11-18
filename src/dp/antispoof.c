#include "minivr/antispoof.h"

#include "minivr/neigh.h"

bool antispoof_ok(const struct port *p, enum eth_class cls, const uint8_t *frame, size_t len) {
    uint32_t bip;
    uint8_t bmac[6];
    if (!neigh_binding(p->id, &bip, bmac))
        return true;
    if (len < ETH_HDR_LEN)
        return true;
    if (memcmp(frame + 6, bmac, ETH_ALEN) != 0)
        return false;
    if (cls == ETH_C_ARP && len >= ARP_FRAME_LEN) {
        const uint8_t *a = frame + ETH_HDR_LEN;
        return memcmp(a + 8, bmac, ETH_ALEN) == 0 && rd32(a + 14) == bip;
    }
    if (cls == ETH_C_IPV4 && len >= ETH_HDR_LEN + IPV4_HDR_LEN)
        return rd32(frame + ETH_HDR_LEN + 12) == bip;
    return true;
}
