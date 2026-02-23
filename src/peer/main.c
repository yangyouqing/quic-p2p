/**
 * P2P Peer: joins first, gets role peer2 (answerer). QUIC server + MOQ Publisher when xquic built.
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
#ifdef _WIN32
#include <fcntl.h>
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

#if P2P_HAVE_XQUIC
static void peer_on_ice_connected(p2p_run_config_t *cfg, void *agent_ptr, void *ice_ctx_ptr);
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
					cfg.on_ice_connected = peer_on_ice_connected;
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
/* Peer connection context: first field ice_ctx for write_socket; rest for MOQ Publisher. */
typedef struct {
	xquic_ice_ctx_t *ice_ctx;
	xqc_moq_session_t *moq_session;
	xqc_moq_track_t *video_track;
	xqc_moq_track_t *audio_track;
	uint64_t video_subscribe_id;
	uint64_t audio_subscribe_id;
	uint64_t video_seq;
	uint64_t audio_seq;
	uint64_t next_frame_us;
} peer_moq_conn_t;

static ssize_t peer_write_socket(const unsigned char *buf, size_t size,
    const struct sockaddr *peer_addr, socklen_t peer_addrlen, void *conn_user_data)
{
	xqc_moq_user_session_t *us = (xqc_moq_user_session_t *)conn_user_data;
	xquic_ice_ctx_t *ctx = ((peer_moq_conn_t *)us->data)->ice_ctx;
	return xquic_ice_write_socket(buf, size, peer_addr, peer_addrlen, ctx);
}

static ssize_t peer_write_socket_ex(uint64_t path_id, const unsigned char *buf, size_t size,
    const struct sockaddr *peer_addr, socklen_t peer_addrlen, void *conn_user_data)
{
	(void)path_id;
	return peer_write_socket(buf, size, peer_addr, peer_addrlen, conn_user_data);
}

static ssize_t peer_stateless_reset(const unsigned char *buf, size_t size,
    const struct sockaddr *peer_addr, socklen_t peer_addrlen,
    const struct sockaddr *local_addr, socklen_t local_addrlen, void *user_data)
{
	xquic_ice_ctx_t *ctx = (xquic_ice_ctx_t *)user_data;
	(void)local_addr;
	(void)local_addrlen;
	return xquic_ice_write_socket(buf, size, peer_addr, peer_addrlen, ctx);
}

static void peer_set_event_timer(xqc_usec_t wake_after, void *engine_user_data)
{
	xquic_ice_ctx_t *ctx = (xquic_ice_ctx_t *)engine_user_data;
	extern xqc_usec_t xqc_now(void);
	ctx->next_wakeup_us = xqc_now() + wake_after;
}

static void peer_log_write(xqc_log_level_t lvl, const void *buf, size_t size, void *engine_user_data)
{
	(void)engine_user_data;
	(void)lvl;
	if (size > 0)
		fwrite(buf, 1, size, stderr);
}

/* MOQ Publisher callbacks */
static void on_session_setup(xqc_moq_user_session_t *user_session, char *extdata)
{
	(void)extdata;
	xqc_moq_session_t *session = user_session->session;
	peer_moq_conn_t *pc = (peer_moq_conn_t *)user_session->data;
	pc->moq_session = session;
	pc->video_subscribe_id = (uint64_t)-1;
	pc->audio_subscribe_id = (uint64_t)-1;

	xqc_moq_selection_params_t vp;
	memset(&vp, 0, sizeof(vp));
	vp.codec = "av01";
	vp.mime_type = "video/mp4";
	vp.bitrate = 1000000;
	vp.framerate = 30;
	vp.width = 720;
	vp.height = 720;
	pc->video_track = xqc_moq_track_create(session, "namespace", "video", XQC_MOQ_TRACK_VIDEO, &vp, XQC_MOQ_CONTAINER_LOC, XQC_MOQ_TRACK_FOR_PUB);

	xqc_moq_selection_params_t ap;
	memset(&ap, 0, sizeof(ap));
	ap.codec = "opus";
	ap.mime_type = "audio/mp4";
	ap.bitrate = 32000;
	ap.samplerate = 48000;
	ap.channel_config = "2";
	pc->audio_track = xqc_moq_track_create(session, "namespace", "audio", XQC_MOQ_TRACK_AUDIO, &ap, XQC_MOQ_CONTAINER_LOC, XQC_MOQ_TRACK_FOR_PUB);
	printf("[MOQ] Publisher tracks created\n");
}

static void on_datachannel(xqc_moq_user_session_t *user_session) { (void)user_session; }
static void on_datachannel_msg(xqc_moq_user_session_t *user_session, uint8_t *msg, size_t msg_len)
{
	(void)user_session; (void)msg; (void)msg_len;
}

