#include <stdio.h>
#include <string.h>

#include "minivr/privdrop.h"
#include "tharness.h"

#define NOBODY 65534

/* A /proc/self/status excerpt with the fields the check reads, and unrelated noise around them. */
static void status(char *out, size_t cap, unsigned uid, unsigned saved, const char *eff,
                   const char *prm, const char *amb, int nnp) {
    snprintf(out, cap,
             "Name:\tminivrd\nState:\tS (sleeping)\nUid:\t%u\t%u\t%u\t%u\nGid:\t65534\t65534\t65534\t65534\n"
             "CapInh:\t0000000000000000\nCapPrm:\t%s\nCapEff:\t%s\nCapBnd:\t000001ffffffffff\n"
             "CapAmb:\t%s\nNoNewPrivs:\t%d\nSeccomp:\t0\n",
             uid, uid, saved, uid, prm, eff, amb, nnp);
}

#define ZERO "0000000000000000"

TEST(a_dropped_process_passes) {
    char t[1024];
    status(t, sizeof t, NOBODY, NOBODY, ZERO, ZERO, ZERO, 1);
    CHECK_EQ_U64(privdrop_check_status(t, NOBODY), 0);
}

TEST(each_failure_is_reported_on_its_own) {
    char t[1024];
    status(t, sizeof t, NOBODY, NOBODY, "0000000000000001", ZERO, ZERO, 1);
    CHECK_EQ_U64(privdrop_check_status(t, NOBODY), PRIVCHK_CAPEFF);
    status(t, sizeof t, NOBODY, NOBODY, ZERO, "000001ffffffffff", ZERO, 1);
    CHECK_EQ_U64(privdrop_check_status(t, NOBODY), PRIVCHK_CAPPRM);
    status(t, sizeof t, NOBODY, NOBODY, ZERO, ZERO, "0000000000002000", 1);
    CHECK_EQ_U64(privdrop_check_status(t, NOBODY), PRIVCHK_CAPAMB);
    status(t, sizeof t, NOBODY, NOBODY, ZERO, ZERO, ZERO, 0);
    CHECK_EQ_U64(privdrop_check_status(t, NOBODY), PRIVCHK_NNP);
    status(t, sizeof t, 1000, 1000, ZERO, ZERO, ZERO, 1); /* wrong user */
    CHECK_EQ_U64(privdrop_check_status(t, NOBODY), PRIVCHK_UID);
}

TEST(saved_uid_root_means_privileges_can_come_back) {
    char t[1024];
    status(t, sizeof t, NOBODY, 0, ZERO, ZERO, ZERO, 1); /* saved set-user-ID still 0 */
    CHECK_EQ_U64(privdrop_check_status(t, NOBODY), PRIVCHK_UID);
}

TEST(running_as_root_is_never_accepted) {
    char t[1024];
    status(t, sizeof t, 0, 0, ZERO, ZERO, ZERO, 1);
    CHECK_EQ_U64(privdrop_check_status(t, 0), PRIVCHK_UID);
}

TEST(a_full_root_process_fails_everything) {
    char t[1024];
    status(t, sizeof t, 0, 0, "000001ffffffffff", "000001ffffffffff", "000001ffffffffff", 0);
    CHECK_EQ_U64(privdrop_check_status(t, NOBODY), 31);
}

TEST(missing_or_garbled_fields_count_as_failures) {
    CHECK_EQ_U64(privdrop_check_status("", NOBODY), 31);
    CHECK_EQ_U64(privdrop_check_status("Name:\tx\nState:\tS\n", NOBODY), 31);
    CHECK_EQ_U64(privdrop_check_status("Uid:\t65534\t65534\nCapEff:\t\nNoNewPrivs:\tx\n", NOBODY), 31);
    char t[1024];
    status(t, sizeof t, NOBODY, NOBODY, ZERO, ZERO, ZERO, 1);
    *strstr(t, "CapAmb") = 'X'; /* an older kernel without CapAmb must not pass silently */
    CHECK_EQ_U64(privdrop_check_status(t, NOBODY), PRIVCHK_CAPAMB);
    char longline[3000];
    memset(longline, 'a', sizeof longline - 1);
    longline[sizeof longline - 1] = 0;
    CHECK_EQ_U64(privdrop_check_status(longline, NOBODY), 31); /* no overflow on long lines */
}
