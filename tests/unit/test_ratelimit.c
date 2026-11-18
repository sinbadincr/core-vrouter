#include "fixture.h"
#include "tharness.h"

TEST(burst_then_refill) {
    struct token_bucket tb;
    uint64_t now = 5 * NS_PER_SEC;
    tb_init(&tb, 100, 20, now);
    for (int i = 0; i < 20; i++)
        CHECK(tb_take(&tb, now));
    CHECK(!tb_take(&tb, now)); /* burst exhausted */
    now += 9 * NS_PER_MS;
    CHECK(!tb_take(&tb, now)); /* 0.9 token */
    now += 1 * NS_PER_MS;
    CHECK(tb_take(&tb, now)); /* 1 token per 10 ms */
    CHECK(!tb_take(&tb, now));
    now += 10 * NS_PER_MS;
    CHECK(tb_take(&tb, now));
}

TEST(refill_is_capped_at_burst) {
    struct token_bucket tb;
    uint64_t now = 0;
    tb_init(&tb, 100, 20, now);
    for (int i = 0; i < 20; i++)
        tb_take(&tb, now);
    now += 3600 * NS_PER_SEC; /* an hour idle */
    int got = 0;
    while (tb_take(&tb, now))
        got++;
    CHECK_EQ_U64(got, 20);
}

TEST(buckets_are_independent_per_port) {
    fx_setup(1);
    struct port *p1 = port_find("p1"), *p2 = port_find("p2");
    uint64_t now = clock_now_ns();
    for (int i = 0; i < 20; i++)
        CHECK(tb_take(&p1->icmp_bucket, now));
    CHECK(!tb_take(&p1->icmp_bucket, now));
    CHECK(tb_take(&p2->icmp_bucket, now));
    fx_teardown();
}

TEST(configuration_ranges) {
    fx_setup(1);
    CHECK(ratelimit_configure(1, 1) == NULL);
    CHECK(ratelimit_configure(100000, 10000) == NULL);
    CHECK(ratelimit_configure(0, 5) != NULL);
    CHECK(ratelimit_configure(100001, 5) != NULL);
    CHECK(ratelimit_configure(5, 0) != NULL);
    CHECK(ratelimit_configure(5, 10001) != NULL);
    CHECK_EQ_U64(ratelimit_rate(), 100000); /* failed calls change nothing */
    CHECK(ratelimit_configure(50, 7) == NULL);
    struct port *p = port_find("p1");
    CHECK_EQ_U64(p->icmp_bucket.rate, 50);
    CHECK_EQ_U64(p->icmp_bucket.burst, 7);
    char b[256];
    struct jw w;
    struct cmd_err e;
    jw_init(&w, b, sizeof b);
    CHECK(cmd_exec("icmp-ratelimit rate 0 burst 5", CMD_BOTH, &w, &e) == -1);
    jw_init(&w, b, sizeof b);
    CHECK(cmd_exec("icmp-ratelimit rate 100 burst 20", CMD_CLI, &w, &e) == 0);
    CHECK_EQ_U64(ratelimit_rate(), 100);
    fx_teardown();
}
