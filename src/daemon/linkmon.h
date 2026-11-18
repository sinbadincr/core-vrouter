#ifndef MINIVR_LINKMON_H
#define MINIVR_LINKMON_H
/* rtnetlink RTMGRP_LINK monitor: keeps port.oper_up in step with IFF_RUNNING (research R14).
 * Open it before the privilege drop; it needs no privileges afterwards. */
int linkmon_init(void);
void linkmon_close(void);
#endif
