/* SPDX-License-Identifier: GPL-2.0 OR MIT
 * Example WireGuard Init Program (PID 1)
 *
 * This demonstrates how to build a custom init that:
 * 1. Brings up network
 * 2. Configures WireGuard
 * 3. Runs HTTP stats server
 * 4. Handles PID 1 responsibilities (reaping zombies)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>

/* WireGuard tools headers - you can link against the .o files */
#include "ipc.h"
#include "config.h"
#include "containers.h"
#include "encoding.h"

/* ============================================================================
 * Network Setup
 * ============================================================================ */

static int setup_network(void)
{
	int ret;

	/* Bring up loopback */
	ret = system("ip link set lo up");
	if (ret != 0) {
		fprintf(stderr, "Failed to bring up loopback\n");
		return -1;
	}

	/* Bring up ethernet (eth0) */
	ret = system("ip link set eth0 up");
	if (ret != 0) {
		fprintf(stderr, "Failed to bring up eth0\n");
		return -1;
	}

	/* Assign static IP - adjust as needed */
	ret = system("ip addr add 10.0.0.1/24 dev eth0");
	if (ret != 0) {
		fprintf(stderr, "Failed to assign IP address\n");
		return -1;
	}

	/* Optional: default route */
	/* system("ip route add default via 10.0.0.254"); */

	printf("Network configured: eth0 = 10.0.0.1/24\n");
	return 0;
}

/* ============================================================================
 * WireGuard Configuration
 * ============================================================================ */

static int setup_wireguard(const char *config_path)
{
	struct config_ctx ctx;
	struct wgdevice *device = NULL;
	FILE *config_file = NULL;
	char *line = NULL;
	size_t line_len = 0;
	int ret = -1;

	/* Create WireGuard interface first */
	if (system("ip link add wg0 type wireguard") != 0) {
		fprintf(stderr, "Failed to create WireGuard interface\n");
		return -1;
	}

	/* Open config file */
	config_file = fopen(config_path, "r");
	if (!config_file) {
		perror("Failed to open WireGuard config");
		return -1;
	}

	/* Initialize config parser */
	if (!config_read_init(&ctx, false)) {
		fprintf(stderr, "Failed to initialize config parser\n");
		fclose(config_file);
		return -1;
	}

	/* Parse config line by line */
	while (getline(&line, &line_len, config_file) >= 0) {
		if (!config_read_line(&ctx, line)) {
			fprintf(stderr, "Config parsing error\n");
			goto cleanup;
		}
	}

	/* Finalize config */
	device = config_read_finish(&ctx);
	if (!device) {
		fprintf(stderr, "Invalid WireGuard configuration\n");
		goto cleanup;
	}

	/* Set interface name */
	strncpy(device->name, "wg0", IFNAMSIZ - 1);
	device->name[IFNAMSIZ - 1] = '\0';

	/* Apply configuration to kernel */
	if (ipc_set_device(device) != 0) {
		perror("Failed to configure WireGuard interface");
		goto cleanup;
	}

	/* Bring interface up */
	if (system("ip link set wg0 up") != 0) {
		fprintf(stderr, "Failed to bring up wg0\n");
		goto cleanup;
	}

	/* Add WireGuard IP address (read from config or hardcode) */
	/* Adjust as needed based on your setup */
	system("ip addr add 10.100.0.1/24 dev wg0");

	printf("WireGuard interface configured and up\n");
	ret = 0;

cleanup:
	if (config_file)
		fclose(config_file);
	free(line);
	free_wgdevice(device);
	return ret;
}

/* ============================================================================
 * HTTP Statistics Server
 * ============================================================================ */

static void format_bytes(char *buf, size_t buflen, uint64_t bytes)
{
	if (bytes < 1024ULL)
		snprintf(buf, buflen, "%llu B", (unsigned long long)bytes);
	else if (bytes < 1024ULL * 1024ULL)
		snprintf(buf, buflen, "%.2f KiB", (double)bytes / 1024);
	else if (bytes < 1024ULL * 1024ULL * 1024ULL)
		snprintf(buf, buflen, "%.2f MiB", (double)bytes / (1024 * 1024));
	else
		snprintf(buf, buflen, "%.2f GiB", (double)bytes / (1024 * 1024 * 1024));
}

