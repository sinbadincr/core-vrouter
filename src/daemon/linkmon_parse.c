#include "linkmon_parse.h"

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdint.h>
#include <string.h>

/* Interface name of one link message, or NULL; `name` must hold IFNAMSIZ + 1 bytes. */
static const char *link_name(const uint8_t *attrs, size_t alen, char *name) {
    while (alen >= sizeof(struct rtattr)) {
        struct rtattr a;
        memcpy(&a, attrs, sizeof a);
        if (a.rta_len < sizeof a || a.rta_len > alen)
            return NULL; /* attribute longer than what is left, or too short to advance */
        if (a.rta_type == IFLA_IFNAME) {
            size_t n = a.rta_len - RTA_LENGTH(0);
            if (n > IFNAMSIZ)
                n = IFNAMSIZ;
            memcpy(name, attrs + RTA_LENGTH(0), n);
            name[n] = 0;
            return name;
        }
        size_t step = RTA_ALIGN(a.rta_len);
        if (step >= alen)
            return NULL;
        attrs += step;
        alen -= step;
    }
    return NULL;
}

int linkmon_parse(const void *buf, size_t len, linkmon_cb cb, void *arg) {
    const uint8_t *p = buf;
    int events = 0;
    while (len >= sizeof(struct nlmsghdr)) {
        struct nlmsghdr nh;
        memcpy(&nh, p, sizeof nh);
        if (nh.nlmsg_len < sizeof nh || nh.nlmsg_len > len)
            break; /* lies about its own length */
        if ((nh.nlmsg_type == RTM_NEWLINK || nh.nlmsg_type == RTM_DELLINK) &&
            nh.nlmsg_len >= NLMSG_LENGTH(sizeof(struct ifinfomsg))) {
            struct ifinfomsg ifi;
            memcpy(&ifi, p + NLMSG_HDRLEN, sizeof ifi);
            size_t off = NLMSG_LENGTH(sizeof ifi);
            char name[IFNAMSIZ + 1];
            if (link_name(p + off, nh.nlmsg_len - off, name)) {
                cb(name, nh.nlmsg_type == RTM_DELLINK ? 0 : ifi.ifi_flags, arg);
                events++;
            }
        }
        size_t step = NLMSG_ALIGN(nh.nlmsg_len);
        if (step >= len)
            break;
        p += step;
        len -= step;
    }
    return events;
}
