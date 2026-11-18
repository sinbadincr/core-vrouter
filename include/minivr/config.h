#ifndef MINIVR_CONFIG_H
#define MINIVR_CONFIG_H
/* Load a config file (one command per line, '#' comments) through the cmd.c dispatch.
 * On error prints "file:line: <code>: <message>" to stderr and returns -1. */
int config_load(const char *path);
#endif
