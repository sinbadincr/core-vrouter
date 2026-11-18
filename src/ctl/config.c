#include "minivr/config.h"

#include <stdio.h>
#include <string.h>

#include "minivr/cmd.h"

int config_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }
    char line[1024];
    char resp[512];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        if (!strchr(line, '\n') && !feof(f)) {
            fprintf(stderr, "%s:%d: bad-syntax: line too long\n", path, lineno);
            fclose(f);
            return -1;
        }
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' ||
                         line[n - 1] == '\t'))
            line[--n] = 0;
        char *s = line;
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            continue;
        struct jw w;
        struct cmd_err err;
        jw_init(&w, resp, sizeof resp);
        if (cmd_exec(s, CMD_CONFIG, &w, &err) != 0) {
            fprintf(stderr, "%s:%d: %s: %s\n", path, lineno, err.code, err.message);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}
