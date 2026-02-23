#ifndef XQUIC_ICE_H
#define XQUIC_ICE_H

#include <stddef.h>
#include <stdint.h>  /* uint64_t */

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

struct juice_agent;
typedef struct juice_agent juice_agent_t;

/* xquic uses struct xqc_engine_s; do not duplicate typedef to avoid conflict with xquic.h */
struct xqc_engine_s;

/**
 * ICE + xquic transport adapter context.
 * Used as user_data for write_socket and for engine; holds packet queue and wakeup fd.
 */
typedef struct xquic_ice_ctx {
	juice_agent_t *agent;
	struct xqc_engine_s *engine;

	int wakeup_read_fd;
	int wakeup_write_fd;

	void *queue_mutex;
	struct xquic_ice_packet *queue_head;
	struct xquic_ice_packet *queue_tail;
	size_t queue_len;
	size_t queue_max;

	struct sockaddr_storage local_addr;
	socklen_t local_len;
	struct sockaddr_storage peer_addr;
	socklen_t peer_len;

	/** Next wakeup time (microseconds) for poll timeout; set by set_event_timer callback */
	uint64_t next_wakeup_us;

	/** Optional: called from main loop after xqc_engine_main_logic (e.g. peer sends MOQ frames) */
	void (*post_main_logic)(struct xquic_ice_ctx *ctx);
	/** Optional app context (e.g. peer stores MOQ user_session here for post_main_logic) */
	void *app_ctx;
} xquic_ice_ctx_t;

/**
 * Initialize placeholder addresses (0.0.0.0:0 and 1.0.0.0:0) and queue.
 * wakeup_read_fd / wakeup_write_fd must be created by caller (e.g. pipe) and set
 * before calling xquic_ice_on_recv.
 */
void xquic_ice_ctx_init(xquic_ice_ctx_t *ctx, juice_agent_t *agent, struct xqc_engine_s *engine);

/**
 * Set wakeup pipe fds (read end for poll, write end for on_recv).
 */
void xquic_ice_ctx_set_wakeup_fds(xquic_ice_ctx_t *ctx, int read_fd, int write_fd);

/**
 * Cleanup: free queue packets and mutex. Does not close wakeup fds or destroy agent/engine.
 */
void xquic_ice_ctx_cleanup(xquic_ice_ctx_t *ctx);

/**
 * Write socket callback for xquic transport_cbs: send buf via juice_send(agent, buf, size).
 * conn_user_data must be xquic_ice_ctx_t*.
 */
ssize_t xquic_ice_write_socket(const unsigned char *buf, size_t size,
    const struct sockaddr *peer_addr, socklen_t peer_addrlen, void *conn_user_data);

/**
 * Write socket ex callback (multi-path); same as write_socket for single path over ICE.
 */
ssize_t xquic_ice_write_socket_ex(uint64_t path_id, const unsigned char *buf, size_t size,
    const struct sockaddr *peer_addr, socklen_t peer_addrlen, void *conn_user_data);

/**
 * Call from libjuice cb_recv: enqueue a copy of (data, size) and write 1 byte to wakeup fd.
 */
void xquic_ice_on_recv(juice_agent_t *agent, const char *data, size_t size, void *user_ptr);

/**
 * Call from main thread when wakeup fd is readable: drain queue, feed each packet to
 * xqc_engine_packet_process, then xqc_engine_finish_recv, then xqc_engine_main_logic.
 * Returns number of packets processed.
 */
int xquic_ice_drain_and_feed(xquic_ice_ctx_t *ctx);

/**
 * Read and discard one byte from wakeup fd (call after poll says readable, before drain).
 */
void xquic_ice_clear_wakeup(xquic_ice_ctx_t *ctx);

#endif /* XQUIC_ICE_H */
