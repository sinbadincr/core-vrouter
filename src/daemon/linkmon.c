#include "linkmon.h"

#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "evloop.h"
#include "minivr/log.h"
#include "minivr/neigh.h"
#include "minivr/port.h"

static struct evsrc src = {.fd = -1};

static void apply(const char *ifname, unsigned flags) {
    for (unsigned i = 0; i < MAX_PORTS; i++) {
        struct port *p = port_get((uint8_t)i);
        if (!p || strcmp(p->ifname, ifname) != 0)
            continue;
        bool up = (flags & IFF_RUNNING) != 0;
        if (up == p->oper_up)
            return;
        p->oper_up = up;
        log_info("linkmon", "port oper state changed", "port=%s ifname=%s oper=%s", p->name,
                 ifname, up ? "up" : "down");
        if (!up)
            neigh_port_down(p->id);
        return;
    }
}

static void handle_msg(const struct nlmsghdr *nh) {
    if (nh->nlmsg_type != RTM_NEWLINK && nh->nlmsg_type != RTM_DELLINK)
        return;
    if (nh->nlmsg_len < NLMSG_LENGTH(sizeof(struct ifinfomsg)))
        return;
    const struct ifinfomsg *ifi = NLMSG_DATA(nh);
    unsigned flags = nh->nlmsg_type == RTM_DELLINK ? 0 : ifi->ifi_flags;
    size_t alen = nh->nlmsg_len - NLMSG_LENGTH(sizeof *ifi);
    for (const struct rtattr *a = IFLA_RTA(ifi); RTA_OK(a, alen); a = RTA_NEXT(a, alen)) {
        if (a->rta_type != IFLA_IFNAME)
            continue;
        char name[IFNAMSIZ + 1];
        size_t n = RTA_PAYLOAD(a);
        if (n > IFNAMSIZ)
            n = IFNAMSIZ;
        memcpy(name, RTA_DATA(a), n);
        name[n] = 0;
        apply(name, flags);
        return;
    }
}

static void on_readable(struct evsrc *s, uint32_t ev) {
    (void)ev;
    char buf[16384];
    ssize_t n = recv(s->fd, buf, sizeof buf, MSG_DONTWAIT);
    if (n < 0) {
        if (errno == ENOBUFS)
            log_warn("linkmon", "netlink overrun; link state may be stale");
        return;
    }
    size_t len = (size_t)n;
    for (const struct nlmsghdr *nh = (const struct nlmsghdr *)buf; NLMSG_OK(nh, len);
         nh = NLMSG_NEXT(nh, len))
        handle_msg(nh);
}

int linkmon_init(void) {
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    if (fd < 0) {
        log_error("linkmon", "cannot open rtnetlink", "errno=%d", errno);
        return -1;
    }
    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof sa);
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = RTMGRP_LINK;
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        log_error("linkmon", "cannot subscribe to link events", "errno=%d", errno);
        close(fd);
        return -1;
    }
    /* initial state: ask for a dump of all links; the replies arrive through the loop */
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
    } req;
    memset(&req, 0, sizeof req);
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof req.ifi);
    req.nh.nlmsg_type = RTM_GETLINK;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nh.nlmsg_seq = 1;
    req.ifi.ifi_family = AF_UNSPEC;
    if (send(fd, &req, req.nh.nlmsg_len, 0) < 0)
        log_warn("linkmon", "link dump request failed", "errno=%d", errno);
    src.fd = fd;
    src.cb = on_readable;
    if (ev_add(&src, EPOLLIN) < 0) {
        close(fd);
        return -1;
    }
    return 0;
}

void linkmon_close(void) {
    if (src.fd >= 0) {
        ev_del(&src);
        close(src.fd);
        src.fd = -1;
    }
}