static void handle_http_request(int client_fd)
{
	struct wgdevice *device = NULL;
	struct wgpeer *peer;
	char response[65536];
	char line[1024];
	size_t offset = 0;
	char pubkey[WG_KEY_LEN_BASE64];
	char rx_str[64], tx_str[64];
	time_t now = time(NULL);

	/* Get WireGuard device stats */
	if (ipc_get_device(&device, "wg0") < 0) {
		const char *error = "HTTP/1.0 500 Internal Server Error\r\n\r\nFailed to get WireGuard stats\n";
		write(client_fd, error, strlen(error));
		return;
	}

	/* Build HTTP response */
	offset += snprintf(response + offset, sizeof(response) - offset,
		"HTTP/1.0 200 OK\r\n"
		"Content-Type: text/plain\r\n"
		"Connection: close\r\n"
		"\r\n");

	offset += snprintf(response + offset, sizeof(response) - offset,
		"WireGuard Statistics (wg0)\n"
		"==========================\n\n");

	if (device->flags & WGDEVICE_HAS_PUBLIC_KEY) {
		key_to_base64(pubkey, device->public_key);
		offset += snprintf(response + offset, sizeof(response) - offset,
			"Public Key: %s\n", pubkey);
	}

	if (device->listen_port) {
		offset += snprintf(response + offset, sizeof(response) - offset,
			"Listen Port: %u\n", device->listen_port);
	}

	offset += snprintf(response + offset, sizeof(response) - offset, "\nPeers:\n");

	/* Iterate through all peers */
	for_each_wgpeer(device, peer) {
		key_to_base64(pubkey, peer->public_key);
		format_bytes(rx_str, sizeof(rx_str), peer->rx_bytes);
		format_bytes(tx_str, sizeof(tx_str), peer->tx_bytes);

		offset += snprintf(response + offset, sizeof(response) - offset,
			"\n  Peer: %s\n", pubkey);

		/* Endpoint */
		if (peer->endpoint.addr.sa_family == AF_INET) {
			struct sockaddr_in *addr = (struct sockaddr_in *)&peer->endpoint.addr;
			char ip[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
			offset += snprintf(response + offset, sizeof(response) - offset,
				"    Endpoint: %s:%u\n", ip, ntohs(addr->sin_port));
		} else if (peer->endpoint.addr.sa_family == AF_INET6) {
			struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&peer->endpoint.addr;
			char ip[INET6_ADDRSTRLEN];
			inet_ntop(AF_INET6, &addr->sin6_addr, ip, sizeof(ip));
			offset += snprintf(response + offset, sizeof(response) - offset,
				"    Endpoint: [%s]:%u\n", ip, ntohs(addr->sin6_port));
		}

		/* Last handshake */
		if (peer->last_handshake_time.tv_sec) {
			long long ago = now - peer->last_handshake_time.tv_sec;
			offset += snprintf(response + offset, sizeof(response) - offset,
				"    Last Handshake: %lld seconds ago\n", ago);
		} else {
			offset += snprintf(response + offset, sizeof(response) - offset,
				"    Last Handshake: Never\n");
		}

		/* Transfer stats */
		offset += snprintf(response + offset, sizeof(response) - offset,
			"    Transfer: %s received, %s sent\n", rx_str, tx_str);
		offset += snprintf(response + offset, sizeof(response) - offset,
			"    Raw Transfer: %llu bytes received, %llu bytes sent\n",
			(unsigned long long)peer->rx_bytes,
			(unsigned long long)peer->tx_bytes);
	}

	/* Send response */
	write(client_fd, response, offset);

	free_wgdevice(device);
}

static int run_http_server(int port)
{
	int server_fd, client_fd;
	struct sockaddr_in addr;
	socklen_t addr_len;

	/* Create socket */
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		perror("socket");
		return -1;
	}

	/* Allow reuse */
	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	/* Bind */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(server_fd);
		return -1;
	}

	/* Listen */
	if (listen(server_fd, 5) < 0) {
		perror("listen");
		close(server_fd);
		return -1;
	}

	printf("HTTP stats server listening on port %d\n", port);

	/* Accept loop */
	while (1) {
		addr_len = sizeof(addr);
		client_fd = accept(server_fd, (struct sockaddr *)&addr, &addr_len);
		if (client_fd < 0) {
			if (errno == EINTR)
				continue;
			perror("accept");
			continue;
		}

		/* Handle request */
		handle_http_request(client_fd);
		close(client_fd);
	}

	close(server_fd);
	return 0;
}

/* ============================================================================
 * PID 1 Signal Handling (Zombie Reaping)
 * ============================================================================ */

static void sigchld_handler(int sig)
{
	(void)sig;
	/* Reap all zombie children - critical for PID 1 */
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

static void setup_signals(void)
{
	struct sigaction sa;

	/* Handle SIGCHLD to reap zombies */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigchld_handler;
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);

	/* Ignore common signals that might crash init */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
}

/* ============================================================================
 * Main Init Program
 * ============================================================================ */

int main(int argc, char *argv[])
{
	const char *wg_config = "/wireguard.conf";  /* Config in initramfs */
	int http_port = 8080;

	printf("Starting WireGuard Init (PID %d)\n", getpid());

	/* Setup signal handlers (critical for PID 1) */
	setup_signals();

	/* 1. Setup network */
	if (setup_network() != 0) {
		fprintf(stderr, "Network setup failed\n");
		/* As init, we shouldn't exit, but for critical failures... */
		sleep(1);
		return 1;
	}

	/* 2. Setup WireGuard */
	if (setup_wireguard(wg_config) != 0) {
		fprintf(stderr, "WireGuard setup failed\n");
		/* Continue anyway - HTTP server might still be useful */
	}

	/* 3. Run HTTP stats server (blocks forever) */
	printf("Init complete, starting HTTP server...\n");
	run_http_server(http_port);

	/* Should never reach here */
	fprintf(stderr, "HTTP server exited unexpectedly\n");

	/* As PID 1, keep running forever even if server fails */
	while (1) {
		pause();
	}

	return 0;
}
