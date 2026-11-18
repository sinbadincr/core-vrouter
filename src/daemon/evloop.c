#include "evloop.h"

#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

#define EV_BATCH 64

static int epfd = -1;
static struct epoll_event batch[EV_BATCH];
static int batch_n;

int ev_init(void) {
    epfd = epoll_create1(EPOLL_CLOEXEC);
    return epfd < 0 ? -1 : 0;
}

void ev_fini(void) {
    if (epfd >= 0)
        close(epfd);
    epfd = -1;
}

int ev_add(struct evsrc *s, uint32_t events) {
    struct epoll_event e = {.events = events, .data.ptr = s};
    return epoll_ctl(epfd, EPOLL_CTL_ADD, s->fd, &e);
}

int ev_mod(struct evsrc *s, uint32_t events) {
    struct epoll_event e = {.events = events, .data.ptr = s};
    return epoll_ctl(epfd, EPOLL_CTL_MOD, s->fd, &e);
}

void ev_del(struct evsrc *s) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, s->fd, NULL);
    for (int i = 0; i < batch_n; i++)
        if (batch[i].data.ptr == s)
            batch[i].data.ptr = NULL; /* pending event for a removed source */
}

int ev_wait(int timeout_ms) {
    int n = epoll_wait(epfd, batch, EV_BATCH, timeout_ms);
    if (n < 0)
        return errno == EINTR ? 0 : -1;
    batch_n = n;
    for (int i = 0; i < n; i++) {
        struct evsrc *s = batch[i].data.ptr;
        if (s)
            s->cb(s, batch[i].events);
    }
    batch_n = 0;
    return n;
}
