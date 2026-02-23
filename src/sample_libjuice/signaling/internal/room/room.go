package room

import (
	"context"
	"sync"
	"time"
	"log/slog"
)

type Peer struct {
	ID       string
	Ch       chan []byte
	Role     string
	RoomID   string
	JoinedAt time.Time
}

type Room struct {
	ID          string
	Peers       [2]*Peer
	mu          sync.Mutex
	LastActive  time.Time
	IdleTimeout time.Duration
	log         *slog.Logger
}

func NewRoom(id string, idleTimeout time.Duration, log *slog.Logger) *Room {
	if log == nil {
		log = slog.Default()
	}
	return &Room{
		ID:          id,
		LastActive:  time.Now(),
		IdleTimeout: idleTimeout,
		log:         log,
	}
}

func (r *Room) Add(peerID string, chSize int) (slot int, role string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	for i := 0; i < 2; i++ {
		if r.Peers[i] == nil {
			if chSize <= 0 {
				chSize = 64
			}
			r.Peers[i] = &Peer{
				ID: peerID, Ch: make(chan []byte, chSize),
				Role: roleForSlot(i), RoomID: r.ID, JoinedAt: time.Now(),
			}
			r.LastActive = time.Now()
			return i, r.Peers[i].Role
		}
	}
	return -1, ""
}

func roleForSlot(slot int) string {
	if slot == 0 {
		return "peer2"
	}
	return "peer1"
}

func (r *Room) Remove(peerID string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	for i := 0; i < 2; i++ {
		if r.Peers[i] != nil && r.Peers[i].ID == peerID {
			close(r.Peers[i].Ch)
			r.Peers[i] = nil
			r.log.Debug("peer left room", "room_id", r.ID, "peer_id", peerID)
			return
		}
	}
}

func (r *Room) Forward(fromPeerID string, data []byte) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.LastActive = time.Now()
	for i := 0; i < 2; i++ {
		if r.Peers[i] != nil && r.Peers[i].ID != fromPeerID {
			select {
			case r.Peers[i].Ch <- data:
				return true
			default:
				r.log.Warn("peer channel full, dropping message", "room_id", r.ID)
				return false
			}
		}
	}
	return false
}

func (r *Room) Paired() bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.Peers[0] != nil && r.Peers[1] != nil
}

func (r *Room) Empty() bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.Peers[0] == nil && r.Peers[1] == nil
}

func (r *Room) ShouldExpire() bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.Peers[0] != nil || r.Peers[1] != nil {
		return false
	}
	if r.IdleTimeout <= 0 {
		return true
	}
	return time.Since(r.LastActive) > r.IdleTimeout
}

type RoomManagerConfig struct {
	MaxPeersPerRoom int
	IdleTimeout     time.Duration
	ChannelSize     int
}

type Manager struct {
	rooms  map[string]*Room
	mu     sync.RWMutex
	config RoomManagerConfig
	log    *slog.Logger
}

func NewManager(cfg RoomManagerConfig, log *slog.Logger) *Manager {
	if log == nil {
		log = slog.Default()
	}
	if cfg.MaxPeersPerRoom <= 0 {
		cfg.MaxPeersPerRoom = 2
	}
	return &Manager{
		rooms:  make(map[string]*Room),
		config: cfg,
		log:    log,
	}
}

func (m *Manager) GetOrCreate(roomID string) *Room {
	m.mu.Lock()
	defer m.mu.Unlock()
	if r, ok := m.rooms[roomID]; ok {
		return r
	}
	r := NewRoom(roomID, m.config.IdleTimeout, m.log)
	m.rooms[roomID] = r
	return r
}

func (m *Manager) AddPeer(roomID string, peerID string) (slot int, role string, r *Room) {
	r = m.GetOrCreate(roomID)
	slot, role = r.Add(peerID, m.config.ChannelSize)
	return slot, role, r
}

func (m *Manager) RemovePeer(roomID string, peerID string) *Room {
	m.mu.RLock()
	r, ok := m.rooms[roomID]
	m.mu.RUnlock()
	if !ok {
		return nil
	}
	r.Remove(peerID)
	return r
}

func (m *Manager) DeleteRoom(roomID string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	delete(m.rooms, roomID)
	m.log.Debug("room deleted", "room_id", roomID)
}

func (m *Manager) RoomCount() int {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return len(m.rooms)
}

func (m *Manager) RunIdleCleanup(ctx context.Context, interval time.Duration) {
	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			m.mu.Lock()
			for id, r := range m.rooms {
				if r.ShouldExpire() {
					delete(m.rooms, id)
				}
			}
			m.mu.Unlock()
		}
	}
}
