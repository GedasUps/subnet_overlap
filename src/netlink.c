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

/* Pagalbinė funkcija: konvertuoja kaukę į prefiksą (255.255.255.0 -> 24) */
static int netmask_to_prefix(struct sockaddr *mask)
{
	if (!mask)
		return 0;
	uint32_t m = ((struct sockaddr_in *)mask)->sin_addr.s_addr;
	int bits   = 0;
	m	   = ntohl(m);
	while (m & 0x80000000) {
		bits++;
		m <<= 1;
	}
	return bits;
}

static uint32_t prefix_to_mask(int prefix)
{
	if (prefix <= 0)
		return 0;
	return htonl(~((1U << (32 - prefix)) - 1));
}

static void handle_subnet_logic(const char *ifname, uint32_t ip, int prefix)
{
	uint32_t mask	 = prefix_to_mask(prefix);
	uint32_t network = ip & mask;

	if (strcmp(ifname, "br-lan") == 0) {
		current_lan_network = network;
		current_lan_ip	    = ip;
		syslog(LOG_NOTICE, "Netlink: LAN subnet monitored: %u.%u.%u.%u/%d", (ip & 0xFF),
		       (ip >> 8 & 0xFF), (ip >> 16 & 0xFF), (ip >> 24 & 0xFF), prefix);
	} else if (strcmp(ifname, "eth1") == 0) {
		// Tikriname konfliktą
		int conflict = (current_lan_network != 0 && network == current_lan_network);

		// Pranešame UBUS sistemai (tuo pačiu atnaujiname statusą ubus.c viduje)
		ubus_notify_conflict(conflict, &ip, &current_lan_ip);

		if (conflict) {
			syslog(LOG_ERR, "Netlink: !!! CONFLICT detected on %s !!! WAN IP matches LAN subnet", ifname);
		} else {
			syslog(LOG_INFO, "Netlink: WAN interface %s checked, no conflict with LAN", ifname);
		}
	}
}

/* Ši funkcija kviečiama TIK VIENĄ KARTĄ programos starto metu */
void netlink_init_lan_status(void)
{
	struct ifaddrs *ifaddr, *ifa;
	char ip_str[INET_ADDRSTRLEN];

	if (getifaddrs(&ifaddr) == -1) {
		syslog(LOG_ERR, "Netlink: getifaddrs failed during bootstrap");
		return;
	}

	// 1 etapas: Surandame LAN
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
			continue;

		if (strcmp(ifa->ifa_name, "br-lan") == 0) {
			struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
			struct sockaddr_in *mask = (struct sockaddr_in *)ifa->ifa_netmask;

			current_lan_ip	    = addr->sin_addr.s_addr;
			current_lan_network = current_lan_ip & mask->sin_addr.s_addr;

			inet_ntop(AF_INET, &current_lan_ip, ip_str, sizeof(ip_str));
			syslog(LOG_NOTICE, "Netlink Bootstrap: LAN detected: %s", ip_str);
		}
	}

	// 2 etapas: Surandame WAN ir patikriname konfliktą, jei jis jau prijungtas
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
			continue;

		if (strcmp(ifa->ifa_name, "eth1") == 0) {
			struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
			int prefix = netmask_to_prefix(ifa->ifa_netmask);

			inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
			syslog(LOG_NOTICE, "Netlink Bootstrap: WAN detected: %s", ip_str);

			handle_subnet_logic(ifa->ifa_name, addr->sin_addr.s_addr, prefix);
		}
	}

	freeifaddrs(ifaddr);
}

static void parse_address_msg(struct nlmsghdr *nh)
{
	struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(nh);
	struct rtattr *rta    = (struct rtattr *)IFA_RTA(ifa);
	int rta_len	      = IFA_PAYLOAD(nh);
	char ifname[IF_NAMESIZE];
	uint32_t ip = 0;

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

	struct sockaddr_nl sa;
	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;
	sa.nl_groups = RTMGRP_IPV4_IFADDR;

	if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(sock);
		return -1;
	}
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
			parse_address_msg(nh);
		}
	}
}