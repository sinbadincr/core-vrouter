#include <string.h>

#include "minivr/frame.h"
#include "tharness.h"

TEST(encode_big_endian_length) {
    uint8_t out[16];
    int n = frame_encode("abc", 3, out, sizeof out);
    CHECK_EQ_U64(n, 7);
    const uint8_t want[] = {0, 0, 0, 3, 'a', 'b', 'c'};
    CHECK_MEM_EQ(out, want, 7);
}

TEST(encode_rejects_oversize_and_small_buffer) {
    static uint8_t big[FRAME_HDR + FRAME_MAX + 8];
    CHECK(frame_encode("x", FRAME_MAX + 1, big, sizeof big) == -1);
    uint8_t tiny[4];
    CHECK(frame_encode("abc", 3, tiny, sizeof tiny) == -1);
    CHECK(frame_encode("", 0, tiny, sizeof tiny) == 4);
}

TEST(decode_whole_frame) {
    static struct frame_rx rx;
    frame_rx_init(&rx);
    uint8_t in[16];
    int n = frame_encode("route show", 10, in, sizeof in);
    CHECK_EQ_U64(frame_rx_feed(&rx, in, (size_t)n), (size_t)n);
    const uint8_t *p;
    size_t len;
    CHECK(frame_rx_get(&rx, &p, &len) == 1);
    CHECK_EQ_U64(len, 10);
    CHECK_MEM_EQ(p, "route show", 10);
}

TEST(partial_reads_reassemble) {
    static struct frame_rx rx;
    frame_rx_init(&rx);
    uint8_t in[32];
    int n = frame_encode("hello world", 11, in, sizeof in);
    const uint8_t *p;
    size_t len;
    for (int i = 0; i < n; i++) {
        CHECK(frame_rx_get(&rx, &p, &len) == 0);
        CHECK_EQ_U64(frame_rx_feed(&rx, in + i, 1), 1);
    }
    CHECK(frame_rx_get(&rx, &p, &len) == 1);
    CHECK_EQ_U64(len, 11);
    CHECK_MEM_EQ(p, "hello world", 11);
}

TEST(feed_stops_at_frame_end) {
    static struct frame_rx rx;
    frame_rx_init(&rx);
    uint8_t in[32];
    int n = frame_encode("ab", 2, in, sizeof in);
    int m = frame_encode("cd", 2, in + n, sizeof in - (size_t)n);
    CHECK_EQ_U64(frame_rx_feed(&rx, in, (size_t)(n + m)), (size_t)n); /* leaves the 2nd frame */
}

TEST(oversize_length_rejected) {
    static struct frame_rx rx;
    frame_rx_init(&rx);
    const uint8_t hdr[] = {0x00, 0x01, 0x00, 0x01}; /* 65,537 */
    frame_rx_feed(&rx, hdr, 4);
    const uint8_t *p;
    size_t len;
    CHECK(frame_rx_get(&rx, &p, &len) == -1);
    frame_rx_init(&rx);
    const uint8_t ok[] = {0x00, 0x01, 0x00, 0x00}; /* exactly 65,536: allowed, incomplete */
    frame_rx_feed(&rx, ok, 4);
    CHECK(frame_rx_get(&rx, &p, &len) == 0);
}
