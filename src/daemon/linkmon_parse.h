#ifndef MINIVR_LINKMON_PARSE_H
#define MINIVR_LINKMON_PARSE_H
#include <stddef.h>

/* Called once per RTM_NEWLINK / RTM_DELLINK carrying an interface name. A deleted link is
 * reported with flags == 0 (not running). */
typedef void (*linkmon_cb)(const char *ifname, unsigned flags, void *arg);

/* Walk a buffer received from an rtnetlink socket. Every length field is checked against the
 * bytes actually present; a malformed message ends the walk (earlier events were delivered).
 * Returns the number of link events delivered. */
int linkmon_parse(const void *buf, size_t len, linkmon_cb cb, void *arg);
#endif
