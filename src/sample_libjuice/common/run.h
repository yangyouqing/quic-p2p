#ifndef P2P_RUN_H
#define P2P_RUN_H

#include "common/signal.h"

typedef struct p2p_run_config {
	p2p_signal_ctx_t *signal;
	char role[16];
	const char *turn_host;
	uint16_t turn_port;
	const char *turn_user;
	const char *turn_pass;
} p2p_run_config_t;

/* Run the P2P peer loop (ICE + signaling). Blocks until disconnect. */
void p2p_run_peer(p2p_run_config_t *cfg);

#endif
