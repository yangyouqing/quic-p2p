/**
 * P2P Client: joins second, gets role peer1 (offerer). QUIC client + MOQ Subscriber when xquic built.
 */
#include "common/signal.h"
#include "common/run.h"
#include "juice/juice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifndef P2P_HAVE_XQUIC
#define P2P_HAVE_XQUIC 0
#endif
#if P2P_HAVE_XQUIC
#include "common/xquic_ice.h"
#include <xquic/xquic.h>
#include <xquic/xquic_typedef.h>
#include <moq/xqc_moq.h>
#endif

#define SIGNAL_DEFAULT_HOST "localhost"
#define SIGNAL_DEFAULT_PORT "8888"
#define TURN_DEFAULT_PORT 3478
#define MOQ_SERVER_NAME "p2p-moq"

#if P2P_HAVE_XQUIC
static void client_on_ice_connected(p2p_run_config_t *cfg, void *agent_ptr, void *ice_ctx_ptr);
#endif

static void print_usage(const char *argv0)
{
	fprintf(stderr,
	        "Usage: %s [options]\n"
	        "  --signaling <host:port>  Signaling server (default: localhost:8888)\n"
	        "  --turn-host <host>       Coturn host (required)\n"
	        "  --turn-port <port>       Coturn port (default: 3478)\n"
	        "  --turn-user <user>       TURN username\n"
	        "  --turn-pass <pass>       TURN password\n"
	        "  --room <id>              Room ID (default: default)\n"
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

	char join_msg[512];
	int n = snprintf(join_msg, sizeof(join_msg), "{\"type\":\"join\",\"room_id\":\"%s\"}\n", room_id);
#ifdef _WIN32
	send((SOCKET)sig_ctx.sock, join_msg, n, 0);
#else
	send(sig_ctx.sock, join_msg, (size_t)n, 0);
#endif

	char line[P2P_SIGNAL_BUFFER_SIZE];
	size_t len = 0;
	line[0] = '\0';
	while (len < sizeof(line) - 1) {
		char buf[256];
#ifdef _WIN32
		int nr = recv((SOCKET)sig_ctx.sock, buf, sizeof(buf), 0);
#else
		ssize_t nr = recv(sig_ctx.sock, buf, sizeof(buf), 0);
#endif
		if (nr <= 0) {
			fprintf(stderr, "Failed to receive role\n");
#ifdef _WIN32
			closesocket(sig_ctx.sock);
			WSACleanup();
#else
			close(sig_ctx.sock);
#endif
			return 1;
		}
		for (int i = 0; i < (int)nr && len < sizeof(line) - 1; i++) {
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
						.on_ice_connected = NULL,
					};
					p2p_signal_parse_role(line, cfg.role, sizeof(cfg.role));
					printf("[Signal] Role: %s\n", cfg.role);
					fflush(stdout);
#if P2P_HAVE_XQUIC
					cfg.on_ice_connected = client_on_ice_connected;
#endif
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

#if P2P_HAVE_XQUIC
/* Client connection context: first field must be ice_ctx for write_socket callback. */
typedef struct {
	xquic_ice_ctx_t *ice_ctx;
	xqc_cid_t cid;
} client_moq_conn_t;

static ssize_t client_write_socket(const unsigned char *buf, size_t size,
    const struct sockaddr *peer_addr, socklen_t peer_addrlen, void *conn_user_data)
{
	xqc_moq_user_session_t *us = (xqc_moq_user_session_t *)conn_user_data;
	xquic_ice_ctx_t *ctx = ((client_moq_conn_t *)us->data)->ice_ctx;
	return xquic_ice_write_socket(buf, size, peer_addr, peer_addrlen, ctx);
}

static ssize_t client_write_socket_ex(uint64_t path_id, const unsigned char *buf, size_t size,
    const struct sockaddr *peer_addr, socklen_t peer_addrlen, void *conn_user_data)
{
	(void)path_id;
	return client_write_socket(buf, size, peer_addr, peer_addrlen, conn_user_data);
}

static void client_set_event_timer(xqc_usec_t wake_after, void *engine_user_data)
{
	xquic_ice_ctx_t *ctx = (xquic_ice_ctx_t *)engine_user_data;
	extern xqc_usec_t xqc_now(void);
	ctx->next_wakeup_us = xqc_now() + wake_after;
}

static void client_log_write(xqc_log_level_t lvl, const void *buf, size_t size, void *engine_user_data)
{
	(void)engine_user_data;
	(void)lvl;
	if (size > 0)
		fwrite(buf, 1, size, stderr);
}

/* MOQ Subscriber callbacks */
static void on_session_setup(xqc_moq_user_session_t *user_session, char *extdata)
{
	(void)user_session;
	if (extdata) printf("[MOQ] session_setup extdata: %s\n", extdata);
}

static void on_datachannel(xqc_moq_user_session_t *user_session) { (void)user_session; }
static void on_datachannel_msg(xqc_moq_user_session_t *user_session, uint8_t *msg, size_t msg_len)
{
	(void)user_session; (void)msg; (void)msg_len;
}

static void on_subscribe(xqc_moq_user_session_t *user_session, uint64_t subscribe_id,
    xqc_moq_track_t *track, xqc_moq_subscribe_msg_t *msg)
{
	(void)user_session; (void)subscribe_id; (void)track; (void)msg;
}

static void on_request_keyframe(xqc_moq_user_session_t *user_session, uint64_t subscribe_id,
    xqc_moq_track_t *track)
{
	(void)user_session; (void)subscribe_id; (void)track;
}

static void on_bitrate_change(xqc_moq_user_session_t *user_session, uint64_t bitrate)
{
	(void)user_session; (void)bitrate;
}

static void on_subscribe_ok(xqc_moq_user_session_t *user_session, xqc_moq_subscribe_ok_msg_t *ok)
{
	(void)user_session;
	printf("[MOQ] subscribe_ok id=%"PRIu64"\n", (uint64_t)ok->subscribe_id);
}

static void on_subscribe_error(xqc_moq_user_session_t *user_session, xqc_moq_subscribe_error_msg_t *err)
{
	(void)user_session;
	printf("[MOQ] subscribe_error id=%"PRIu64" code=%"PRIu64"\n", (uint64_t)err->subscribe_id, (uint64_t)err->error_code);
}

static void on_catalog(xqc_moq_user_session_t *user_session, xqc_moq_track_info_t **track_info_array, xqc_int_t array_size)
{
	xqc_moq_session_t *session = user_session->session;
	for (xqc_int_t i = 0; i < array_size; i++) {
		xqc_moq_track_info_t *info = track_info_array[i];
		printf("[MOQ] catalog track %s/%s\n", info->track_namespace, info->track_name);
		if (xqc_moq_subscribe_latest(session, info->track_namespace, info->track_name) < 0)
			printf("[MOQ] subscribe_latest failed\n");
	}
}

static void on_video_frame(xqc_moq_user_session_t *user_session, uint64_t subscribe_id, xqc_moq_video_frame_t *frame)
{
	(void)user_session;
	printf("[MOQ] video subscribe_id=%"PRIu64" seq=%"PRIu64" len=%"PRIu64"\n", subscribe_id, (uint64_t)frame->seq_num, (uint64_t)frame->video_len);
}

static void on_audio_frame(xqc_moq_user_session_t *user_session, uint64_t subscribe_id, xqc_moq_audio_frame_t *frame)
{
	(void)user_session;
	printf("[MOQ] audio subscribe_id=%"PRIu64" seq=%"PRIu64" len=%"PRIu64"\n", subscribe_id, (uint64_t)frame->seq_num, (uint64_t)frame->audio_len);
}

static int client_conn_create_notify(xqc_connection_t *conn, const xqc_cid_t *cid, void *user_data, void *conn_proto_data)
{
	xqc_moq_user_session_t *user_session = (xqc_moq_user_session_t *)user_data;
	xqc_moq_session_callbacks_t callbacks = {
		.on_session_setup = on_session_setup,
		.on_datachannel = on_datachannel,
		.on_datachannel_msg = on_datachannel_msg,
		.on_subscribe = on_subscribe,
		.on_request_keyframe = on_request_keyframe,
		.on_bitrate_change = on_bitrate_change,
		.on_subscribe_ok = on_subscribe_ok,
		.on_subscribe_error = on_subscribe_error,
		.on_catalog = on_catalog,
		.on_video = on_video_frame,
		.on_audio = on_audio_frame,
	};
	xqc_moq_session_t *session = xqc_moq_session_create(conn, user_session, XQC_MOQ_TRANSPORT_QUIC, XQC_MOQ_SUBSCRIBER, callbacks, NULL);
	if (!session) return -1;
	xqc_moq_configure_bitrate(session, 1000000, 8000000, 1000000);
	return 0;
}

static int client_conn_close_notify(xqc_connection_t *conn, const xqc_cid_t *cid, void *user_data, void *conn_proto_data)
{
	xqc_moq_user_session_t *user_session = (xqc_moq_user_session_t *)user_data;
	if (user_session->session)
		xqc_moq_session_destroy(user_session->session);
	free(user_data);
	return 0;
}

static void client_conn_handshake_finished(xqc_connection_t *conn, void *user_data, void *conn_proto_data)
{
	(void)conn; (void)user_data; (void)conn_proto_data;
	printf("[QUIC] handshake finished\n");
}

static void client_on_ice_connected(p2p_run_config_t *cfg, void *agent_ptr, void *ice_ctx_ptr)
{
	(void)cfg;
	(void)agent_ptr;
	xquic_ice_ctx_t *ice_ctx = (xquic_ice_ctx_t *)ice_ctx_ptr;

	int pipe_fds[2];
#ifdef _WIN32
	/* Windows: use _pipe or socketpair; minimal use _pipe */
	if (_pipe(pipe_fds, 256, _O_BINARY) != 0) {
		fprintf(stderr, "pipe failed\n");
		return;
	}
#else
	if (pipe(pipe_fds) != 0) {
		fprintf(stderr, "pipe failed\n");
		return;
	}
#endif
	xquic_ice_ctx_set_wakeup_fds(ice_ctx, pipe_fds[0], pipe_fds[1]);

	xqc_config_t config;
	if (xqc_engine_get_default_config(&config, XQC_ENGINE_CLIENT) < 0) {
		fprintf(stderr, "xqc_engine_get_default_config failed\n");
		return;
	}
	config.cfg_log_level = XQC_LOG_DEBUG;

	xqc_engine_ssl_config_t ssl_config;
	memset(&ssl_config, 0, sizeof(ssl_config));
	ssl_config.ciphers = XQC_TLS_CIPHERS;
	ssl_config.groups = XQC_TLS_GROUPS;

	xqc_engine_callback_t engine_cb = {
		.set_event_timer = client_set_event_timer,
		.log_callbacks = {
			.xqc_log_write_err = client_log_write,
			.xqc_log_write_stat = client_log_write,
		},
	};

	xqc_transport_callbacks_t tcbs = {
		.write_socket = client_write_socket,
		.write_socket_ex = client_write_socket_ex,
	};

	xqc_engine_t *engine = xqc_engine_create(XQC_ENGINE_CLIENT, &config, &ssl_config, &engine_cb, &tcbs, ice_ctx);
	if (!engine) {
		fprintf(stderr, "xqc_engine_create failed\n");
		return;
	}

	xqc_conn_callbacks_t conn_cbs = {
		.conn_create_notify = client_conn_create_notify,
		.conn_close_notify = client_conn_close_notify,
		.conn_handshake_finished = client_conn_handshake_finished,
	};
	xqc_moq_init_alpn(engine, &conn_cbs, XQC_MOQ_TRANSPORT_QUIC);

	xqc_moq_user_session_t *user_session = (xqc_moq_user_session_t *)calloc(1, sizeof(xqc_moq_user_session_t) + sizeof(client_moq_conn_t));
	if (!user_session) {
		xqc_engine_destroy(engine);
		return;
	}
	((client_moq_conn_t *)user_session->data)->ice_ctx = ice_ctx;

	xqc_conn_settings_t conn_settings;
	memset(&conn_settings, 0, sizeof(conn_settings));
	conn_settings.cong_ctrl_callback = xqc_bbr_cb;

	xqc_conn_ssl_config_t conn_ssl;
	memset(&conn_ssl, 0, sizeof(conn_ssl));
	conn_ssl.cert_verify_flag |= XQC_TLS_CERT_FLAG_ALLOW_SELF_SIGNED;

	const xqc_cid_t *cid = xqc_connect(engine, &conn_settings, NULL, 0, MOQ_SERVER_NAME, 0,
	    &conn_ssl, (struct sockaddr *)&ice_ctx->peer_addr, ice_ctx->peer_len, XQC_ALPN_MOQ_QUIC, user_session);
	if (!cid) {
		fprintf(stderr, "xqc_connect failed\n");
		free(user_session);
		xqc_engine_destroy(engine);
		return;
	}
	((client_moq_conn_t *)user_session->data)->cid = *cid;

	ice_ctx->engine = engine;
	printf("[QUIC] connecting to %s over ICE (MOQ Subscriber)\n", MOQ_SERVER_NAME);
}
#endif
