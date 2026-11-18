#ifndef MINIVR_RATELIMIT_H
#define MINIVR_RATELIMIT_H
#include <stdbool.h>
#include <stdint.h>

#define RL_DEFAULT_RATE 100
#define RL_DEFAULT_BURST 20
#define RL_MAX_RATE 100000
#define RL_MAX_BURST 10000

struct token_bucket {
    uint32_t rate;   /* tokens per second */
    uint32_t burst;  /* bucket depth */
    uint64_t tokens; /* scaled x1e9 */
    uint64_t last;   /* monotonic ns, lazy refill */
};

void tb_init(struct token_bucket *tb, uint32_t rate, uint32_t burst, uint64_t now);
bool tb_take(struct token_bucket *tb, uint64_t now);

/* Global ICMP limiter settings: validates (bad-syntax on range error), stores, and re-inits
 * every port's bucket. Returns NULL or an error code. */
const char *ratelimit_configure(uint64_t rate, uint64_t burst);
uint32_t ratelimit_rate(void);
uint32_t ratelimit_burst(void);
#endif
