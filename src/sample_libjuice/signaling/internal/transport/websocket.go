package transport

import (
	"encoding/json"
	"net/http"
	"sync"
	"sync/atomic"

	"log/slog"

	"github.com/gorilla/websocket"
	"github.com/quic-p2p/signaling/internal/room"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

type WSServer struct {
	rooms    *room.Manager
	limits   Limits
	log      *slog.Logger
	connCount atomic.Int64
	maxConns int64
}

func NewWSServer(rooms *room.Manager, limits Limits, maxConns int, log *slog.Logger) *WSServer {
	if log == nil {
		log = slog.Default()
	}
	return &WSServer{
		rooms:    rooms,
		limits:   limits,
		log:      log,
		maxConns: int64(maxConns),
	}
}

func (s *WSServer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if s.maxConns > 0 && s.connCount.Load() >= s.maxConns {
		http.Error(w, "too many connections", http.StatusServiceUnavailable)
		return
	}
	roomID := r.URL.Query().Get("room_id")
	if roomID == "" {
		roomID = "default"
	}
	if len(roomID) > s.limits.RoomIDMaxLength {
		http.Error(w, "room_id too long", http.StatusBadRequest)
		return
	}

	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		s.log.Warn("websocket upgrade failed", "err", err)
		return
	}
	defer conn.Close()
	s.connCount.Add(1)
	defer s.connCount.Add(-1)

	connID := r.RemoteAddr
	slot, role, rm := s.rooms.AddPeer(roomID, connID)
	if slot < 0 {
		s.log.Warn("room full", "room_id", roomID)
		return
	}

	peerCh := rm.Peers[slot].Ch
	defer s.rooms.RemovePeer(roomID, connID)

	roleMsg, _ := json.Marshal(map[string]string{"type": "role", "role": role, "room_id": roomID})
	if err := conn.WriteMessage(websocket.TextMessage, roleMsg); err != nil {
		return
	}
	s.log.Info("WebSocket peer connected", "conn_id", connID, "room_id", roomID, "role", role)

	var sendMu sync.Mutex
	done := make(chan struct{})
	go func() {
		for data := range peerCh {
			sendMu.Lock()
			err := conn.WriteMessage(websocket.TextMessage, data)
			sendMu.Unlock()
			if err != nil {
				return
			}
		}
		close(done)
	}()

	for {
		_, data, err := conn.ReadMessage()
		if err != nil {
			break
		}
		if len(data) > s.limits.MaxMessageSize {
			s.log.Warn("message too large", "size", len(data))
			continue
		}
		rm.Forward(connID, data)
	}
	<-done
}
