#include "minivr/json_out.h"

#include <stdio.h>
#include <string.h>

void jw_init(struct jw *w, char *buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    jw_reset(w);
}

void jw_reset(struct jw *w) {
    w->len = 0;
    w->overflow = false;
    w->depth = 0;
    memset(w->has_item, 0, sizeof w->has_item);
    if (w->cap)
        w->buf[0] = 0;
}

static void put(struct jw *w, const char *s, size_t n) {
    if (w->overflow)
        return;
    if (w->len + n + 1 > w->cap) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = 0;
}

static void puts_(struct jw *w, const char *s) {
    put(w, s, strlen(s));
}

static void put_escaped(struct jw *w, const char *s) {
    put(w, "\"", 1);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"': puts_(w, "\\\""); break;
        case '\\': puts_(w, "\\\\"); break;
        case '\n': puts_(w, "\\n"); break;
        case '\r': puts_(w, "\\r"); break;
        case '\t': puts_(w, "\\t"); break;
        default:
            if (c < 0x20) {
                char esc[8];
                snprintf(esc, sizeof esc, "\\u%04x", c);
                puts_(w, esc);
            } else {
                put(w, s, 1);
            }
        }
    }
    put(w, "\"", 1);
}

/* comma handling and optional "key": */
static void item(struct jw *w, const char *key) {
    if (w->has_item[w->depth])
        put(w, ",", 1);
    w->has_item[w->depth] = true;
    if (key) {
        put_escaped(w, key);
        put(w, ":", 1);
    }
}

static void open(struct jw *w, const char *key, char c) {
    item(w, key);
    put(w, &c, 1);
    if (w->depth >= JW_MAX_DEPTH) {
        w->overflow = true;
        return;
    }
    w->depth++;
    w->has_item[w->depth] = false;
}

static void close_(struct jw *w, char c) {
    if (w->depth == 0) {
        w->overflow = true;
        return;
    }
    w->depth--;
    put(w, &c, 1);
}

void jw_obj_begin(struct jw *w, const char *key) { open(w, key, '{'); }
void jw_obj_end(struct jw *w) { close_(w, '}'); }
void jw_arr_begin(struct jw *w, const char *key) { open(w, key, '['); }
void jw_arr_end(struct jw *w) { close_(w, ']'); }

void jw_str(struct jw *w, const char *key, const char *val) {
    item(w, key);
    put_escaped(w, val);
}

void jw_u64(struct jw *w, const char *key, uint64_t val) {
    char tmp[24];
    snprintf(tmp, sizeof tmp, "%llu", (unsigned long long)val);
    item(w, key);
    puts_(w, tmp);
}

void jw_bool(struct jw *w, const char *key, bool val) {
    item(w, key);
    puts_(w, val ? "true" : "false");
}

void jw_null(struct jw *w, const char *key) {
    item(w, key);
    puts_(w, "null");
}

int jw_finish(const struct jw *w) {
    return (w->overflow || w->depth != 0) ? -1 : 0;
}
