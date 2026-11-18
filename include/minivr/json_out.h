#ifndef MINIVR_JSON_OUT_H
#define MINIVR_JSON_OUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JW_MAX_DEPTH 16

/* JSON writer into a caller-supplied buffer. On overflow it stops writing and
 * sets `overflow`; it never truncates silently. `key` is NULL inside arrays. */
struct jw {
    char *buf;
    size_t cap, len;
    bool overflow;
    int depth;
    bool has_item[JW_MAX_DEPTH + 1];
};

void jw_init(struct jw *w, char *buf, size_t cap);
void jw_reset(struct jw *w);
void jw_obj_begin(struct jw *w, const char *key);
void jw_obj_end(struct jw *w);
void jw_arr_begin(struct jw *w, const char *key);
void jw_arr_end(struct jw *w);
void jw_str(struct jw *w, const char *key, const char *val);
void jw_u64(struct jw *w, const char *key, uint64_t val);
void jw_bool(struct jw *w, const char *key, bool val);
void jw_null(struct jw *w, const char *key);
/* 0 if complete and balanced, -1 on overflow or unbalanced nesting. */
int jw_finish(const struct jw *w);
#endif
