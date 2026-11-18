#ifndef MINIVR_CLOCK_H
#define MINIVR_CLOCK_H
#include <stdint.h>

/* Monotonic time in ns. Tests may switch to a fake clock. */
uint64_t clock_now_ns(void);
void clock_set_fake(uint64_t ns); /* enable fake clock at the given time */
void clock_advance(uint64_t ns);  /* advance the fake clock (no-op on the real clock) */
void clock_use_real(void);

#define NS_PER_MS 1000000ULL
#define NS_PER_SEC 1000000000ULL
#endif
