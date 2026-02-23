package transport

import (
	"bufio"
	"encoding/json"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"log/slog"

	"github.com/quic-p2p/signaling/internal/room"
)

type Limits struct {
	MaxMessageSize  int
	RoomIDMaxLength int
}

type TCPServer struct {
	listener net.Listener
	rooms    *room.Manager
	limits   Limits
	log      *slog.Logger
	connCount atomic.Int64
	maxConns int64
}

func NewTCPServer(ln net.Listener, rooms *room.Manager, limits Limits, maxConns int, log *slog.Logger) *TCPServer {
	if log == nil {
		log = slog.Default()
	}
	return &TCPServer{
		listener: ln,
		rooms:    rooms,
		limits:   limits,
		log:      log,
		maxConns: int64(maxConns),
	}
}

func (s *TCPServer) Serve() error {
	for {
		conn, err := s.listener.Accept()
		if err != nil {
			return err
		}
		if s.maxConns > 0 && s.connCount.Load() >= s.maxConns {
			conn.Close()
			s.log.Warn("rejected connection: max connections reached")
			continue
		}
		s.connCount.Add(1)
		go s.handleConn(conn)
	}
}

func (s *TCPServer) handleConn(conn net.Conn) {
	defer conn.Close()
	defer s.connCount.Add(-1)

	connID := conn.RemoteAddr().String()
	roomID := "default"

	scanner := bufio.NewScanner(conn)
	scanner.Buffer(make([]byte, 0, 4096), s.limits.MaxMessageSize)

	if !scanner.Scan() {
		return
	}
	firstLine := scanner.Bytes()
	if len(firstLine) > 0 {
		var msg struct {
			Type   string `json:"type"`
			RoomID string `json:"room_id"`
		}
		if json.Unmarshal(firstLine, &msg) == nil && msg.Type == "join" && msg.RoomID != "" {
			if len(msg.RoomID) <= s.limits.RoomIDMaxLength {
				roomID = msg.RoomID
			}
		}
	}

	slot, role, r := s.rooms.AddPeer(roomID, connID)
	if slot < 0 {
		s.log.Warn("room full", "room_id", roomID, "conn_id", connID)
		return
	}

	var sendMu sync.Mutex
	send := func(data []byte) bool {
		sendMu.Lock()
		defer sendMu.Unlock()
		_, err := conn.Write(append(data, '\n'))
		return err == nil
	}

	roleMsg, _ := json.Marshal(map[string]string{"type": "role", "role": role, "room_id": roomID})
	send(roleMsg)

	s.log.Info("TCP peer connected", "conn_id", connID, "room_id", roomID, "role", role)

	peerCh := r.Peers[slot].Ch
	defer func() {
		s.rooms.RemovePeer(roomID, connID)
		s.log.Info("TCP peer disconnected", "conn_id", connID, "room_id", roomID)
		if r != nil && r.Empty() {
			time.AfterFunc(5*time.Second, func() {
				if r.ShouldExpire() {
					s.rooms.DeleteRoom(roomID)
				}
			})
		}
	}()

	go func() {
		for data := range peerCh {
			send(data)
		}
	}()

	for scanner.Scan() {
		line := scanner.Bytes()
		if len(line) == 0 {
			continue
		}
		if len(line) > s.limits.MaxMessageSize {
			s.log.Warn("message too large", "size", len(line), "conn_id", connID)
			continue
		}
		dup := make([]byte, len(line))
		copy(dup, line)
		r.Forward(connID, dup)
	}
}
