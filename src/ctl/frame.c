#include "minivr/frame.h"

#include <string.h>

int frame_encode(const void *payload, size_t len, uint8_t *out, size_t cap) {
    if (len > FRAME_MAX || cap < FRAME_HDR + len)
        return -1;
    out[0] = (uint8_t)(len >> 24);
    out[1] = (uint8_t)(len >> 16);
    out[2] = (uint8_t)(len >> 8);
    out[3] = (uint8_t)len;
    if (len)
        memcpy(out + FRAME_HDR, payload, len);
    return (int)(FRAME_HDR + len);
}

void frame_rx_init(struct frame_rx *rx) {
    rx->have = 0;
}

static size_t declared_len(const struct frame_rx *rx) {
    return ((size_t)rx->buf[0] << 24) | ((size_t)rx->buf[1] << 16) | ((size_t)rx->buf[2] << 8) |
           rx->buf[3];
}

size_t frame_rx_feed(struct frame_rx *rx, const void *data, size_t n) {
    const uint8_t *p = data;
    size_t used = 0;
    while (used < n) {
        size_t want;
        if (rx->have < FRAME_HDR) {
            want = FRAME_HDR - rx->have;
        } else {
            size_t len = declared_len(rx);
            if (len > FRAME_MAX)
                break;
            want = FRAME_HDR + len - rx->have;
        }
        if (want == 0)
            break;
        if (want > n - used)
            want = n - used;
        memcpy(rx->buf + rx->have, p + used, want);
        rx->have += want;
        used += want;
    }
    return used;
}

int frame_rx_get(const struct frame_rx *rx, const uint8_t **payload, size_t *len) {
    if (rx->have < FRAME_HDR)
        return 0;
    size_t l = declared_len(rx);
    if (l > FRAME_MAX)
        return -1;
    if (rx->have < FRAME_HDR + l)
        return 0;
    *payload = rx->buf + FRAME_HDR;
    *len = l;
    return 1;
}
