#include "minivr/clock.h"

#include <stdbool.h>
#include <time.h>

static bool fake_on;
static uint64_t fake_ns;

uint64_t clock_now_ns(void) {
    if (fake_on)
        return fake_ns;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * NS_PER_SEC + (uint64_t)ts.tv_nsec;
}

void clock_set_fake(uint64_t ns) {
    fake_on = true;
    fake_ns = ns;
}

void clock_advance(uint64_t ns) {
    if (fake_on)
        fake_ns += ns;
}

void clock_use_real(void) {
    fake_on = false;
}
