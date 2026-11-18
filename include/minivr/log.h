#ifndef MINIVR_LOG_H
#define MINIVR_LOG_H
#include <stdint.h>

enum log_level { LOG_L_DEBUG, LOG_L_INFO, LOG_L_WARN, LOG_L_ERROR };

struct log_rl {
    uint64_t tokens; /* scaled x1e9 */
    uint64_t last;
    int init;
};

void log_set_level(enum log_level lvl);
/* Line: ts=<RFC3339.ms> level=<lvl> comp=<c> msg="..." <fmt as k=v ...> to stderr. */
void log_write(enum log_level lvl, const char *comp, const char *msg, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
int log_rl_allow(struct log_rl *rl); /* 10 lines/s, burst 10 */

/* Optional trailing arguments: a printf format (starting with k=v) and its values. */
#define log_debug(c, m, ...) log_write(LOG_L_DEBUG, c, m, " " __VA_ARGS__)
#define log_info(c, m, ...) log_write(LOG_L_INFO, c, m, " " __VA_ARGS__)
#define log_warn(c, m, ...) log_write(LOG_L_WARN, c, m, " " __VA_ARGS__)
#define log_error(c, m, ...) log_write(LOG_L_ERROR, c, m, " " __VA_ARGS__)

/* Per-call-site rate-limited logging; the only logging allowed on the packet path. */
#define LOG_RL(lvl, c, m, ...)                                                                     \
    do {                                                                                           \
        static struct log_rl rl_site_;                                                             \
        if (log_rl_allow(&rl_site_))                                                               \
            log_write(lvl, c, m, " " __VA_ARGS__);                                                 \
    } while (0)
#endif
