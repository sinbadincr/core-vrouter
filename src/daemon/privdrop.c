#include "minivr/privdrop.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "minivr/log.h"

#ifndef PR_CAP_AMBIENT
#define PR_CAP_AMBIENT 47
#define PR_CAP_AMBIENT_CLEAR_ALL 4
#endif

static int verify(uid_t want_uid) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) {
        log_error("privdrop", "cannot read /proc/self/status", "errno=%d", errno);
        return -1;
    }
    char line[256];
    int ok_uid = 0, ok_eff = 0, ok_prm = 0, ok_amb = 0, ok_nnp = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned r, e, s, fs;
        char cap[32];
        int nnp;
        if (sscanf(line, "Uid: %u %u %u %u", &r, &e, &s, &fs) == 4)
            ok_uid = r == want_uid && e == want_uid && s == want_uid && fs == want_uid && r != 0;
        else if (sscanf(line, "CapEff: %31s", cap) == 1)
            ok_eff = strspn(cap, "0") == strlen(cap);
        else if (sscanf(line, "CapPrm: %31s", cap) == 1)
            ok_prm = strspn(cap, "0") == strlen(cap);
        else if (sscanf(line, "CapAmb: %31s", cap) == 1)
            ok_amb = strspn(cap, "0") == strlen(cap);
        else if (sscanf(line, "NoNewPrivs: %d", &nnp) == 1)
            ok_nnp = nnp == 1;
    }
    fclose(f);
    if (!(ok_uid && ok_eff && ok_prm && ok_amb && ok_nnp)) {
        log_error("privdrop", "self-verification failed",
                  "uid_ok=%d capeff_ok=%d capprm_ok=%d capamb_ok=%d nnp_ok=%d", ok_uid, ok_eff,
                  ok_prm, ok_amb, ok_nnp);
        return -1;
    }
    return 0;
}

int privdrop(const char *user) {
    struct passwd *pw = getpwnam(user);
    if (!pw) {
        log_error("privdrop", "unknown user", "user=%s", user);
        return -1;
    }
    if (pw->pw_uid == 0) {
        log_error("privdrop", "refusing to run as uid 0", "user=%s", user);
        return -1;
    }
    int is_root = geteuid() == 0;

    if (is_root) {
        if (mkdir(MINIVR_DUMP_DIR, 0700) < 0 && errno != EEXIST) {
            log_error("privdrop", "cannot create dump dir", "dir=%s errno=%d", MINIVR_DUMP_DIR,
                      errno);
            return -1;
        }
        if (chown(MINIVR_DUMP_DIR, pw->pw_uid, pw->pw_gid) < 0 || chmod(MINIVR_DUMP_DIR, 0700) < 0) {
            log_error("privdrop", "cannot own dump dir", "dir=%s errno=%d", MINIVR_DUMP_DIR, errno);
            return -1;
        }
        if (prctl(PR_SET_KEEPCAPS, 0, 0, 0, 0) < 0 || setgroups(0, NULL) < 0 ||
            setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0 ||
            setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0) {
            log_error("privdrop", "cannot change identity", "user=%s errno=%d", user, errno);
            return -1;
        }
    } else {
        log_warn("privdrop", "not running as root; identity change skipped");
    }
    if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) < 0 && errno != EINVAL) {
        log_error("privdrop", "cannot clear ambient caps", "errno=%d", errno);
        return -1;
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        log_error("privdrop", "cannot set no_new_privs", "errno=%d", errno);
        return -1;
    }
#ifdef MINIVR_DEBUG
    prctl(PR_SET_DUMPABLE, 1, 0, 0, 0); /* a uid change resets it; keep core dumps (R11) */
#endif
    uid_t want = is_root ? pw->pw_uid : geteuid();
    if (verify(want) != 0)
        return -1;
    log_info("privdrop", "privileges dropped", "user=%s uid=%u", user, (unsigned)want);
    return 0;
}
