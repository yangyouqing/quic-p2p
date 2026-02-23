#include "common/xquic_ice.h"
#include "juice/juice.h"
#include <xquic/xquic.h>
#include <xquic/xquic_typedef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* xqc_now is implemented in xquic (xqc_time.c), not in public header */
extern xqc_usec_t xqc_now(void);

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE
#endif
#include <io.h>
#define read _read
#define write _write
#else
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#endif

#define XQUIC_ICE_PACKET_MAX 1500
#define XQUIC_ICE_QUEUE_MAX  256

struct xquic_ice_packet {
	unsigned char *data;
	size_t size;
	struct xquic_ice_packet *next;
};

#if defined(_WIN32) || defined(_WIN64)
/* Windows: use CRITICAL_SECTION for simplicity (no pthread dependency) */
#include <windows.h>
typedef CRITICAL_SECTION xquic_ice_mutex_t;
static void mutex_init(xquic_ice_mutex_t *m) { InitializeCriticalSection(m); }
static void mutex_lock(xquic_ice_mutex_t *m) { EnterCriticalSection(m); }
static void mutex_unlock(xquic_ice_mutex_t *m) { LeaveCriticalSection(m); }
static void mutex_destroy(xquic_ice_mutex_t *m) { DeleteCriticalSection(m); }
#else
typedef pthread_mutex_t xquic_ice_mutex_t;
static void mutex_init(xquic_ice_mutex_t *m) { pthread_mutex_init(m, NULL); }
static void mutex_lock(xquic_ice_mutex_t *m) { pthread_mutex_lock(m); }
static void mutex_unlock(xquic_ice_mutex_t *m) { pthread_mutex_unlock(m); }
static void mutex_destroy(xquic_ice_mutex_t *m) { pthread_mutex_destroy(m); }
#endif

static void set_placeholder_addrs(xquic_ice_ctx_t *ctx)
{
	struct sockaddr_in *local = (struct sockaddr_in *)&ctx->local_addr;
	struct sockaddr_in *peer = (struct sockaddr_in *)&ctx->peer_addr;
	memset(&ctx->local_addr, 0, sizeof(ctx->local_addr));
	memset(&ctx->peer_addr, 0, sizeof(ctx->peer_addr));
	local->sin_family = AF_INET;
	local->sin_addr.s_addr = htonl(INADDR_ANY);
	local->sin_port = 0;
	ctx->local_len = sizeof(struct sockaddr_in);
	peer->sin_family = AF_INET;
	peer->sin_addr.s_addr = htonl(0x01000000); /* 1.0.0.0 */
	peer->sin_port = htons(1);
	ctx->peer_len = sizeof(struct sockaddr_in);
}

void xquic_ice_ctx_init(xquic_ice_ctx_t *ctx, juice_agent_t *agent, xqc_engine_t *engine)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->agent = agent;
	ctx->engine = engine;
	ctx->wakeup_read_fd = -1;
	ctx->wakeup_write_fd = -1;
	ctx->queue_max = XQUIC_ICE_QUEUE_MAX;
	ctx->next_wakeup_us = 0;
	set_placeholder_addrs(ctx);
	ctx->queue_mutex = malloc(sizeof(xquic_ice_mutex_t));
	if (ctx->queue_mutex)
		mutex_init((xquic_ice_mutex_t *)ctx->queue_mutex);
}

void xquic_ice_ctx_set_wakeup_fds(xquic_ice_ctx_t *ctx, int read_fd, int write_fd)
{
	ctx->wakeup_read_fd = read_fd;
	ctx->wakeup_write_fd = write_fd;
}

static void free_packet(struct xquic_ice_packet *p)
{
	if (!p) return;
	free(p->data);
	free(p);
}

void xquic_ice_ctx_cleanup(xquic_ice_ctx_t *ctx)
{
	xquic_ice_mutex_t *mu = (xquic_ice_mutex_t *)ctx->queue_mutex;
	if (mu) {
		mutex_lock(mu);
		while (ctx->queue_head) {
			struct xquic_ice_packet *p = ctx->queue_head;
			ctx->queue_head = p->next;
			free_packet(p);
		}
		ctx->queue_tail = NULL;
		ctx->queue_len = 0;
		mutex_unlock(mu);
		mutex_destroy(mu);
		free(ctx->queue_mutex);
		ctx->queue_mutex = NULL;
	}
}

