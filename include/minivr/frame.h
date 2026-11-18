#ifndef MINIVR_FRAME_H
#define MINIVR_FRAME_H
#include <stddef.h>
#include <stdint.h>

#define FRAME_HDR 4
#define FRAME_MAX 65536

/* 4-byte big-endian length + payload. Returns bytes written, -1 if it does not fit. */
int frame_encode(const void *payload, size_t len, uint8_t *out, size_t cap);

struct frame_rx {
    uint8_t buf[FRAME_HDR + FRAME_MAX];
    size_t have;
};

void frame_rx_init(struct frame_rx *rx);
/* Append at most what the current frame still needs; returns bytes consumed. */
size_t frame_rx_feed(struct frame_rx *rx, const void *data, size_t n);
/* 1: complete (payload/len set), 0: need more, -1: declared length > FRAME_MAX. */
int frame_rx_get(const struct frame_rx *rx, const uint8_t **payload, size_t *len);
#endif
