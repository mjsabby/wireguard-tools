/* SPDX-License-Identifier: GPL-2.0 OR MIT
 * Test Program: Create WireGuard Interface via Netlink
 *
 * This is a minimal standalone test to verify the netlink interface creation
 * works correctly. It's equivalent to running:
 *   ip link add dev wg0 type wireguard
 *
 * Compile:
 *   gcc -o test-netlink-create test-netlink-create.c
 *
 * Run (as root):
 *   ./test-netlink-create
 *
 * Verify:
 *   ip link show wg0
 *   # Should show the wg0 interface
 *
 * Cleanup:
 *   ip link del wg0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <errno.h>

/* Netlink request structure */
struct nl_req {
	struct nlmsghdr n;
	struct ifinfomsg i;
	char buf[1024];
};

/**
 * create_wireguard_interface - Create a WireGuard interface using netlink
 * @ifname: Interface name (e.g., "wg0")
 *
 * This function constructs and sends a netlink RTM_NEWLINK message to create
 * a new WireGuard interface. This is equivalent to:
 *   ip link add dev <ifname> type wireguard
 *
 * Returns: 0 on success, -1 on error
 */
static int create_wireguard_interface(const char *ifname)
{
	int sock;
	struct nl_req req;
	struct rtattr *linkinfo, *attr, *kind;
	struct sockaddr_nl sa;
	char buf[4096];
	int ret = -1;
	int len;
	struct nlmsghdr *nh;
	struct nlmsgerr *err;

	printf("Creating WireGuard interface '%s' using netlink...\n", ifname);

	/* Create netlink socket (AF_NETLINK, NETLINK_ROUTE) */
	sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock < 0) {
		perror("netlink socket");
		return -1;
	}
	printf("  ✓ Netlink socket created (fd=%d)\n", sock);

	/* Bind to netlink */
	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;

	if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("netlink bind");
		close(sock);
		return -1;
	}
	printf("  ✓ Netlink socket bound\n");

	/* Build netlink request to create interface */
	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
	req.n.nlmsg_type = RTM_NEWLINK;
	req.i.ifi_family = AF_UNSPEC;
	printf("  ✓ Netlink message header initialized\n");
	printf("    - Type: RTM_NEWLINK (create new link)\n");
	printf("    - Flags: REQUEST | CREATE | EXCL | ACK\n");

	/* Add interface name attribute (IFLA_IFNAME) */
	attr = (struct rtattr *)(((char *)&req) + NLMSG_ALIGN(req.n.nlmsg_len));
	attr->rta_type = IFLA_IFNAME;
	attr->rta_len = RTA_LENGTH(strlen(ifname) + 1);
	strcpy(RTA_DATA(attr), ifname);
	req.n.nlmsg_len = NLMSG_ALIGN(req.n.nlmsg_len) + RTA_ALIGN(attr->rta_len);
	printf("  ✓ Added IFLA_IFNAME attribute: '%s'\n", ifname);

	/* Add link info for wireguard (IFLA_LINKINFO) */
	linkinfo = (struct rtattr *)(((char *)&req) + NLMSG_ALIGN(req.n.nlmsg_len));
	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_LENGTH(0);
	printf("  ✓ Added IFLA_LINKINFO attribute\n");

	/* Add kind = "wireguard" (IFLA_INFO_KIND) */
	kind = (struct rtattr *)(((char *)linkinfo) + RTA_ALIGN(linkinfo->rta_len));
	kind->rta_type = IFLA_INFO_KIND;
	kind->rta_len = RTA_LENGTH(strlen("wireguard") + 1);
	strcpy(RTA_DATA(kind), "wireguard");
	linkinfo->rta_len = RTA_ALIGN(linkinfo->rta_len) + RTA_ALIGN(kind->rta_len);
	printf("  ✓ Added IFLA_INFO_KIND attribute: 'wireguard'\n");

	req.n.nlmsg_len = NLMSG_ALIGN(req.n.nlmsg_len) + RTA_ALIGN(linkinfo->rta_len);
	printf("  ✓ Final netlink message size: %d bytes\n", req.n.nlmsg_len);

	/* Send netlink message */
	printf("\nSending netlink message to kernel...\n");
	if (send(sock, &req, req.n.nlmsg_len, 0) < 0) {
		perror("netlink send");
		close(sock);
		return -1;
	}
	printf("  ✓ Message sent\n");

	/* Read acknowledgment */
	printf("\nWaiting for kernel acknowledgment...\n");
	len = recv(sock, buf, sizeof(buf), 0);
	if (len < 0) {
		perror("netlink recv");
		close(sock);
		return -1;
	}
	printf("  ✓ Received response (%d bytes)\n", len);

	/* Parse acknowledgment */
	nh = (struct nlmsghdr *)buf;
	if (nh->nlmsg_type == NLMSG_ERROR) {
		err = (struct nlmsgerr *)NLMSG_DATA(nh);
		if (err->error == 0) {
			printf("\n✓ SUCCESS: WireGuard interface '%s' created!\n", ifname);
			ret = 0;
		} else if (err->error == -EEXIST) {
			printf("\n⚠ Interface '%s' already exists\n", ifname);
			ret = 0;  /* Not an error if it already exists */
		} else {
			fprintf(stderr, "\n✗ ERROR: Netlink error creating interface: %s\n",
				strerror(-err->error));
			ret = -1;
		}
	} else {
		fprintf(stderr, "\n✗ ERROR: Unexpected response type: %d\n", nh->nlmsg_type);
		ret = -1;
	}

	close(sock);
	return ret;
}

int main(int argc, char *argv[])
{
	const char *ifname = "wg0";

	/* Allow custom interface name */
	if (argc > 1) {
		ifname = argv[1];
	}

	printf("==============================================\n");
	printf("Netlink WireGuard Interface Creation Test\n");
	printf("==============================================\n\n");

	/* Check if running as root */
	if (getuid() != 0) {
		fprintf(stderr, "ERROR: This program must be run as root\n");
		fprintf(stderr, "Try: sudo %s\n", argv[0]);
		return 1;
	}

	/* Create the interface */
	if (create_wireguard_interface(ifname) != 0) {
		fprintf(stderr, "\nTest FAILED\n");
		return 1;
	}

	/* Print verification instructions */
	printf("\n==============================================\n");
	printf("Verification\n");
	printf("==============================================\n");
	printf("To verify the interface was created, run:\n");
	printf("  ip link show %s\n", ifname);
	printf("\nTo see more details:\n");
	printf("  ip -d link show %s\n", ifname);
	printf("\nTo clean up:\n");
	printf("  ip link del %s\n", ifname);
	printf("==============================================\n");

	return 0;
}
