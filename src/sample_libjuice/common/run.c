#include "common/run.h"
#include "common/signal.h"
#include "juice/juice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

static void on_state_changed(juice_agent_t *agent, juice_state_t state, void *user_ptr)
{
	p2p_run_config_t *cfg = (p2p_run_config_t *)user_ptr;
	printf("[ICE] State: %s\n", juice_state_to_string(state));
	fflush(stdout);
	if (state == JUICE_STATE_CONNECTED || state == JUICE_STATE_COMPLETED) {
		const char *msg = "Hello from libjuice peer!";
		juice_send(agent, msg, strlen(msg));
	}
}

static void on_candidate(juice_agent_t *agent, const char *sdp, void *user_ptr)
{
	p2p_run_config_t *cfg = (p2p_run_config_t *)user_ptr;
	printf("[ICE] Candidate: %s\n", sdp);
	p2p_signal_send(cfg->signal, "candidate", sdp);
}

static void on_gathering_done(juice_agent_t *agent, void *user_ptr)
{
	p2p_run_config_t *cfg = (p2p_run_config_t *)user_ptr;
	printf("[ICE] Gathering done\n");
	p2p_signal_send(cfg->signal, "gathering_done", NULL);
}

static void on_recv(juice_agent_t *agent, const char *data, size_t size, void *user_ptr)
{
	printf("[ICE] Received %zu bytes: ", size);
	fwrite(data, 1, size, stdout);
	printf("\n");
}

void p2p_run_peer(p2p_run_config_t *cfg)
{
	juice_config_t config;
	memset(&config, 0, sizeof(config));
	config.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
	config.stun_server_host = cfg->turn_host;
	config.stun_server_port = cfg->turn_port;
	config.cb_state_changed = on_state_changed;
	config.cb_candidate = on_candidate;
	config.cb_gathering_done = on_gathering_done;
	config.cb_recv = on_recv;
	config.user_ptr = cfg;

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

	char line[P2P_SIGNAL_BUFFER_SIZE];
	size_t len = 0;
	for (;;) {
		char buf[256];
#ifdef _WIN32
		int n = recv((SOCKET)cfg->signal->sock, buf, (int)sizeof(buf), 0);
#else
		ssize_t n = recv(cfg->signal->sock, buf, sizeof(buf), 0);
#endif
		if (n <= 0)
			break;
		for (ssize_t i = 0; i < n; i++) {
			if (buf[i] == '\n' || buf[i] == '\r') {
				if (len > 0) {
					line[len] = '\0';
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
					len = 0;
				}
			} else if (len < sizeof(line) - 1) {
				line[len++] = buf[i];
			}
		}
	}
	juice_destroy(agent);
}
