package config

import (
	"fmt"
	"os"
	"strconv"
	"time"

	"gopkg.in/yaml.v3"
)

type Config struct {
	Server   ServerConfig `yaml:"server"`
	Room     RoomConfig   `yaml:"room"`
	Limits   LimitsConfig `yaml:"limits"`
	LogLevel string       `yaml:"log_level"`
}

type ServerConfig struct {
	HTTPAddr string `yaml:"http_addr"`
	TCPAddr  string `yaml:"tcp_addr"`
}

type RoomConfig struct {
	MaxPeersPerRoom int           `yaml:"max_peers_per_room"`
	IdleTimeout     time.Duration `yaml:"idle_timeout"`
}

type LimitsConfig struct {
	MaxMessageSize  int `yaml:"max_message_size"`
	MaxConnections  int `yaml:"max_connections"`
	RoomIDMaxLength int `yaml:"room_id_max_length"`
}

func Default() *Config {
	return &Config{
		Server:   ServerConfig{HTTPAddr: ":8080", TCPAddr: ":8888"},
		Room:     RoomConfig{MaxPeersPerRoom: 2},
		Limits:   LimitsConfig{MaxMessageSize: 65536, RoomIDMaxLength: 256},
		LogLevel: "info",
	}
}

func Load(path string) (*Config, error) {
	cfg := Default()
	if path != "" {
		data, err := os.ReadFile(path)
		if err != nil && !os.IsNotExist(err) {
			return nil, fmt.Errorf("read config: %w", err)
		}
		if err == nil {
			if e := yaml.Unmarshal(data, cfg); e != nil {
				return nil, fmt.Errorf("parse config: %w", e)
			}
		}
	}
	if v := os.Getenv("SIGNALING_HTTP_ADDR"); v != "" {
		cfg.Server.HTTPAddr = v
	}
	if v := os.Getenv("SIGNALING_TCP_ADDR"); v != "" {
		cfg.Server.TCPAddr = v
	}
	if v := os.Getenv("SIGNALING_LOG_LEVEL"); v != "" {
		cfg.LogLevel = v
	}
	if v := os.Getenv("SIGNALING_MAX_CONNECTIONS"); v != "" {
		if n, err := strconv.Atoi(v); err == nil {
			cfg.Limits.MaxConnections = n
		}
	}
	if cfg.Limits.MaxMessageSize <= 0 {
		cfg.Limits.MaxMessageSize = 65536
	}
	if cfg.Room.MaxPeersPerRoom <= 0 {
		cfg.Room.MaxPeersPerRoom = 2
	}
	return cfg, nil
}
