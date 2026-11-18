#ifndef MINIVR_LPM_H
#define MINIVR_LPM_H
#include <stdint.h>

#define LPM_MAX_RULES 10000
#define LPM_MAX_NODES (LPM_MAX_RULES * 32 + 2)

struct lpm_node {
    uint32_t child[2]; /* 0 = none */
    uint32_t value;
    uint8_t has_value;
};

/* Binary trie over a pre-allocated node pool (no allocation after init). */
struct lpm {
    struct lpm_node nodes[LPM_MAX_NODES];
    uint32_t free_head; /* free list through child[0] */
    uint32_t next_unused;
    uint32_t rules;
};

typedef void (*lpm_walk_fn)(uint32_t prefix, uint8_t len, uint32_t value, void *arg);

void lpm_init(struct lpm *t);
int lpm_add(struct lpm *t, uint32_t prefix, uint8_t len, uint32_t value); /* 0, -1 exists, -2 full */
int lpm_del(struct lpm *t, uint32_t prefix, uint8_t len);                  /* 0, -1 not found */
int lpm_get(const struct lpm *t, uint32_t prefix, uint8_t len, uint32_t *value); /* exact, 1/0 */
int lpm_lookup(const struct lpm *t, uint32_t ip, uint32_t *value, uint8_t *len); /* 1 hit, 0 miss */
void lpm_walk(const struct lpm *t, lpm_walk_fn fn, void *arg); /* prefix order, shorter first */
uint32_t lpm_count(const struct lpm *t);
#endif
