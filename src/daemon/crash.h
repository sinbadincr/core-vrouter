#ifndef MINIVR_CRASH_H
#define MINIVR_CRASH_H
/* Crash diagnostics (research R11, FR-021). In debug/asan builds only: unlimited core size and
 * a SIGSEGV/SIGABRT/SIGBUS handler on an alternate stack that logs a symbolized backtrace to
 * stderr, then re-raises with the default action so the kernel writes the core. */
void crash_install(void);      /* before the privilege drop (raises RLIMIT_CORE) */
void crash_enter_dumpdir(void); /* after privdrop created and chowned MINIVR_DUMP_DIR */
#endif
