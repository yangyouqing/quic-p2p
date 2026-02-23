#include "common/run.h"
#include "common/signal.h"
#include "juice/juice.h"
#ifndef P2P_HAVE_XQUIC
#define P2P_HAVE_XQUIC 0
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#if P2P_HAVE_XQUIC
#include "common/xquic_ice.h"
#include <xquic/xquic.h>
#include <xquic/xquic_typedef.h>
extern xqc_usec_t xqc_now(void);
#endif

/** User pointer for juice callbacks */
#if P2P_HAVE_XQUIC
typedef struct {
	p2p_run_config_t *cfg;
	xquic_ice_ctx_t *ice_ctx;
} p2p_run_user_t;
#else
typedef struct {
	p2p_run_config_t *cfg;
	void *ice_ctx; /* unused when xquic not built */
} p2p_run_user_t;
#endif

static void on_state_changed(juice_agent_t *agent, juice_state_t state, void *user_ptr)
{
	p2p_run_user_t *run_user = (p2p_run_user_t *)user_ptr;
	p2p_run_config_t *cfg = run_user->cfg;
	printf("[ICE] State: %s\n", juice_state_to_string(state));
	fflush(stdout);
	if (state == JUICE_STATE_CONNECTED || state == JUICE_STATE_COMPLETED) {
#if P2P_HAVE_XQUIC
		if (cfg->on_ice_connected && !run_user->ice_ctx) {
			run_user->ice_ctx = calloc(1, sizeof(xquic_ice_ctx_t));
			if (run_user->ice_ctx) {
				xquic_ice_ctx_init(run_user->ice_ctx, agent, NULL);
				cfg->on_ice_connected(cfg, agent, run_user->ice_ctx);
			}
		}
		if (!run_user->ice_ctx || !run_user->ice_ctx->engine) {
#endif
			const char *msg = "Hello from libjuice peer!";
			juice_send(agent, msg, strlen(msg));
#if P2P_HAVE_XQUIC
		}
#endif
	}
}

static void on_candidate(juice_agent_t *agent, const char *sdp, void *user_ptr)
{
	p2p_run_user_t *run_user = (p2p_run_user_t *)user_ptr;
	p2p_signal_send(run_user->cfg->signal, "candidate", sdp);
	printf("[ICE] Candidate: %s\n", sdp);
}

static void on_gathering_done(juice_agent_t *agent, void *user_ptr)
{
	p2p_run_user_t *run_user = (p2p_run_user_t *)user_ptr;
	printf("[ICE] Gathering done\n");
	p2p_signal_send(run_user->cfg->signal, "gathering_done", NULL);
}

static void on_recv(juice_agent_t *agent, const char *data, size_t size, void *user_ptr)
{
	p2p_run_user_t *run_user = (p2p_run_user_t *)user_ptr;
#if P2P_HAVE_XQUIC
	if (run_user->ice_ctx && run_user->ice_ctx->engine) {
		xquic_ice_on_recv(agent, data, size, run_user->ice_ctx);
		return;
	}
#endif
	printf("[ICE] Received %zu bytes: ", size);
	fwrite(data, 1, size, stdout);
	printf("\n");
}

static int get_signal_socket_fd(p2p_signal_ctx_t *signal)
{
#ifdef _WIN32
	return (int)(intptr_t)signal->sock;
#else
	return signal->sock;
#endif
}

static void process_signal_input(p2p_run_config_t *cfg, juice_agent_t *agent,
	char *line, size_t *len, char *buf, ssize_t n)
{
	for (ssize_t i = 0; i < n; i++) {
		if (buf[i] == '\n' || buf[i] == '\r') {
			if (*len > 0) {
				line[*len] = '\0';
				char type[64], sdp[P2P_JUICE_MAX_SDP_LEN];
				if (p2p_signal_parse_message(line, type, sizeof(type), sdp, sizeof(sdp)) == 0) {
					if (strcmp(type, "offer") == 0) {
						printf("[Signal] Received offer, sending answer...\n");
						fflush(stdout);
						juice_set_remote_description(agent, sdp);
						char local_sdp[JUICE_MAX_SDP_STRING_LEN];
						juice_get_local_description(agent, local_sdp, sizeof(local_sdp));
						p2p_signal_send(cfg->signal, "answer", local_sdp);
						juice_gather_candidates(agent);
					} else if (strcmp(type, "answer") == 0) {
						printf("[Signal] Received answer\n");
						fflush(stdout);
						juice_set_remote_description(agent, sdp);
						juice_gather_candidates(agent);
					} else if (strcmp(type, "candidate") == 0) {
						juice_add_remote_candidate(agent, sdp);
					} else if (strcmp(type, "gathering_done") == 0) {
						printf("[Signal] Remote gathering done\n");
						fflush(stdout);
						juice_set_remote_gathering_done(agent);
					}
				}
				*len = 0;
			}
		} else if (*len < P2P_SIGNAL_BUFFER_SIZE - 1) {
			line[(*len)++] = buf[i];
		}
	}
}

