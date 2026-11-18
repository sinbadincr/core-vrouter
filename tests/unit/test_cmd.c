#include <string.h>

#include "minivr/cmd.h"
#include "tharness.h"

TEST(tokenize_splits_on_spaces) {
    char line[] = "route  add 10.0.9.0/24\tdev p3";
    char *argv[CMD_MAX_ARGS];
    int n = cmd_tokenize(line, argv, CMD_MAX_ARGS);
    CHECK_EQ_U64(n, 5);
    CHECK_STR_EQ(argv[0], "route");
    CHECK_STR_EQ(argv[1], "add");
    CHECK_STR_EQ(argv[2], "10.0.9.0/24");
    CHECK_STR_EQ(argv[4], "p3");
    char empty[] = "   ";
    CHECK_EQ_U64(cmd_tokenize(empty, argv, CMD_MAX_ARGS), 0);
    char many[] = "a b c d";
    CHECK(cmd_tokenize(many, argv, 3) == -1);
}

TEST(parse_ipv4) {
    uint32_t ip = 0;
    CHECK(cmd_parse_ipv4("10.0.1.12", &ip) == NULL);
    CHECK_EQ_U64(ip, 0x0a00010cU);
    CHECK(cmd_parse_ipv4("255.255.255.255", &ip) == NULL);
    const char *bad[] = {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.-4", "a.b.c.d",
                         "1..2.3", "1.2.3.4 ", "01234.1.1.1", " 1.2.3.4", "1.2.3.4/24"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK_STR_EQ(cmd_parse_ipv4(bad[i], &ip), "bad-address");
}

TEST(parse_prefix) {
    uint32_t ip;
    uint8_t len;
    CHECK(cmd_parse_prefix("10.0.9.0/24", &ip, &len) == NULL);
    CHECK_EQ_U64(ip, 0x0a000900U);
    CHECK_EQ_U64(len, 24);
    CHECK(cmd_parse_prefix("0.0.0.0/0", &ip, &len) == NULL);
    CHECK(cmd_parse_prefix("1.2.3.4/32", &ip, &len) == NULL);
    CHECK_STR_EQ(cmd_parse_prefix("10.0.9.1/24", &ip, &len), "bad-prefix"); /* host bits */
    CHECK_STR_EQ(cmd_parse_prefix("10.0.9.0/33", &ip, &len), "bad-prefix");
    CHECK_STR_EQ(cmd_parse_prefix("10.0.9.0", &ip, &len), "bad-prefix");
    CHECK_STR_EQ(cmd_parse_prefix("10.0.9.0/", &ip, &len), "bad-prefix");
    CHECK_STR_EQ(cmd_parse_prefix("10.0.9.0/x", &ip, &len), "bad-prefix");
    CHECK_STR_EQ(cmd_parse_prefix("bogus/24", &ip, &len), "bad-prefix");
}

TEST(parse_mac) {
    uint8_t mac[6];
    CHECK(cmd_parse_mac("02:00:00:00:01:0a", mac) == NULL);
    const uint8_t want[6] = {2, 0, 0, 0, 1, 0x0a};
    CHECK_MEM_EQ(mac, want, 6);
    CHECK(cmd_parse_mac("FF:ff:Ff:fF:ff:ff", mac) == NULL);
    const char *bad[] = {"", "02:00:00:00:01", "02:00:00:00:01:0a:0b", "02-00-00-00-01-0a",
                         "02:00:00:00:01:0g", "2:0:0:0:1:a", "02:00:00:00:01:0a "};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK_STR_EQ(cmd_parse_mac(bad[i], mac), "bad-mac");
}

TEST(parse_uint_range) {
    uint64_t v;
    CHECK(cmd_parse_uint("100", 1, 100000, &v) == NULL);
    CHECK_EQ_U64(v, 100);
    CHECK_STR_EQ(cmd_parse_uint("0", 1, 100000, &v), "bad-syntax");
    CHECK_STR_EQ(cmd_parse_uint("100001", 1, 100000, &v), "bad-syntax");
    CHECK_STR_EQ(cmd_parse_uint("-1", 0, 10, &v), "bad-syntax");
    CHECK_STR_EQ(cmd_parse_uint("", 0, 10, &v), "bad-syntax");
    CHECK_STR_EQ(cmd_parse_uint("12x", 0, 100, &v), "bad-syntax");
    CHECK_STR_EQ(cmd_parse_uint("99999999999999999999999", 0, 100, &v), "bad-syntax");
}

static int run(const char *line, enum cmd_where where, char *buf, size_t cap, struct cmd_err *e) {
    struct jw w;
    jw_init(&w, buf, cap);
    memset(e, 0, sizeof *e);
    int rc = cmd_exec(line, where, &w, e);
    CHECK(jw_finish(&w) == 0);
    return rc;
}

TEST(unknown_command_is_bad_syntax) {
    char b[512];
    struct cmd_err e;
    CHECK(run("frobnicate now", CMD_CLI, b, sizeof b, &e) == -1);
    CHECK_STR_EQ(e.code, "bad-syntax");
    CHECK(strstr(b, "\"ok\":false") != NULL);
    CHECK(strstr(b, "\"code\":\"bad-syntax\"") != NULL);
    CHECK(run("", CMD_CLI, b, sizeof b, &e) == -1);
    CHECK_STR_EQ(e.code, "bad-syntax");
}

TEST(version_command) {
    char b[512];
    struct cmd_err e;
    cmd_init();
    CHECK(run("version", CMD_CLI, b, sizeof b, &e) == 0);
    CHECK(strstr(b, "\"ok\":true") != NULL);
    CHECK(strstr(b, "\"protocol\":1") != NULL);
    CHECK(strstr(b, "\"version\":\"") != NULL);
    CHECK(strstr(b, "\"build\":\"") != NULL);
    CHECK(strstr(b, "\"uptime_s\":") != NULL);
}

TEST(where_is_enforced) {
    char b[512];
    struct cmd_err e;
    CHECK(run("port add p1 ifname eth0 ip 10.0.1.1/24", CMD_CLI, b, sizeof b, &e) == -1);
    CHECK_STR_EQ(e.code, "config-only");
    CHECK(run("version", CMD_CONFIG, b, sizeof b, &e) == -1);
    CHECK_STR_EQ(e.code, "bad-syntax");
}

TEST(port_add_argument_validation) {
    char b[512];
    struct cmd_err e;
    CHECK(run("port add p1 ifname eth0 ip 10.0.1.1/33", CMD_CONFIG, b, sizeof b, &e) == -1);
    CHECK_STR_EQ(e.code, "bad-prefix");
    CHECK(run("port add p1 ifname eth0", CMD_CONFIG, b, sizeof b, &e) == -1);
    CHECK_STR_EQ(e.code, "bad-syntax");
    CHECK(run("port add p1 ifname eth0 ip 10.0.1.1", CMD_CONFIG, b, sizeof b, &e) == -1);
    CHECK_STR_EQ(e.code, "bad-prefix");
}

/* ---- full command set against the lab fixture (T061) ---- */
#include "fixture.h"

static char out[65536];
static struct cmd_err cerr;

static int cli(const char *line) {
    struct jw w;
    jw_init(&w, out, sizeof out);
    memset(&cerr, 0, sizeof cerr);
    int rc = cmd_exec(line, CMD_CLI, &w, &cerr);
    CHECK(jw_finish(&w) == 0);
    return rc;
}

static void state_digest(char *dst, size_t cap) {
    dst[0] = 0;
    static const char *const cmds[] = {"route show", "neigh show", "port show"};
    for (size_t i = 0; i < 3; i++) {
        cli(cmds[i]);
        strncat(dst, out, cap - strlen(dst) - 1);
    }
}

TEST(show_commands_json_shapes) {
    fx_setup(1);
    CHECK(cli("route show") == 0);
    CHECK(strstr(out, "{\"prefix\":\"10.0.1.0/24\",\"port\":null,\"gateway\":null,\"origin\":\"connected\"}"));
    CHECK(strstr(out, "{\"prefix\":\"10.0.1.11/32\",\"port\":\"p1\",\"gateway\":null,\"origin\":\"host\"}"));
    CHECK(cli("neigh show") == 0);
    CHECK(strstr(out, "{\"ip\":\"10.0.2.13\",\"mac\":\"02:00:00:00:01:03\",\"port\":\"p3\","
                      "\"kind\":\"static\",\"state\":\"REACHABLE\",\"held\":0}"));
    CHECK(cli("port show") == 0);
    CHECK(strstr(out, "\"name\":\"p1\",\"ifname\":\"p1\",\"mac\":\"02:00:00:00:00:01\","
                      "\"ip\":\"10.0.1.1/24\",\"mtu\":1500,\"admin\":\"up\",\"oper\":\"up\""));
    CHECK(cli("stats show") == 0);
    CHECK(strstr(out, "\"mbuf\":{\"total\":4096,\"in_use\":0}"));
    CHECK(strstr(out, "\"drops\":{\"total\":0,\"reasons\":{\"truncated\":0"));
    CHECK(strstr(out, "\"events\":{\"icmp_ratelimited\":0"));
    CHECK(cli("route lookup 10.0.2.13") == 0);
    CHECK(strstr(out, "\"route\":{\"prefix\":\"10.0.2.13/32\""));
    CHECK(cli("version") == 0);
    fx_teardown();
}

TEST(mutating_commands_work) {
    fx_setup(0);
    CHECK(cli("route add 10.0.9.0/24 dev p3 via 10.0.2.13") == 0);
    CHECK(strstr(out, "{\"ok\":true,\"data\":{}}"));
    CHECK(cli("route lookup 10.0.9.1") == 0);
    CHECK(strstr(out, "\"gateway\":\"10.0.2.13\""));
    CHECK(cli("route del 10.0.9.0/24") == 0);
    CHECK(cli("route lookup 10.0.9.1") == -1);
    CHECK_STR_EQ(cerr.code, "not-found");
    CHECK(cli("neigh add 10.0.2.13 lladdr 02:00:00:00:01:03 dev p3") == 0);
    CHECK(cli("neigh del 10.0.2.13") == 0);
    CHECK(cli("port set p3 admin down") == 0);
    CHECK(!port_find("p3")->admin_up);
    CHECK(cli("port set p3 admin up") == 0);
    CHECK(port_find("p3")->admin_up);
    stats_drop(DROP_NO_ROUTE);
    CHECK(cli("stats clear") == 0);
    CHECK_EQ_U64(g_stats.drop_total, 0);
    fx_teardown();
}

TEST(every_error_code_is_reachable_and_leaves_state_unchanged) {
    fx_setup(1);
    char before[2 * 65536], after[2 * 65536];
    struct {
        const char *line;
        const char *code;
    } bad[] = {
        {"route add 10.0.9.1/24 dev p3", "bad-prefix"},
        {"route add nonsense dev p3", "bad-prefix"},
        {"route add 10.0.9.0/24 dev p3 via 999.1.1.1", "bad-address"},
        {"route add 10.0.9.0/24 dev p3 via 10.7.7.7", "gw-not-connected"},
        {"route add 10.0.9.0/24 dev nope", "unknown-port"},
        {"route add 10.0.9.0/24", "bad-syntax"},
        {"route add 10.0.1.0/24 dev p1", "exists"},
        {"route del 10.0.2.0/24", "immutable"},
        {"route del 10.0.2.13/32", "immutable"},
        {"route del 10.5.0.0/16", "not-found"},
        {"route del 10.5.0.1/16", "bad-prefix"},
        {"route lookup 1.2.3", "bad-address"},
        {"neigh add 10.0.1.99 lladdr zz:00:00:00:00:00 dev p1", "bad-mac"},
        {"neigh add 10.0.1.99.9 lladdr 02:00:00:00:00:09 dev p1", "bad-address"},
        {"neigh add 10.0.1.99 lladdr 02:00:00:00:00:09 dev nope", "unknown-port"},
        {"neigh add 10.0.1.99 lladdr 02:00:00:00:00:09 dev p1", "exists"}, /* p1 already bound */
        {"neigh add 10.0.1.11 lladdr 02:00:00:00:00:09 dev p2", "exists"},
        {"neigh del 10.1.1.1", "not-found"},
        {"port set nope admin up", "unknown-port"},
        {"port set p1 admin sideways", "bad-syntax"},
        {"port add p9 ifname p9 ip 10.9.9.1/24", "config-only"},
        {"icmp-ratelimit rate 0 burst 5", "bad-syntax"},
        {"icmp-ratelimit rate 5", "bad-syntax"},
        {"stats show extra", "bad-syntax"},
        {"bogus", "bad-syntax"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        state_digest(before, sizeof before);
        CHECK(cli(bad[i].line) == -1);
        if (cerr.code == NULL || strcmp(cerr.code, bad[i].code) != 0)
            TH_FAIL("'%s': got %s, want %s", bad[i].line, cerr.code ? cerr.code : "(none)", bad[i].code);
        CHECK(strstr(out, "\"ok\":false") != NULL);
        state_digest(after, sizeof after);
        if (strcmp(before, after) != 0)
            TH_FAIL("'%s' changed state", bad[i].line);
    }
    fx_teardown();
}

TEST(table_full_error) {
    fx_setup(1);
    for (uint32_t i = 0; route_count() < LPM_MAX_RULES; i++) {
        char line[96];
        snprintf(line, sizeof line, "route add %u.%u.%u.0/24 dev p3", 100 + (i >> 16), (i >> 8) & 255, i & 255);
        CHECK(cli(line) == 0);
    }
    CHECK(cli("route add 200.0.0.0/24 dev p3") == -1);
    CHECK_STR_EQ(cerr.code, "table-full");
    CHECK(strstr(out, "route table full (10000)"));
    fx_teardown();
}

TEST(oversized_response_is_an_error_not_truncation) {
    fx_setup(1);
    for (uint32_t i = 0; i < 2000; i++) {
        char line[96];
        snprintf(line, sizeof line, "route add 100.%u.%u.0/24 dev p3", i >> 8, i & 255);
        CHECK(cli(line) == 0);
    }
    struct jw w;
    char small[4096];
    jw_init(&w, small, sizeof small);
    struct cmd_err e;
    cmd_exec("route show", CMD_CLI, &w, &e);
    CHECK(w.overflow); /* the server maps this to an error response (ctl_server.c) */
    fx_teardown();
}
