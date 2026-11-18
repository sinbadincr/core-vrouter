#ifndef MINIVR_PRIVDROP_H
#define MINIVR_PRIVDROP_H

#define MINIVR_DUMP_DIR "/var/tmp/minivr"

/* Prepare MINIVR_DUMP_DIR, drop to `user` with zero capabilities and no_new_privs, then
 * self-verify from /proc/self/status. Returns 0, or -1 (message logged) - caller must exit. */
int privdrop(const char *user);
#endif
