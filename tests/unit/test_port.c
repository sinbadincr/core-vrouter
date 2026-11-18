#include "fixture.h"
#include "tharness.h"

static int run(const char *line, struct cmd_err *e) {
    char buf[256];
    struct jw w;
    jw_init(&w, buf, sizeof buf);
    return cmd_exec(line, CMD_CONFIG, &w, e);
}

TEST(sixteen_ports_then_table_full) {
    fx_setup(1);
    port_reset();
    pio_fake_reset();
    struct cmd_err e;
    for (int i = 0; i < MAX_PORTS; i++) {
        char name[16], line[128];
        uint8_t mac[6] = {2, 0, 0, 0, 9, (uint8_t)i};
        snprintf(name, sizeof name, "q%d", i);
        pio_fake_add_if(name, mac, 1500);
        snprintf(line, sizeof line, "port add %s ifname %s ip 10.%d.0.1/24", name, name, 100 + i);
        if (run(line, &e) != 0)
            TH_FAIL("port %d refused: %s", i, e.message);
    }
    CHECK_EQ_U64(port_count(), MAX_PORTS);
    uint8_t mac[6] = {2, 0, 0, 0, 9, 99};
    pio_fake_add_if("q16", mac, 1500);
    CHECK(run("port add q16 ifname q16 ip 10.200.0.1/24", &e) == -1);
    CHECK_STR_EQ(e.code, "table-full");
    fx_teardown();
}

TEST(port_add_validation) {
    fx_setup(1);
    struct cmd_err e;
    CHECK(run("port add p1 ifname p9 ip 10.9.9.1/24", &e) == -1); /* name taken */
    CHECK_STR_EQ(e.code, "exists");
    CHECK(run("port add px ifname p1 ip 10.9.9.1/24", &e) == -1); /* interface already used */
    CHECK_STR_EQ(e.code, "exists");
    CHECK(run("port add px ifname nosuch ip 10.9.9.1/24", &e) == -1);
    CHECK_STR_EQ(e.code, "not-found");
    uint8_t mac[6] = {2, 0, 0, 0, 7, 7};
    pio_fake_add_if("x1", mac, 1500);
    /* the anycast address may be shared only with an identical prefix */
    CHECK(run("port add px ifname x1 ip 10.0.1.1/16", &e) == -1);
    CHECK_STR_EQ(e.code, "exists");
    CHECK(run("port add px ifname x1 ip 10.0.1.1/24", &e) == 0);
    CHECK(run("port add averyveryverylongportname ifname x1 ip 10.9.9.1/24", &e) == -1);
    fx_teardown();
}

TEST(port_reads_mac_and_mtu_from_the_interface) {
    fx_setup(1);
    const struct port *p = port_find("p2");
    CHECK_MEM_EQ(p->mac, MAC_P2, 6);
    CHECK_EQ_U64(p->mtu, 1500);
    CHECK(p->admin_up && p->oper_up);
    CHECK_EQ_U64(port_netmask(p), 0xffffff00);
    fx_teardown();
}
