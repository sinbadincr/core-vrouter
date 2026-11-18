#include "minivr/ratelimit.h"

#include "minivr/clock.h"
#include "minivr/port.h"

static uint32_t cfg_rate = RL_DEFAULT_RATE, cfg_burst = RL_DEFAULT_BURST;

void tb_init(struct token_bucket *tb, uint32_t rate, uint32_t burst, uint64_t now) {
    tb->rate = rate;
    tb->burst = burst;
    tb->tokens = (uint64_t)burst * NS_PER_SEC;
    tb->last = now;
}

bool tb_take(struct token_bucket *tb, uint64_t now) {
    uint64_t cap = (uint64_t)tb->burst * NS_PER_SEC;
    if (now > tb->last) {
        /* rate tokens/s == rate/1e9 tokens per ns; tokens are scaled by 1e9 */
        uint64_t add = (now - tb->last) * tb->rate;
        tb->tokens = (tb->tokens + add > cap || tb->tokens + add < tb->tokens) ? cap : tb->tokens + add;
        tb->last = now;
    }
    if (tb->tokens < NS_PER_SEC)
        return false;
    tb->tokens -= NS_PER_SEC;
    return true;
}

const char *ratelimit_configure(uint64_t rate, uint64_t burst) {
    if (rate < 1 || rate > RL_MAX_RATE || burst < 1 || burst > RL_MAX_BURST)
        return "bad-syntax";
    cfg_rate = (uint32_t)rate;
    cfg_burst = (uint32_t)burst;
    uint64_t now = clock_now_ns();
    for (unsigned i = 0; i < MAX_PORTS; i++) {
        struct port *p = port_get((uint8_t)i);
        if (p)
            tb_init(&p->icmp_bucket, cfg_rate, cfg_burst, now);
    }
    return NULL;
}

uint32_t ratelimit_rate(void) {
    return cfg_rate;
}

uint32_t ratelimit_burst(void) {
    return cfg_burst;
}
