#ifndef P2P_RUN_H
#define P2P_RUN_H

#include "common/signal.h"

struct p2p_run_config;
/** Called when ICE reaches CONNECTED/COMPLETED. Implementor creates xquic engine, pipe, MOQ, etc. */
typedef void (*p2p_on_ice_connected_t)(struct p2p_run_config *cfg, void *agent, void *ice_ctx);

typedef struct p2p_run_config {
	p2p_signal_ctx_t *signal;
	char role[16];
	const char *turn_host;
	uint16_t turn_port;
	const char *turn_user;
	const char *turn_pass;
	p2p_on_ice_connected_t on_ice_connected;
} p2p_run_config_t;

/* Run the P2P peer loop (ICE + signaling). Blocks until disconnect. */
void p2p_run_peer(p2p_run_config_t *cfg);

#endif
