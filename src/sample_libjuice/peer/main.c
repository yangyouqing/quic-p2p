/**
 * P2P Peer (peer2 role: waits for offer, sends answer).
 * Connect to signaling server, then run ICE with libjuice + coturn.
 * Run this first, then run client so that server assigns peer2 to this process.
 */
#include "common/signal.h"
#include "common/run.h"
#include "juice/juice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

#define SIGNAL_DEFAULT_HOST "localhost"
#define SIGNAL_DEFAULT_PORT "8888"
#define TURN_DEFAULT_PORT 3478

static void print_usage(const char *argv0)
{
	fprintf(stderr,
	        "Usage: %s [options]\n"
	        "  --signaling <host:port>  Signaling server (default: localhost:8888)\n"
	        "  --turn-host <host>       Coturn host (required)\n"
	        "  --turn-port <port>       Coturn port (default: 3478)\n"
	        "  --turn-user <user>        TURN username\n"
	        "  --turn-pass <pass>       TURN password\n"
	        "  --room <id>               Room ID (default: default)\n"
	        "\nExample:\n"
	        "  %s --signaling localhost:8888 --turn-host 127.0.0.1 --turn-user juice_demo --turn-pass juice_password\n",
	        argv0, argv0);
}

int main(int argc, char *argv[])
{
	const char *signaling_host = SIGNAL_DEFAULT_HOST;
	const char *signaling_port = SIGNAL_DEFAULT_PORT;
	const char *turn_host = NULL;
	uint16_t turn_port = TURN_DEFAULT_PORT;
	const char *turn_user = NULL;
	const char *turn_pass = NULL;
	const char *room_id = "default";

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--signaling") == 0 && i + 1 < argc) {
			char *sep = strchr(argv[i + 1], ':');
			if (sep) {
				*sep = '\0';
				signaling_host = argv[i + 1];
				signaling_port = sep + 1;
			}
			i++;
		} else if (strcmp(argv[i], "--turn-host") == 0 && i + 1 < argc) {
			turn_host = argv[++i];
		} else if (strcmp(argv[i], "--turn-port") == 0 && i + 1 < argc) {
			turn_port = (uint16_t)atoi(argv[++i]);
		} else if (strcmp(argv[i], "--turn-user") == 0 && i + 1 < argc) {
			turn_user = argv[++i];
		} else if (strcmp(argv[i], "--turn-pass") == 0 && i + 1 < argc) {
			turn_pass = argv[++i];
		} else if (strcmp(argv[i], "--room") == 0 && i + 1 < argc) {
			room_id = argv[++i];
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			return 0;
		}
	}

	if (!turn_host) {
		fprintf(stderr, "Error: --turn-host is required\n");
		print_usage(argv[0]);
		return 1;
	}

#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		fprintf(stderr, "WSAStartup failed\n");
		return 1;
	}
#endif

	p2p_signal_ctx_t sig_ctx = { .sock = SIGNAL_INVALID_SOCKET, .mutex = NULL };
	printf("Connecting to signaling %s:%s...\n", signaling_host, signaling_port);
	if (p2p_signal_connect(&sig_ctx, signaling_host, signaling_port) < 0) {
		fprintf(stderr, "Failed to connect to signaling server\n");
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	/* Send join so server proceeds */
	{
		char join_msg[512];
		int n = snprintf(join_msg, sizeof(join_msg), "{\"type\":\"join\",\"room_id\":\"%s\"}\n", room_id);
#ifdef _WIN32
		send((SOCKET)sig_ctx.sock, join_msg, n, 0);
#else
		send(sig_ctx.sock, join_msg, (size_t)n, 0);
#endif
	}

	/* Wait for role message */
	char line[P2P_SIGNAL_BUFFER_SIZE];
	size_t len = 0;
	line[0] = '\0';
	while (len < sizeof(line) - 1) {
		char buf[256];
#ifdef _WIN32
		int n = recv((SOCKET)sig_ctx.sock, buf, sizeof(buf), 0);
#else
		ssize_t n = recv(sig_ctx.sock, buf, sizeof(buf), 0);
#endif
		if (n <= 0) {
			fprintf(stderr, "Failed to receive role\n");
#ifdef _WIN32
			closesocket(sig_ctx.sock);
			WSACleanup();
#else
			close(sig_ctx.sock);
#endif
			return 1;
		}
		for (int i = 0; i < (int)n && len < sizeof(line) - 1; i++) {
			if (buf[i] == '\n' || buf[i] == '\r') {
				line[len] = '\0';
				char type[64], sdp[P2P_JUICE_MAX_SDP_LEN];
				if (p2p_signal_parse_message(line, type, sizeof(type), sdp, sizeof(sdp)) == 0 &&
				    strcmp(type, "role") == 0) {
					p2p_run_config_t cfg = {
						.signal = &sig_ctx,
						.turn_host = turn_host,
						.turn_port = turn_port,
						.turn_user = turn_user,
						.turn_pass = turn_pass,
					};
					p2p_signal_parse_role(line, cfg.role, sizeof(cfg.role));
					printf("[Signal] Role: %s\n", cfg.role);
					fflush(stdout);
					juice_set_log_level(JUICE_LOG_LEVEL_DEBUG);
					p2p_run_peer(&cfg);
#ifdef _WIN32
					closesocket(sig_ctx.sock);
					WSACleanup();
#else
					close(sig_ctx.sock);
#endif
					return 0;
				}
				len = 0;
			} else {
				line[len++] = buf[i];
			}
		}
	}
	fprintf(stderr, "Invalid role message\n");
#ifdef _WIN32
	closesocket(sig_ctx.sock);
	WSACleanup();
#else
	close(sig_ctx.sock);
#endif
	return 1;
}
