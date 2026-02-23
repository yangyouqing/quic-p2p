#ifndef P2P_SIGNAL_H
#define P2P_SIGNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

typedef int signal_sock_t;
#define SIGNAL_INVALID_SOCKET (-1)

#define P2P_SIGNAL_BUFFER_SIZE 16384
#define P2P_JUICE_MAX_SDP_LEN  4096

typedef struct p2p_signal_ctx {
	signal_sock_t sock;
	void *mutex;
} p2p_signal_ctx_t;

int p2p_signal_connect(p2p_signal_ctx_t *ctx, const char *host, const char *port);
int p2p_signal_send(p2p_signal_ctx_t *ctx, const char *type, const char *sdp);
int p2p_signal_parse_message(const char *json, char *type_out, size_t type_size,
                             char *sdp_out, size_t sdp_size);
void p2p_signal_parse_role(const char *json, char *role_out, size_t role_size);

#endif