void p2p_run_peer(p2p_run_config_t *cfg)
{
	juice_config_t config;
	memset(&config, 0, sizeof(config));
	config.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
	config.stun_server_host = cfg->turn_host;
	config.stun_server_port = cfg->turn_port;

	p2p_run_user_t run_user = { .cfg = cfg, .ice_ctx = NULL };
	config.cb_state_changed = on_state_changed;
	config.cb_candidate = on_candidate;
	config.cb_gathering_done = on_gathering_done;
	config.cb_recv = on_recv;
	config.user_ptr = &run_user;

	juice_turn_server_t turn_server;
	if (cfg->turn_user && cfg->turn_pass) {
		memset(&turn_server, 0, sizeof(turn_server));
		turn_server.host = cfg->turn_host;
		turn_server.port = cfg->turn_port;
		turn_server.username = cfg->turn_user;
		turn_server.password = cfg->turn_pass;
		config.turn_servers = &turn_server;
		config.turn_servers_count = 1;
	}

	juice_agent_t *agent = juice_create(&config);
	if (!agent) {
		fprintf(stderr, "juice_create failed\n");
		return;
	}

	if (strcmp(cfg->role, "peer1") == 0) {
		printf("[ICE] Sending offer...\n");
		fflush(stdout);
		char sdp[JUICE_MAX_SDP_STRING_LEN];
		if (juice_get_local_description(agent, sdp, sizeof(sdp)) != 0) {
			fprintf(stderr, "juice_get_local_description failed\n");
			juice_destroy(agent);
			return;
		}
		p2p_signal_send(cfg->signal, "offer", sdp);
	} else {
		printf("[ICE] Waiting for offer from peer1...\n");
		fflush(stdout);
	}

	int signal_fd = get_signal_socket_fd(cfg->signal);
	char line[P2P_SIGNAL_BUFFER_SIZE];
	size_t len = 0;

#if !P2P_HAVE_XQUIC
	/* Simple blocking recv loop when xquic not built */
	for (;;) {
		char buf[256];
#ifdef _WIN32
		int n = recv((SOCKET)cfg->signal->sock, buf, (int)sizeof(buf), 0);
#else
		ssize_t n = recv(cfg->signal->sock, buf, sizeof(buf), 0);
#endif
		if (n <= 0)
			break;
		process_signal_input(cfg, agent, line, &len, buf, n);
	}
#else
	/* Poll loop: signal fd + wakeup fd + xquic timer */
	for (;;) {
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET((unsigned)signal_fd, &read_fds);
		int max_fd = signal_fd;
		if (run_user.ice_ctx && run_user.ice_ctx->wakeup_read_fd >= 0) {
			FD_SET((unsigned)run_user.ice_ctx->wakeup_read_fd, &read_fds);
			if (run_user.ice_ctx->wakeup_read_fd > max_fd)
				max_fd = run_user.ice_ctx->wakeup_read_fd;
		}

		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		if (run_user.ice_ctx && run_user.ice_ctx->engine && run_user.ice_ctx->next_wakeup_us) {
			uint64_t now_us = xqc_now();
			uint64_t diff_us = run_user.ice_ctx->next_wakeup_us > now_us
				? run_user.ice_ctx->next_wakeup_us - now_us
				: 0;
			tv.tv_sec = (long)(diff_us / 1000000);
			tv.tv_usec = (long)(diff_us % 1000000);
			if (tv.tv_sec == 0 && tv.tv_usec == 0)
				tv.tv_usec = 10000;
		}

		int nready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
		if (nready < 0) {
			perror("select");
			break;
		}

		if (run_user.ice_ctx && run_user.ice_ctx->engine) {
			if (nready == 0 || (run_user.ice_ctx->wakeup_read_fd >= 0 &&
			    FD_ISSET((unsigned)run_user.ice_ctx->wakeup_read_fd, &read_fds))) {
				if (run_user.ice_ctx->wakeup_read_fd >= 0)
					xquic_ice_clear_wakeup(run_user.ice_ctx);
				xquic_ice_drain_and_feed(run_user.ice_ctx);
			}
			if (nready == 0)
				xqc_engine_main_logic(run_user.ice_ctx->engine);
			if (run_user.ice_ctx->post_main_logic)
				run_user.ice_ctx->post_main_logic(run_user.ice_ctx);
		}

		if (!FD_ISSET((unsigned)signal_fd, &read_fds))
			continue;

		char buf[256];
		ssize_t n;
#ifdef _WIN32
		n = recv((SOCKET)cfg->signal->sock, buf, (int)sizeof(buf), 0);
#else
		n = recv(cfg->signal->sock, buf, sizeof(buf), 0);
#endif
		if (n <= 0)
			break;
		process_signal_input(cfg, agent, line, &len, buf, n);
	}

#if P2P_HAVE_XQUIC
	if (run_user.ice_ctx) {
		xquic_ice_ctx_cleanup(run_user.ice_ctx);
		free(run_user.ice_ctx);
	}
#endif
#endif

	juice_destroy(agent);
}
