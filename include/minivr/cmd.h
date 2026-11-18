#ifndef MINIVR_CMD_H
#define MINIVR_CMD_H
#include <stdint.h>

#include "json_out.h"

/* Command grammar shared by the config file and the control socket (contracts/cli.md). */

#define CMD_MAX_ARGS 16

struct cmd_err {
    const char *code; /* identifier from contracts/cli.md, e.g. "bad-prefix" */
    char message[160];
};

enum cmd_where { CMD_CONFIG = 1, CMD_CLI = 2, CMD_BOTH = 3 };

/* Split `line` in place on spaces/tabs. Returns argc, or -1 if more than `max` tokens. */
int cmd_tokenize(char *line, char **argv, int max);

/* Typed argument parsers: NULL on success, else the error code. IPs are host byte order. */
const char *cmd_parse_ipv4(const char *s, uint32_t *ip);                       /* bad-address */
const char *cmd_parse_prefix(const char *s, uint32_t *ip, uint8_t *len);       /* bad-prefix */
const char *cmd_parse_mac(const char *s, uint8_t mac[6]);                      /* bad-mac */
const char *cmd_parse_uint(const char *s, uint64_t min, uint64_t max, uint64_t *out); /* bad-syntax */

void cmd_init(void); /* records the start time for `version` uptime */

/* Parse and run one command line. Always writes the full response envelope to `out`
 * ({"ok":true,"data":{...}} or {"ok":false,"error":{...}}). Returns 0 on success,
 * -1 on error (err filled in). A failed command leaves state unchanged (FR-018). */
int cmd_exec(const char *line, enum cmd_where where, struct jw *out, struct cmd_err *err);

/* Helpers for handlers. */
int cmd_fail(struct cmd_err *err, const char *code, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
#endif
