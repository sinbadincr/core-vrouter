#include "fixture.h"
#include "tharness.h"

static int run(enum cmd_where where, const char *line, struct cmd_err *e) {
    char buf[512];
    struct jw w;
    jw_init(&w, buf, sizeof buf);
    return cmd_exec(line, where, &w, e);
}

struct collect {
    int n;
    uint32_t prefix[32];
    uint8_t len[32];
    enum route_origin origin[32];
    int port[32];
};
static void collect_cb(const struct route *r, void *arg) {
    struct collect *c = arg;
    c->prefix[c->n] = r->prefix;
    c->len[c->n] = r->len;
    c->origin[c->n] = r->origin;
    c->port[c->n] = r->port;
    c->n++;
}

TEST(connected_routes_and_shared_subnet) {
    fx_setup(1);
    const struct route *r = route_find(IP4(10, 0, 2, 0), 24);
    CHECK(r != NULL);
    CHECK(r->origin == ROUTE_CONNECTED);
    CHECK_EQ_U64(r->port, port_find("p3")->id);
    /* p1 and p2 share 10.0.1.0/24: egress none */
    r = route_find(IP4(10, 0, 1, 0), 24);
    CHECK(r != NULL);
    CHECK_EQ_U64(r->port, ROUTE_NO_PORT);
    fx_teardown();
}

TEST(static_neighbour_installs_and_removes_host_route) {
    fx_setup(1);
    const struct route *r = route_find(VM1_IP, 32);
    CHECK(r != NULL);
    CHECK(r->origin == ROUTE_HOST);
    CHECK_EQ_U64(r->port, port_find("p1")->id);
    struct cmd_err e;
    CHECK(neigh_del(VM1_IP, &e) == 0);
    CHECK(route_find(VM1_IP, 32) == NULL);
    fx_teardown();
}

TEST(host_routes_count_toward_capacity) {
    fx_setup(1);
    unsigned before = route_count(); /* 2 connected subnets + 3 host routes */
    CHECK_EQ_U64(before, 5);
    fx_teardown();
}

TEST(route_add_validation) {
    fx_setup(1);
    struct cmd_err e;
    CHECK(run(CMD_CLI, "route add 10.0.9.0/24 dev p3 via 10.0.2.13", &e) == 0);
    const struct route *r = route_lookup(IP4(10, 0, 9, 1));
    CHECK(r && r->origin == ROUTE_STATIC && r->gateway == VM3_IP);
    CHECK(run(CMD_CLI, "route add 10.0.9.0/24 dev p3 via 10.0.2.13", &e) == -1);
    CHECK_STR_EQ(e.code, "exists");
    CHECK(run(CMD_CLI, "route add 10.0.8.0/24 dev p3 via 10.0.7.1", &e) == -1); /* outside p3 subnet */
    CHECK_STR_EQ(e.code, "gw-not-connected");
    CHECK(run(CMD_CLI, "route add 10.0.8.1/24 dev p3", &e) == -1);
    CHECK_STR_EQ(e.code, "bad-prefix");
    CHECK(run(CMD_CLI, "route add 10.0.8.0/24 dev p9", &e) == -1);
    CHECK_STR_EQ(e.code, "unknown-port");
    fx_teardown();
}

TEST(route_del_rules) {
    fx_setup(1);
    struct cmd_err e;
    CHECK(route_del(IP4(10, 0, 2, 0), 24, &e) == -1); /* connected */
    CHECK_STR_EQ(e.code, "immutable");
    CHECK(route_del(VM3_IP, 32, &e) == -1); /* host */
    CHECK_STR_EQ(e.code, "immutable");
    CHECK(route_del(IP4(10, 9, 0, 0), 16, &e) == -1);
    CHECK_STR_EQ(e.code, "not-found");
    CHECK(run(CMD_CLI, "route add 10.0.9.0/24 dev p3 via 10.0.2.13", &e) == 0);
    CHECK(route_del(IP4(10, 0, 9, 0), 24, &e) == 0);
    CHECK(route_find(IP4(10, 0, 9, 0), 24) == NULL);
    fx_teardown();
}

TEST(table_full_at_10000) {
    fx_setup(1);
    struct cmd_err e;
    unsigned n = route_count();
    for (uint32_t i = 0; n < LPM_MAX_RULES; i++, n++)
        CHECK(route_add_static(IP4(100, 0, 0, 0) + (i << 8), 24, 0, 0, &e) == 0);
    CHECK(route_add_static(IP4(200, 0, 0, 0), 24, 0, 0, &e) == -1);
    CHECK_STR_EQ(e.code, "table-full");
    CHECK_EQ_U64(route_count(), LPM_MAX_RULES);
    fx_teardown();
}

TEST(walk_is_in_prefix_order) {
    fx_setup(1);
    struct collect c = {0};
    route_walk(collect_cb, &c);
    CHECK_EQ_U64(c.n, 5);
    for (int i = 1; i < c.n; i++)
        CHECK(c.prefix[i - 1] < c.prefix[i] || (c.prefix[i - 1] == c.prefix[i] && c.len[i - 1] < c.len[i]));
    CHECK_EQ_U64(c.prefix[0], IP4(10, 0, 1, 0)); /* connected before its /32s */
    fx_teardown();
}
