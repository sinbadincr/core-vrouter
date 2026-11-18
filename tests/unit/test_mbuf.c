#include <stdint.h>

#include "minivr/mbuf.h"
#include "tharness.h"

TEST(pool_exhaustion_and_free) {
    CHECK(mbuf_pool_init(MBUF_POOL_SIZE) == 0);
    CHECK_EQ_U64(mbuf_pool_total(), MBUF_POOL_SIZE);
    static struct mbuf *all[MBUF_POOL_SIZE];
    for (size_t i = 0; i < MBUF_POOL_SIZE; i++) {
        all[i] = mbuf_alloc();
        CHECK(all[i] != NULL);
        CHECK_EQ_U64(mbuf_in_use(), i + 1);
    }
    CHECK(mbuf_alloc() == NULL);
    CHECK_EQ_U64(mbuf_in_use(), MBUF_POOL_SIZE);
    mbuf_free(all[7]);
    CHECK_EQ_U64(mbuf_in_use(), MBUF_POOL_SIZE - 1);
    struct mbuf *again = mbuf_alloc(); /* LIFO: the buffer just freed */
    CHECK(again == all[7]);
    for (size_t i = 0; i < MBUF_POOL_SIZE; i++)
        mbuf_free(all[i]);
    CHECK_EQ_U64(mbuf_in_use(), 0);
    mbuf_pool_fini();
}

TEST(layout) {
    CHECK(mbuf_pool_init(4) == 0);
    struct mbuf *m = mbuf_alloc();
    CHECK(m != NULL);
    CHECK_EQ_U64((uintptr_t)m % 64, 0);
    CHECK_EQ_U64(sizeof(m->buf), 2048);
    CHECK_EQ_U64(m->off, 128);
    CHECK_EQ_U64(m->len, 0);
    CHECK(mbuf_data(m) == m->buf + 128);
    CHECK_EQ_U64(mbuf_tailroom(m), 2048 - 128);
    CHECK(m->next == NULL);
    mbuf_free(m);
    mbuf_pool_fini();
}
