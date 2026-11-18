#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "minivr/clock.h"
#include "minivr/log.h"
#include "tharness.h"

/* Capture stderr into a buffer while f() runs. */
static size_t capture(void (*f)(void), char *buf, size_t cap) {
    fflush(stderr);
    int saved = dup(2), p[2];
    if (pipe(p) != 0)
        return 0;
    dup2(p[1], 2);
    f();
    fflush(stderr);
    dup2(saved, 2);
    close(p[1]);
    close(saved);
    ssize_t n = read(p[0], buf, cap - 1);
    close(p[0]);
    buf[n > 0 ? n : 0] = 0;
    return n > 0 ? (size_t)n : 0;
}

static void emit(void) {
    log_info("main", "forwarding started", "ports=%d sock=%s", 3, "/run/x.sock");
    log_warn("pio", "no extras");
}

TEST(line_format_is_structured) {
    char b[1024];
    log_set_level(LOG_L_INFO);
    CHECK(capture(emit, b, sizeof b) > 0);
    /* ts=2026-11-18T01:02:03.456Z level=info comp=main msg="forwarding started" ports=3 sock=... */
    CHECK(strncmp(b, "ts=", 3) == 0);
    CHECK(b[7] == '-' && b[10] == '-' && b[13] == 'T' && b[16] == ':' && b[19] == ':' && b[22] == '.');
    CHECK(strstr(b, "Z level=info comp=main msg=\"forwarding started\" ports=3 sock=/run/x.sock\n"));
    CHECK(strstr(b, "level=warn comp=pio msg=\"no extras\"\n")); /* no trailing blank */
}

static void emit_debug(void) {
    log_debug("main", "hidden");
}

TEST(level_filter) {
    char b[256];
    log_set_level(LOG_L_INFO);
    CHECK_EQ_U64(capture(emit_debug, b, sizeof b), 0);
    log_set_level(LOG_L_DEBUG);
    CHECK(capture(emit_debug, b, sizeof b) > 0);
    log_set_level(LOG_L_INFO);
}

static void spam(void) {
    for (int i = 0; i < 1000; i++)
        LOG_RL(LOG_L_WARN, "dp", "burst", "i=%d", i);
}

TEST(rate_limited_logging_is_throttled_per_call_site) {
    char b[65536];
    clock_set_fake(1000 * NS_PER_SEC);
    log_set_level(LOG_L_INFO);
    capture(spam, b, sizeof b);
    int lines = 0;
    for (char *p = b; (p = strchr(p, '\n')); p++)
        lines++;
    CHECK_EQ_U64(lines, 10); /* burst of 10, then silence until time passes */
    clock_advance(NS_PER_SEC);
    capture(spam, b, sizeof b);
    lines = 0;
    for (char *p = b; (p = strchr(p, '\n')); p++)
        lines++;
    CHECK_EQ_U64(lines, 10); /* 10 lines/s refill */
    clock_use_real();
}
