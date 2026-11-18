#ifndef MINIVR_CTL_SERVER_H
#define MINIVR_CTL_SERVER_H
#include <stdint.h>
#include <sys/types.h>

/* Control socket server (contracts/control-protocol.md). Must run before privdrop. */
int ctl_server_init(const char *path, uid_t owner_uid);
void ctl_server_tick(uint64_t now_ns); /* idle-client timeout */
void ctl_server_close(void);           /* close the listener and all clients */
#endif
