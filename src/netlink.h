#include <linux/rtnetlink.h>

int netlink_setup_socket(void);
void netlink_init_lan_status(void);
void netlink_handle_event(int fd);
