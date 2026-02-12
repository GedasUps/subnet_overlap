#include <libubus.h>

int init_ubus(void);
void stop_ubus(void);
int ubus_notify_conflict(int conflict_detected, uint32_t *wan_ip, uint32_t *lan_ip);