#include "minivr/json_out.h"
#include "tharness.h"

TEST(object_array_scalars) {
    char b[256];
    struct jw w;
    jw_init(&w, b, sizeof b);
    jw_obj_begin(&w, NULL);
    jw_bool(&w, "ok", true);
    jw_u64(&w, "n", 18446744073709551615ULL);
    jw_null(&w, "x");
    jw_arr_begin(&w, "a");
    jw_u64(&w, NULL, 1);
    jw_str(&w, NULL, "two");
    jw_obj_begin(&w, NULL);
    jw_bool(&w, "f", false);
    jw_obj_end(&w);
    jw_arr_end(&w);
    jw_obj_end(&w);
    CHECK(jw_finish(&w) == 0);
    CHECK_STR_EQ(b, "{\"ok\":true,\"n\":18446744073709551615,\"x\":null,"
                    "\"a\":[1,\"two\",{\"f\":false}]}");
}

TEST(string_escaping) {
    char b[128];
    struct jw w;
    jw_init(&w, b, sizeof b);
    jw_obj_begin(&w, NULL);
    jw_str(&w, "s", "a\"b\\c\n\t\r\x01z");
    jw_obj_end(&w);
    CHECK(jw_finish(&w) == 0);
    CHECK_STR_EQ(b, "{\"s\":\"a\\\"b\\\\c\\n\\t\\r\\u0001z\"}");
}

TEST(overflow_is_reported) {
    char b[16];
    struct jw w;
    jw_init(&w, b, sizeof b);
    jw_obj_begin(&w, NULL);
    jw_str(&w, "key", "a value that is far too long for the buffer");
    jw_obj_end(&w);
    CHECK(w.overflow);
    CHECK(jw_finish(&w) == -1);
    CHECK(b[sizeof b - 1] == 0 || w.len < sizeof b); /* never past the buffer */
}

TEST(unbalanced_is_reported) {
    char b[64];
    struct jw w;
    jw_init(&w, b, sizeof b);
    jw_obj_begin(&w, NULL);
    CHECK(jw_finish(&w) == -1);
}

TEST(reset_reuses_buffer) {
    char b[64];
    struct jw w;
    jw_init(&w, b, sizeof b);
    jw_obj_begin(&w, NULL);
    jw_reset(&w);
    jw_obj_begin(&w, NULL);
    jw_obj_end(&w);
    CHECK(jw_finish(&w) == 0);
    CHECK_STR_EQ(b, "{}");
}
