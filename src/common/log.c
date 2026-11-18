#include "minivr/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "minivr/clock.h"

static enum log_level min_level = LOG_L_INFO;
static const char *const names[] = {"debug", "info", "warn", "error"};

void log_set_level(enum log_level lvl) {
    min_level = lvl;
}

void log_write(enum log_level lvl, const char *comp, const char *msg, const char *fmt, ...) {
    if (lvl < min_level)
        return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    gmtime_r(&ts.tv_sec, &tm);
    char line[512];
    int n = snprintf(line, sizeof line,
                     "ts=%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ level=%s comp=%s msg=\"%s\"",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                     tm.tm_sec, ts.tv_nsec / 1000000, names[lvl], comp, msg);
    if (n < 0 || (size_t)n >= sizeof line)
        n = (int)sizeof line - 1;
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    size_t len = (size_t)n + (m > 0 ? (size_t)m : 0);
    if (len > sizeof line - 2)
        len = sizeof line - 2;
    while (len > 0 && line[len - 1] == ' ')
        len--;
    line[len++] = '\n';
    fwrite(line, 1, len, stderr);
}

#define RL_RATE 10
#define RL_BURST 10

int log_rl_allow(struct log_rl *rl) {
    uint64_t now = clock_now_ns();
    if (!rl->init) {
        rl->init = 1;
        rl->tokens = (uint64_t)RL_BURST * NS_PER_SEC;
        rl->last = now;
    }
    uint64_t add = (now - rl->last) * RL_RATE;
    rl->last = now;
    rl->tokens += add;
    if (rl->tokens > (uint64_t)RL_BURST * NS_PER_SEC)
        rl->tokens = (uint64_t)RL_BURST * NS_PER_SEC;
    if (rl->tokens < NS_PER_SEC)
        return 0;
    rl->tokens -= NS_PER_SEC;
    return 1;
}
