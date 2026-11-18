#ifndef MINIVR_PRIVDROP_H
#define MINIVR_PRIVDROP_H

#define MINIVR_DUMP_DIR "/var/tmp/minivr"

/* Prepare MINIVR_DUMP_DIR, drop to `user` with zero capabilities and no_new_privs, then
 * self-verify from /proc/self/status. Returns 0, or -1 (message logged) - caller must exit. */
int privdrop(const char *user);

/* Self-check of the text of /proc/self/status: 0 if the process runs as `want_uid` (non-zero) in
 * all four uid fields with empty effective/permitted/ambient capability sets and no_new_privs,
 * otherwise a bit mask of what is wrong. A missing field counts as wrong. */
#define PRIVCHK_UID 1
#define PRIVCHK_CAPEFF 2
#define PRIVCHK_CAPPRM 4
#define PRIVCHK_CAPAMB 8
#define PRIVCHK_NNP 16
int privdrop_check_status(const char *status_text, unsigned want_uid);
#endif
