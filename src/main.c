#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <libubox/uloop.h>
#include "ubus.h"
#include "netlink.h"

static struct uloop_fd net_fd;

static void netlink_cb(struct uloop_fd *u, unsigned int events)
{
	netlink_handle_event(u->fd);
}

int main(void)
{
	openlog("subnet_overlap", LOG_PID, LOG_DAEMON);
	uloop_init();

	if (ubus_init() < 0) {
		syslog(LOG_ERR, "Failed to initialize UBUS");
		closelog();
		return 1;
	}
	netlink_init_lan_status();
	int sock = netlink_setup_socket();
	if (sock < 0) {
		syslog(LOG_ERR, "Failed to setup Netlink socket");
		ubus_deinit();
		closelog();
		return 1;
	}

	net_fd.fd = sock;
	net_fd.cb = netlink_cb;
	uloop_fd_add(&net_fd, ULOOP_READ);

	syslog(LOG_INFO, "Entering main event loop.");
	uloop_run();

	syslog(LOG_INFO, "Shutting down monitor.");
	uloop_fd_delete(&net_fd);
	close(sock);
	ubus_deinit();
	uloop_done();
	closelog();

	return 0;
}