static void on_subscribe(xqc_moq_user_session_t *user_session, uint64_t subscribe_id,
    xqc_moq_track_t *track, xqc_moq_subscribe_msg_t *msg)
{
	xqc_moq_session_t *session = user_session->session;
	peer_moq_conn_t *pc = (peer_moq_conn_t *)user_session->data;
	xqc_moq_subscribe_ok_msg_t ok;
	memset(&ok, 0, sizeof(ok));
	ok.subscribe_id = subscribe_id;
	ok.expire_ms = 0;
	ok.content_exist = 1;
	ok.largest_group_id = 0;
	ok.largest_object_id = 0;
	if (xqc_moq_write_subscribe_ok(session, &ok) < 0)
		printf("[MOQ] write_subscribe_ok failed\n");
	if (msg->track_name && strcmp(msg->track_name, "video") == 0)
		pc->video_subscribe_id = subscribe_id;
	else if (msg->track_name && strcmp(msg->track_name, "audio") == 0)
		pc->audio_subscribe_id = subscribe_id;
	extern xqc_usec_t xqc_now(void);
	pc->next_frame_us = xqc_now();
	printf("[MOQ] subscribe %s id=%"PRIu64"\n", msg->track_name ? msg->track_name : "?", (uint64_t)subscribe_id);
}

static void on_request_keyframe(xqc_moq_user_session_t *user_session, uint64_t subscribe_id, xqc_moq_track_t *track)
{
	(void)user_session; (void)subscribe_id; (void)track;
}

static void on_bitrate_change(xqc_moq_user_session_t *user_session, uint64_t bitrate)
{
	(void)user_session; (void)bitrate;
}

static void on_subscribe_ok(xqc_moq_user_session_t *user_session, xqc_moq_subscribe_ok_msg_t *ok)
{
	(void)user_session; (void)ok;
}

static void on_subscribe_error(xqc_moq_user_session_t *user_session, xqc_moq_subscribe_error_msg_t *err)
{
	(void)user_session; (void)err;
}

static void on_catalog(xqc_moq_user_session_t *user_session, xqc_moq_track_info_t **track_info_array, xqc_int_t array_size)
{
	(void)user_session; (void)track_info_array; (void)array_size;
}

static void on_video_frame(xqc_moq_user_session_t *user_session, uint64_t subscribe_id, xqc_moq_video_frame_t *frame)
{
	(void)user_session; (void)subscribe_id; (void)frame;
}

static void on_audio_frame(xqc_moq_user_session_t *user_session, uint64_t subscribe_id, xqc_moq_audio_frame_t *frame)
{
	(void)user_session; (void)subscribe_id; (void)frame;
}

static int server_accept(xqc_engine_t *engine, xqc_connection_t *conn, const xqc_cid_t *cid, void *user_data)
{
	xquic_ice_ctx_t *ice_ctx = (xquic_ice_ctx_t *)user_data;
	xqc_moq_user_session_t *user_session = (xqc_moq_user_session_t *)calloc(1, sizeof(xqc_moq_user_session_t) + sizeof(peer_moq_conn_t));
	if (!user_session) return -1;
	peer_moq_conn_t *pc = (peer_moq_conn_t *)user_session->data;
	pc->ice_ctx = ice_ctx;
	pc->video_subscribe_id = (uint64_t)-1;
	pc->audio_subscribe_id = (uint64_t)-1;

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
	xqc_moq_session_t *session = xqc_moq_session_create(conn, user_session, XQC_MOQ_TRANSPORT_QUIC, XQC_MOQ_PUBLISHER, callbacks, NULL);
	if (!session) {
		free(user_session);
		return -1;
	}
	xqc_moq_configure_bitrate(session, 1000000, 8000000, 1000000);
	xqc_conn_set_transport_user_data(conn, user_session);
	ice_ctx->app_ctx = user_session;
	printf("[QUIC] connection accepted (MOQ Publisher)\n");
	return 0;
}

static void server_refuse(xqc_engine_t *engine, xqc_connection_t *conn, const xqc_cid_t *cid, void *user_data)
{
	(void)engine; (void)conn; (void)cid; (void)user_data;
}

static xqc_int_t server_conn_closing(xqc_connection_t *conn, const xqc_cid_t *cid, xqc_int_t err_code, void *conn_user_data)
{
	(void)conn; (void)cid; (void)err_code; (void)conn_user_data;
	return XQC_OK;
}

static int server_conn_create_notify(xqc_connection_t *conn, const xqc_cid_t *cid, void *user_data, void *conn_proto_data)
{
	(void)conn; (void)cid; (void)user_data; (void)conn_proto_data;
	return 0;
}

