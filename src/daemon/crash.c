#include "crash.h"

#include <execinfo.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "minivr/log.h"
#include "minivr/privdrop.h"

#ifdef MINIVR_DEBUG

static uint8_t altstack[64 * 1024];

static void on_fatal(int sig) {
    static const char hdr[] = "FATAL: fatal signal, backtrace follows\n";
    void *frames[48];
    (void)!write(STDERR_FILENO, hdr, sizeof hdr - 1);
    int n = backtrace(frames, 48);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    signal(sig, SIG_DFL);
    raise(sig);
}

void crash_install(void) {
    struct rlimit rl = {RLIM_INFINITY, RLIM_INFINITY};
    if (setrlimit(RLIMIT_CORE, &rl) != 0)
        log_warn("crash", "cannot raise RLIMIT_CORE; core dumps may be truncated");
    void *warm[2];
    backtrace(warm, 2); /* load libgcc now so the handler does not allocate */
    stack_t ss = {.ss_sp = altstack, .ss_size = sizeof altstack, .ss_flags = 0};
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fatal;
    sa.sa_flags = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

void crash_enter_dumpdir(void) {
    if (chdir(MINIVR_DUMP_DIR) != 0)
        log_warn("crash", "cannot enter dump directory", "dir=%s", MINIVR_DUMP_DIR);
}

#else /* release: no handlers, default core behaviour */

void crash_install(void) {}
void crash_enter_dumpdir(void) {}

#endif
