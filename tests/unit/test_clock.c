#include "minivr/clock.h"
#include "tharness.h"

TEST(fake_clock_is_deterministic) {
    clock_set_fake(1000);
    CHECK_EQ_U64(clock_now_ns(), 1000);
    clock_advance(500);
    CHECK_EQ_U64(clock_now_ns(), 1500);
    CHECK_EQ_U64(clock_now_ns(), 1500);
    clock_use_real();
}

TEST(real_clock_is_monotonic) {
    clock_use_real();
    uint64_t prev = clock_now_ns();
    for (int i = 0; i < 1000; i++) {
        uint64_t now = clock_now_ns();
        CHECK(now >= prev);
        prev = now;
    }
    clock_advance(1000000); /* no effect on the real clock */
    CHECK(clock_now_ns() - prev < NS_PER_SEC);
}