ssize_t xquic_ice_write_socket(const unsigned char *buf, size_t size,
    const struct sockaddr *peer_addr, socklen_t peer_addrlen, void *conn_user_data)
{
	(void)peer_addr;
	(void)peer_addrlen;
	xquic_ice_ctx_t *ctx = (xquic_ice_ctx_t *)conn_user_data;
	if (!ctx || !ctx->agent) return XQC_SOCKET_ERROR;
	/* juice_send takes (agent, data, size); data is const char* */
	int ret = juice_send(ctx->agent, (const char *)buf, size);
	if (ret != 0) {
		if (ret == JUICE_ERR_AGAIN) return XQC_SOCKET_EAGAIN;
		return XQC_SOCKET_ERROR;
	}
	return (ssize_t)size;
}

ssize_t xquic_ice_write_socket_ex(uint64_t path_id, const unsigned char *buf, size_t size,
    const struct sockaddr *peer_addr, socklen_t peer_addrlen, void *conn_user_data)
{
	(void)path_id;
	return xquic_ice_write_socket(buf, size, peer_addr, peer_addrlen, conn_user_data);
}

void xquic_ice_on_recv(juice_agent_t *agent, const char *data, size_t size, void *user_ptr)
{
	xquic_ice_ctx_t *ctx = (xquic_ice_ctx_t *)user_ptr;
	if (!ctx || !data || size == 0 || size > XQUIC_ICE_PACKET_MAX) return;

	struct xquic_ice_packet *p = malloc(sizeof(*p));
	if (!p) return;
	p->data = malloc(size);
	if (!p->data) { free(p); return; }
	memcpy(p->data, data, size);
	p->size = size;
	p->next = NULL;

	xquic_ice_mutex_t *mu = (xquic_ice_mutex_t *)ctx->queue_mutex;
	if (!mu) { free(p->data); free(p); return; }

	mutex_lock(mu);
	/* Drop oldest if queue full */
	while (ctx->queue_len >= ctx->queue_max && ctx->queue_head) {
		struct xquic_ice_packet *old = ctx->queue_head;
		ctx->queue_head = old->next;
		if (!ctx->queue_head) ctx->queue_tail = NULL;
		ctx->queue_len--;
		free_packet(old);
	}
	if (ctx->queue_len >= ctx->queue_max) {
		mutex_unlock(mu);
		free_packet(p);
		return;
	}
	if (ctx->queue_tail)
		ctx->queue_tail->next = p;
	else
		ctx->queue_head = p;
	ctx->queue_tail = p;
	ctx->queue_len++;
	mutex_unlock(mu);

	if (ctx->wakeup_write_fd >= 0) {
		char b = 1;
		(void)write(ctx->wakeup_write_fd, &b, 1);
	}
	(void)agent;
}

void xquic_ice_clear_wakeup(xquic_ice_ctx_t *ctx)
{
	if (ctx->wakeup_read_fd < 0) return;
	char buf[64];
	while (read(ctx->wakeup_read_fd, buf, sizeof(buf)) > 0) {}
}

int xquic_ice_drain_and_feed(xquic_ice_ctx_t *ctx)
{
	xquic_ice_mutex_t *mu = (xquic_ice_mutex_t *)ctx->queue_mutex;
	if (!mu || !ctx->engine) return 0;

	int n = 0;
	for (;;) {
		struct xquic_ice_packet *p = NULL;
		mutex_lock(mu);
		p = ctx->queue_head;
		if (p) {
			ctx->queue_head = p->next;
			if (!ctx->queue_head) ctx->queue_tail = NULL;
			ctx->queue_len--;
		}
		mutex_unlock(mu);
		if (!p) break;

		uint64_t recv_time = xqc_now();
		xqc_int_t ret = xqc_engine_packet_process(ctx->engine,
		    p->data, p->size,
		    (struct sockaddr *)&ctx->local_addr, ctx->local_len,
		    (struct sockaddr *)&ctx->peer_addr, ctx->peer_len,
		    recv_time, ctx);
		free_packet(p);
		if (ret != XQC_OK) continue;
		n++;
	}
	if (n > 0) {
		xqc_engine_finish_recv(ctx->engine);
		xqc_engine_main_logic(ctx->engine);
	}
	return n;
}
