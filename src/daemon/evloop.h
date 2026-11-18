#ifndef MINIVR_EVLOOP_H
#define MINIVR_EVLOOP_H
#include <stdint.h>

/* Thin epoll wrapper: every fd is an evsrc with a callback. */
struct evsrc {
    int fd;
    void (*cb)(struct evsrc *s, uint32_t events);
    void *arg;
};

int ev_init(void);
void ev_fini(void);
int ev_add(struct evsrc *s, uint32_t events); /* EPOLLIN etc.; 0 on success */
int ev_mod(struct evsrc *s, uint32_t events);
void ev_del(struct evsrc *s); /* safe to call from inside a callback */
int ev_wait(int timeout_ms); /* dispatch one batch; <0 on error (not EINTR) */
#endif
