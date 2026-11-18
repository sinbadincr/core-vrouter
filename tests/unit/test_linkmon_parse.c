#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>

#include "linkmon_parse.h"
#include "tharness.h"

struct ev {
    char name[IFNAMSIZ + 1];
    unsigned flags;
};
struct sink {
    struct ev e[8];
    int n;
};

static void collect(const char *ifname, unsigned flags, void *arg) {
    struct sink *s = arg;
    if (s->n < 8) {
        snprintf(s->e[s->n].name, sizeof s->e[s->n].name, "%s", ifname);
        s->e[s->n].flags = flags;
    }
    s->n++;
}

/* One link message with an IFLA_IFNAME attribute (name may be "" to omit it). */
static size_t msg(uint8_t *out, uint16_t type, const char *name, unsigned flags) {
    memset(out, 0, 256);
    struct nlmsghdr *nh = (struct nlmsghdr *)out;
    struct ifinfomsg *ifi = NLMSG_DATA(nh);
    nh->nlmsg_type = type;
    ifi->ifi_family = AF_UNSPEC;
    ifi->ifi_flags = flags;
    size_t len = NLMSG_LENGTH(sizeof *ifi);
    if (*name) {
        struct rtattr *a = (struct rtattr *)(out + len);
        a->rta_type = IFLA_IFNAME;
        a->rta_len = (unsigned short)RTA_LENGTH(strlen(name) + 1);
        memcpy(RTA_DATA(a), name, strlen(name) + 1);
        len += RTA_ALIGN(a->rta_len);
    }
    nh->nlmsg_len = (uint32_t)len;
    return len;
}

/* Run the parser over an exact-size heap copy so ASan sees any read past the end. */
static int run(const uint8_t *buf, size_t len, struct sink *s) {
    uint8_t *copy = malloc(len ? len : 1);
    memcpy(copy, buf, len);
    s->n = 0;
    int n = linkmon_parse(copy, len, collect, s);
    free(copy);
    return n;
}

TEST(valid_newlink_and_dellink) {
    uint8_t b[512];
    struct sink s;
    size_t n = msg(b, RTM_NEWLINK, "p3", IFF_UP | IFF_RUNNING);
    CHECK_EQ_U64(run(b, n, &s), 1);
    CHECK_STR_EQ(s.e[0].name, "p3");
    CHECK(s.e[0].flags & IFF_RUNNING);
    n = msg(b, RTM_NEWLINK, "p3", IFF_UP); /* carrier lost: no IFF_RUNNING */
    CHECK_EQ_U64(run(b, n, &s), 1);
    CHECK(!(s.e[0].flags & IFF_RUNNING));
    n = msg(b, RTM_DELLINK, "p3", IFF_UP | IFF_RUNNING);
    CHECK_EQ_U64(run(b, n, &s), 1);
    CHECK_EQ_U64(s.e[0].flags, 0);
}

TEST(several_messages_in_one_buffer) {
    uint8_t b[1024], one[256];
    size_t off = 0;
    size_t n = msg(one, RTM_NEWLINK, "p1", IFF_RUNNING);
    memcpy(b + off, one, n);
    off += NLMSG_ALIGN(n);
    n = msg(one, RTM_GETLINK, "ignored", 0); /* other message types are skipped */
    memcpy(b + off, one, n);
    off += NLMSG_ALIGN(n);
    n = msg(one, RTM_NEWLINK, "p2", 0);
    memcpy(b + off, one, n);
    off += n;
    struct sink s;
    CHECK_EQ_U64(run(b, off, &s), 2);
    CHECK_STR_EQ(s.e[0].name, "p1");
    CHECK_STR_EQ(s.e[1].name, "p2");
}

TEST(every_truncation_is_safe_and_delivers_no_garbage) {
    uint8_t b[512];
    size_t n = msg(b, RTM_NEWLINK, "p3", IFF_RUNNING);
    struct sink s;
    for (size_t len = 0; len < n; len++) {
        int got = run(b, len, &s);
        CHECK_EQ_U64(got, 0); /* nlmsg_len now exceeds what is present */
    }
    CHECK_EQ_U64(run(b, n, &s), 1);
}

TEST(lying_lengths_end_the_walk) {
    uint8_t b[512];
    struct sink s;
    size_t n = msg(b, RTM_NEWLINK, "p3", IFF_RUNNING);
    ((struct nlmsghdr *)b)->nlmsg_len = 4; /* shorter than the header itself */
    CHECK_EQ_U64(run(b, n, &s), 0);
    ((struct nlmsghdr *)b)->nlmsg_len = (uint32_t)n + 100; /* longer than the buffer */
    CHECK_EQ_U64(run(b, n, &s), 0);
    ((struct nlmsghdr *)b)->nlmsg_len = (uint32_t)NLMSG_HDRLEN + 2; /* too short for ifinfomsg */
    CHECK_EQ_U64(run(b, n, &s), 0);
}

TEST(bad_attribute_lengths) {
    uint8_t b[512];
    struct sink s;
    size_t n = msg(b, RTM_NEWLINK, "p3", IFF_RUNNING);
    struct rtattr *a = (struct rtattr *)(b + NLMSG_LENGTH(sizeof(struct ifinfomsg)));
    a->rta_len = 0; /* would loop forever if trusted */
    CHECK_EQ_U64(run(b, n, &s), 0);
    a->rta_len = 3; /* shorter than an rtattr header */
    CHECK_EQ_U64(run(b, n, &s), 0);
    a->rta_len = 4000; /* longer than the message */
    CHECK_EQ_U64(run(b, n, &s), 0);
    a->rta_len = (unsigned short)RTA_LENGTH(3);
    a->rta_type = IFLA_ADDRESS; /* no IFLA_IFNAME at all */
    CHECK_EQ_U64(run(b, n, &s), 0);
}

TEST(overlong_and_unterminated_names_are_clipped) {
    uint8_t b[512];
    struct sink s;
    size_t n = msg(b, RTM_NEWLINK, "abcdefghijklmnopqrstuvwxyz", IFF_RUNNING);
    CHECK_EQ_U64(run(b, n, &s), 1);
    CHECK_EQ_U64(strlen(s.e[0].name), IFNAMSIZ); /* clipped, always NUL-terminated */
}
