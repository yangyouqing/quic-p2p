#include "common/signal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#define close_sock closesocket
#define sock_errno WSAGetLastError()
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#define close_sock close
#define sock_errno errno
#endif

#ifdef __has_include
#if __has_include(<pthread.h>)
#include <pthread.h>
#define HAVE_PTHREAD 1
#endif
#endif

int p2p_signal_connect(p2p_signal_ctx_t *ctx, const char *host, const char *port)
{
	struct addrinfo hints, *res = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int err = getaddrinfo(host, port, &hints, &res);
	if (err) {
#ifdef EAI_SYSTEM
		fprintf(stderr, "getaddrinfo: %s\n", err == EAI_SYSTEM ? strerror(errno) : gai_strerror(err));
#else
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
#endif
		return -1;
	}
	signal_sock_t sock = (signal_sock_t)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock == SIGNAL_INVALID_SOCKET) {
		fprintf(stderr, "socket: %d\n", (int)sock_errno);
		freeaddrinfo(res);
		return -1;
	}
	if (connect(sock, res->ai_addr, (socklen_t)res->ai_addrlen) < 0) {
		fprintf(stderr, "connect: %d\n", (int)sock_errno);
		close_sock(sock);
		freeaddrinfo(res);
		return -1;
	}
	freeaddrinfo(res);
	ctx->sock = sock;
	return 0;
}

int p2p_signal_send(p2p_signal_ctx_t *ctx, const char *type, const char *sdp)
{
#ifdef HAVE_PTHREAD
	if (ctx->mutex)
		pthread_mutex_lock((pthread_mutex_t *)ctx->mutex);
#endif
	char msg[P2P_SIGNAL_BUFFER_SIZE];
	int len;
	if (sdp && sdp[0]) {
		len = snprintf(msg, sizeof(msg), "{\"type\":\"%s\",\"sdp\":\"", type);
		for (const char *s = sdp; *s && len < (int)sizeof(msg) - 4; s++) {
			if (*s == '\r')
				continue;
			if (*s == '\n') {
				len += snprintf(msg + len, sizeof(msg) - (size_t)len, "\\n");
			} else if (*s == '"' || *s == '\\') {
				msg[len++] = '\\';
				msg[len++] = *s;
			} else {
				msg[len++] = *s;
			}
		}
		len += snprintf(msg + len, sizeof(msg) - (size_t)len, "\"}\n");
	} else {
		len = snprintf(msg, sizeof(msg), "{\"type\":\"%s\"}\n", type);
	}
#ifdef _WIN32
	int ret = (int)send((SOCKET)ctx->sock, msg, len, 0);
#else
	int ret = (int)send(ctx->sock, msg, (size_t)len, 0);
#endif
#ifdef HAVE_PTHREAD
	if (ctx->mutex)
		pthread_mutex_unlock((pthread_mutex_t *)ctx->mutex);
#endif
	return ret == len ? 0 : -1;
}

int p2p_signal_parse_message(const char *json, char *type_out, size_t type_size,
                             char *sdp_out, size_t sdp_size)
{
	const char *p;
	if ((p = strstr(json, "\"type\"")) == NULL)
		return -1;
	p += 6;
	while (*p && (*p == ' ' || *p == ':' || *p == '\t'))
		p++;
	if (*p != '"')
		return -1;
	p++;
	size_t i = 0;
	while (*p && *p != '"' && i < type_size - 1)
		type_out[i++] = *p++;
	type_out[i] = '\0';

	if (sdp_out && sdp_size > 0)
		sdp_out[0] = '\0';
	if ((p = strstr(json, "\"sdp\"")) == NULL)
		return 0;
	p += 5;
	while (*p && (*p == ' ' || *p == ':' || *p == '\t'))
		p++;
	if (*p != '"')
		return 0;
	p++;
	i = 0;
	while (*p && i < sdp_size - 1) {
		if (*p == '\\' && *(p + 1) == 'n') {
			p += 2;
			sdp_out[i++] = '\n';
			continue;
		}
		if (*p == '\\' && *(p + 1) == '"') {
			p += 2;
			sdp_out[i++] = '"';
			continue;
		}
		if (*p == '"')
			break;
		sdp_out[i++] = *p++;
	}
	sdp_out[i] = '\0';
	return 0;
}

void p2p_signal_parse_role(const char *json, char *role_out, size_t role_size)
{
	const char *p = strstr(json, "\"role\"");
	if (!p)
		return;
	p += 6;
	while (*p && (*p == ' ' || *p == ':' || *p == '\t'))
		p++;
	if (*p != '"')
		return;
	p++;
	size_t i = 0;
	while (*p && *p != '"' && i < role_size - 1)
		role_out[i++] = *p++;
	role_out[i] = '\0';
}