static int server_conn_close_notify(xqc_connection_t *conn, const xqc_cid_t *cid, void *user_data, void *conn_proto_data)
{
	xqc_moq_user_session_t *user_session = (xqc_moq_user_session_t *)user_data;
	xquic_ice_ctx_t *ice_ctx = ((peer_moq_conn_t *)user_session->data)->ice_ctx;
	if (user_session->session)
		xqc_moq_session_destroy(user_session->session);
	if (ice_ctx->app_ctx == user_session)
		ice_ctx->app_ctx = NULL;
	free(user_session);
	return 0;
}

static void server_conn_handshake_finished(xqc_connection_t *conn, void *user_data, void *conn_proto_data)
{
	(void)conn; (void)user_data; (void)conn_proto_data;
}

static void peer_send_frames_if_due(xquic_ice_ctx_t *ctx)
{
	xqc_moq_user_session_t *user_session = (xqc_moq_user_session_t *)ctx->app_ctx;
	if (!user_session || !user_session->session) return;
	peer_moq_conn_t *pc = (peer_moq_conn_t *)user_session->data;
	extern xqc_usec_t xqc_now(void);
	uint64_t now = xqc_now();
	if (pc->next_frame_us > now) return;
	pc->next_frame_us = now + 33000; /* ~30 fps */

	if (pc->video_subscribe_id != (uint64_t)-1 && pc->video_track) {
		static uint8_t payload[1024];
		xqc_moq_video_frame_t vf;
		vf.type = (pc->video_seq % 30 == 0) ? XQC_MOQ_VIDEO_KEY : XQC_MOQ_VIDEO_DELTA;
		vf.seq_num = pc->video_seq++;
		vf.timestamp_us = now;
		vf.video_len = sizeof(payload);
		vf.video_data = payload;
		if (xqc_moq_write_video_frame(user_session->session, pc->video_subscribe_id, pc->video_track, &vf) < 0)
			; /* ignore */
	}
	if (pc->audio_subscribe_id != (uint64_t)-1 && pc->audio_track) {
		static uint8_t apayload[256];
		xqc_moq_audio_frame_t af;
		af.seq_num = pc->audio_seq++;
		af.timestamp_us = now;
		af.audio_len = sizeof(apayload);
		af.audio_data = apayload;
		if (xqc_moq_write_audio_frame(user_session->session, pc->audio_subscribe_id, pc->audio_track, &af) < 0)
			; /* ignore */
	}
}

static void peer_on_ice_connected(p2p_run_config_t *cfg, void *agent_ptr, void *ice_ctx_ptr)
{
	(void)cfg;
	(void)agent_ptr;
	xquic_ice_ctx_t *ice_ctx = (xquic_ice_ctx_t *)ice_ctx_ptr;

	const char *cert_file = getenv("P2P_SSL_CERT");
	const char *key_file = getenv("P2P_SSL_KEY");
	if (!cert_file) cert_file = "server.crt";
	if (!key_file) key_file = "server.key";

	int pipe_fds[2];
#ifdef _WIN32
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
	if (xqc_engine_get_default_config(&config, XQC_ENGINE_SERVER) < 0) {
		fprintf(stderr, "xqc_engine_get_default_config failed\n");
		return;
	}
	config.cfg_log_level = XQC_LOG_DEBUG;

	xqc_engine_ssl_config_t ssl_config;
	memset(&ssl_config, 0, sizeof(ssl_config));
	ssl_config.private_key_file = (char *)key_file;
	ssl_config.cert_file = (char *)cert_file;
	ssl_config.ciphers = XQC_TLS_CIPHERS;
	ssl_config.groups = XQC_TLS_GROUPS;

	xqc_engine_callback_t engine_cb = {
		.set_event_timer = peer_set_event_timer,
		.log_callbacks = {
			.xqc_log_write_err = peer_log_write,
			.xqc_log_write_stat = peer_log_write,
		},
	};

	xqc_transport_callbacks_t tcbs = {
		.server_accept = server_accept,
		.server_refuse = server_refuse,
		.write_socket = peer_write_socket,
		.write_socket_ex = peer_write_socket_ex,
		.stateless_reset = peer_stateless_reset,
		.conn_closing = server_conn_closing,
	};

	xqc_engine_t *engine = xqc_engine_create(XQC_ENGINE_SERVER, &config, &ssl_config, &engine_cb, &tcbs, ice_ctx);
	if (!engine) {
		fprintf(stderr, "xqc_engine_create failed (check cert %s / key %s)\n", cert_file, key_file);
		return;
	}

	xqc_conn_callbacks_t conn_cbs = {
		.conn_create_notify = server_conn_create_notify,
		.conn_close_notify = server_conn_close_notify,
		.conn_handshake_finished = server_conn_handshake_finished,
	};
	xqc_moq_init_alpn(engine, &conn_cbs, XQC_MOQ_TRANSPORT_QUIC);

	ice_ctx->engine = engine;
	ice_ctx->post_main_logic = peer_send_frames_if_due;
	printf("[QUIC] server listening over ICE (MOQ Publisher), cert=%s\n", cert_file);
}
#endif
