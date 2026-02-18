#include <libubox/blobmsg_json.h>
#include "ubus.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <syslog.h>

static struct ubus_context *ctx = NULL;
static struct blob_buf b;

// Global state to track conflict
static int current_conflict    = 0;
static char current_wan_ip[16] = "";
static char current_lan_ip[16] = "";

static int conflict_status(struct ubus_context *c, struct ubus_object *obj, struct ubus_request_data *req,
			   const char *method, struct blob_attr *msg)
{
	blob_buf_init(&b, 0);

	// Return current conflict status
	blobmsg_add_u32(&b, "conflict", current_conflict);
	blobmsg_add_string(&b, "wan_ip", current_wan_ip[0] ? current_wan_ip : "N/A");
	blobmsg_add_string(&b, "lan_ip", current_lan_ip[0] ? current_lan_ip : "N/A");
	blobmsg_add_string(&b, "message",
			   current_conflict ? "Subnet conflict detected!" : "No conflicts detected");

	syslog(LOG_INFO, "UBUS: status method called - conflict=%d", current_conflict);
	return ubus_send_reply(c, req, b.head);
}

static const struct ubus_method conflict_methods[] = {
	UBUS_METHOD_NOARG("status", conflict_status),
};

static struct ubus_object_type conflict_obj_type = UBUS_OBJECT_TYPE("network.conflict", conflict_methods);

static struct ubus_object conflict_obj = {
	.name	   = "network.conflict",
	.type	   = &conflict_obj_type,
	.methods   = conflict_methods,
	.n_methods = ARRAY_SIZE(conflict_methods),
};

int ubus_init(void)
{
	ctx = ubus_connect(NULL);
	if (!ctx) {
		syslog(LOG_ERR, "UBUS: Failed to connect");
		return -1;
	}

	ubus_add_uloop(ctx);

	int ret = ubus_add_object(ctx, &conflict_obj);
	if (ret) {
		syslog(LOG_ERR, "UBUS: Failed to add object: %d", ret);
		return ret;
	}

	syslog(LOG_INFO, "UBUS: network.conflict object registered");
	return 0;
}

int ubus_notify_conflict(int detected, uint32_t *wan_ip, uint32_t *lan_ip)
{
	current_conflict = detected;
	inet_ntop(AF_INET, wan_ip, current_wan_ip, sizeof(current_wan_ip));
	inet_ntop(AF_INET, lan_ip, current_lan_ip, sizeof(current_lan_ip));

	blob_buf_init(&b, 0);
	blobmsg_add_u32(&b, "conflict", detected);
	blobmsg_add_string(&b, "wan_ip", current_wan_ip);
	blobmsg_add_string(&b, "lan_ip", current_lan_ip);
	blobmsg_add_string(&b, "message", detected ? "Subnet conflict detected!" : "Conflict resolved");

	syslog(LOG_NOTICE, "UBUS: Notifying conflict status - detected=%d, wan=%s, lan=%s", detected,
	       current_wan_ip, current_lan_ip);

	return ubus_notify(ctx, &conflict_obj, "update", b.head, -1);
}

void ubus_deinit(void)
{
	if (ctx) {
		ubus_free(ctx);
		ctx = NULL;
	}
}