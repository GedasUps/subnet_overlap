#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <linux/rtnetlink.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>

#include "netlink.h"
#include "ubus.h"

static uint32_t current_lan_network = 0;
static uint32_t current_lan_ip	    = 0;

static uint32_t prefix_to_mask(int prefix)
{
	if (prefix <= 0)
		return 0;
	return htonl(~((1U << (32 - prefix)) - 1));
}
void netlink_init_lan_status(void)
{
	struct ifaddrs *ifaddr, *ifa;

	if (getifaddrs(&ifaddr) == -1) {
		syslog(LOG_ERR, "Netlink: getifaddrs nepavyko");
		return;
	}

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		// check maybe it has by at start already also wan
		if (strcmp(ifa->ifa_name, "br-lan") == 0) {
			struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
			struct sockaddr_in *mask = (struct sockaddr_in *)ifa->ifa_netmask;

			current_lan_ip	    = addr->sin_addr.s_addr;
			current_lan_network = current_lan_ip & mask->sin_addr.s_addr;

			char ip_str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &current_lan_ip, ip_str, sizeof(ip_str));
			syslog(LOG_NOTICE, "Netlink Bootstrap: LAN initial state detected: %s", ip_str);
			break;
		}
	}

	freeifaddrs(ifaddr);
}

static void handle_subnet_logic(const char *ifname, uint32_t ip, int prefix)
{
	uint32_t mask	 = prefix_to_mask(prefix);
	uint32_t network = ip & mask;
	syslog(LOG_INFO, "Netlink handle subnet logic started for interface: %s", ifname);
	if (strcmp(ifname, "br-lan") == 0) {
		current_lan_network = network;
		current_lan_ip	    = ip;
		syslog(LOG_NOTICE, "Netlink: LAN subnet monitored: %u.%u.%u.%u/%d", (ip & 0xFF),
		       (ip >> 8 & 0xFF), (ip >> 16 & 0xFF), (ip >> 24 & 0xFF), prefix);
	} else if (strcmp(ifname, "eth1") == 0) {
		int conflict = (current_lan_network != 0 && network == current_lan_network);
		ubus_notify_conflict(conflict, &ip, &current_lan_ip);
		syslog(LOG_INFO, "Netlink: WAN subnet monitored: %u.%u.%u.%u/%d", (ip & 0xFF),
		       (ip >> 8 & 0xFF), (ip >> 16 & 0xFF), (ip >> 24 & 0xFF), prefix);
		syslog(LOG_INFO, "Netlink: LAN network: %u.%u.%u.%u/%d", (current_lan_network & 0xFF),
		       (current_lan_network >> 8 & 0xFF), (current_lan_network >> 16 & 0xFF),
		       (current_lan_network >> 24 & 0xFF), prefix);
		if (conflict) {
			syslog(LOG_ERR, "Netlink: CONFLICT detected on %s!", ifname);
		}
	}
}

static void parse_address_msg(struct nlmsghdr *nh)
{
	struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(nh);
	struct rtattr *rta    = (struct rtattr *)IFA_RTA(ifa);
	int rta_len	      = IFA_PAYLOAD(nh);
	char ifname[IF_NAMESIZE];
	uint32_t ip = 0;
	syslog(LOG_INFO, "Netlink: parsing address message");
	if (!if_indextoname(ifa->ifa_index, ifname))
		return;

	while (RTA_OK(rta, rta_len)) {
		if (rta->rta_type == IFA_LOCAL || rta->rta_type == IFA_ADDRESS) {
			ip = *(uint32_t *)RTA_DATA(rta);
		}
		rta = RTA_NEXT(rta, rta_len);
	}

	if (ip != 0) {
		handle_subnet_logic(ifname, ip, ifa->ifa_prefixlen);
	}
}

int netlink_setup_socket(void)
{
	int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock < 0)
		return -1;

	struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = RTMGRP_IPV4_IFADDR };
	if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(sock);
		return -1;
	}
	syslog(LOG_INFO, "Netlink: socket created and bound");
	return sock;
}

void netlink_handle_event(int fd)
{
	char buf[8192];
	int len = recv(fd, buf, sizeof(buf), 0);
	if (len <= 0)
		return;

	for (struct nlmsghdr *nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
		if (nh->nlmsg_type == NLMSG_DONE)
			break;
		if (nh->nlmsg_type == RTM_NEWADDR) {
			syslog(LOG_INFO, "Netlink: new address event");
			parse_address_msg(nh);
		}
	}
}