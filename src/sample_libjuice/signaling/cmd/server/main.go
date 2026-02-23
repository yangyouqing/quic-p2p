package main

import (
	"context"
	"flag"
	"net"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"log/slog"

	"github.com/quic-p2p/signaling/internal/config"
	"github.com/quic-p2p/signaling/internal/room"
	"github.com/quic-p2p/signaling/internal/transport"
)

func main() {
	configPath := flag.String("config", "", "path to config.yaml")
	flag.Parse()

	cfg, err := config.Load(*configPath)
	if err != nil {
		slog.Error("load config", "err", err)
		os.Exit(1)
	}

	level := new(slog.LevelVar)
	switch cfg.LogLevel {
	case "debug":
		level.Set(slog.LevelDebug)
	case "warn":
		level.Set(slog.LevelWarn)
	case "error":
		level.Set(slog.LevelError)
	default:
		level.Set(slog.LevelInfo)
	}
	logger := slog.New(slog.NewTextHandler(os.Stdout, &slog.HandlerOptions{Level: level}))
	slog.SetDefault(logger)

	roomCfg := room.RoomManagerConfig{
		MaxPeersPerRoom: cfg.Room.MaxPeersPerRoom,
		IdleTimeout:     cfg.Room.IdleTimeout,
		ChannelSize:     64,
	}
	roomMgr := room.NewManager(roomCfg, logger)

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer stop()

	go roomMgr.RunIdleCleanup(ctx, 30*time.Second)

	limits := transport.Limits{
		MaxMessageSize:  cfg.Limits.MaxMessageSize,
		RoomIDMaxLength: cfg.Limits.RoomIDMaxLength,
	}

	tcpLn, err := net.Listen("tcp", cfg.Server.TCPAddr)
	if err != nil {
		slog.Error("TCP listen", "addr", cfg.Server.TCPAddr, "err", err)
		os.Exit(1)
	}
	defer tcpLn.Close()
	tcpSrv := transport.NewTCPServer(tcpLn, roomMgr, limits, cfg.Limits.MaxConnections, logger)
	go func() {
		if err := tcpSrv.Serve(); err != nil {
			slog.Error("TCP serve", "err", err)
		}
	}()
	slog.Info("TCP signaling listening", "addr", cfg.Server.TCPAddr)

	mux := http.NewServeMux()
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("ok"))
	})
	mux.HandleFunc("/ready", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("ready"))
	})
	mux.Handle("/", transport.NewWSServer(roomMgr, limits, cfg.Limits.MaxConnections, logger))

	httpSrv := &http.Server{Addr: cfg.Server.HTTPAddr, Handler: mux}
	go func() {
		slog.Info("HTTP/WebSocket listening", "addr", cfg.Server.HTTPAddr)
		if err := httpSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			slog.Error("HTTP serve", "err", err)
		}
	}()

	<-ctx.Done()
	slog.Info("shutting down...")
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	_ = httpSrv.Shutdown(shutdownCtx)
	tcpLn.Close()
	slog.Info("shutdown complete")
}
