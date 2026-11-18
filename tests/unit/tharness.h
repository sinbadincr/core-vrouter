/* Minimal unit-test harness: TEST(name) registers a case; each test_*.c is one CTest test. */
#ifndef THARNESS_H
#define THARNESS_H
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef void (*th_fn)(void);
struct th_case {
    const char *name;
    th_fn fn;
};
#define TH_MAX_CASES 256
static struct th_case th_cases[TH_MAX_CASES];
static int th_ncases;
static int th_failures;
static int th_cur_failed;
static const char *th_cur_name;

static inline void th_register(const char *name, th_fn fn) {
    if (th_ncases < TH_MAX_CASES) {
        th_cases[th_ncases].name = name;
        th_cases[th_ncases].fn = fn;
        th_ncases++;
    }
}

#define TEST(name)                                                                                 \
    static void name(void);                                                                        \
    __attribute__((constructor)) static void th_reg_##name(void) {                                 \
        th_register(#name, name);                                                                  \
    }                                                                                              \
    static void name(void)

#define TH_FAIL(...)                                                                               \
    do {                                                                                           \
        th_cur_failed = 1;                                                                         \
        fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);                                     \
        fprintf(stderr, __VA_ARGS__);                                                              \
        fputc('\n', stderr);                                                                       \
    } while (0)

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond))                                                                               \
            TH_FAIL("CHECK(%s)", #cond);                                                           \
    } while (0)

#define CHECK_EQ_U64(a, b)                                                                         \
    do {                                                                                           \
        uint64_t th_a = (uint64_t)(a), th_b = (uint64_t)(b);                                       \
        if (th_a != th_b)                                                                          \
            TH_FAIL("%s == %s: %" PRIu64 " != %" PRIu64, #a, #b, th_a, th_b);                      \
    } while (0)

#define CHECK_STR_EQ(a, b)                                                                         \
    do {                                                                                           \
        const char *th_sa = (a), *th_sb = (b);                                                     \
        if (!th_sa || !th_sb || strcmp(th_sa, th_sb) != 0)                                         \
            TH_FAIL("%s == %s: \"%s\" != \"%s\"", #a, #b, th_sa ? th_sa : "(null)",                \
                    th_sb ? th_sb : "(null)");                                                     \
    } while (0)

#define CHECK_MEM_EQ(a, b, n)                                                                      \
    do {                                                                                           \
        if (memcmp((a), (b), (n)) != 0)                                                            \
            TH_FAIL("memory differs: %s vs %s (%zu bytes)", #a, #b, (size_t)(n));                  \
    } while (0)

int main(void) {
    for (int i = 0; i < th_ncases; i++) {
        th_cur_name = th_cases[i].name;
        th_cur_failed = 0;
        th_cases[i].fn();
        printf("%s %s\n", th_cur_failed ? "FAIL" : "ok  ", th_cur_name);
        th_failures += th_cur_failed;
    }
    printf("%d/%d passed\n", th_ncases - th_failures, th_ncases);
    return th_failures ? 1 : 0;
}
#endif
