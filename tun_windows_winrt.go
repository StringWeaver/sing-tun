//go:build windows && with_winrt_vpn
// +build windows,with_winrt_vpn

package tun

import (
	"errors"
	"io"
	"sync"
)

var (
	ErrWinRTVpnClosed = errors.New("winrt vpn closed")
)

type winRTVpn struct {
	options Options
	
	// buffer for reading packets from C++ callback
	packetChan chan []byte
	
	closed bool
	mu     sync.Mutex
}

func NewWinRTVpn(options Options) (Tun, error) {
	return &winRTVpn{
		options:    options,
		packetChan: make(chan []byte, 1024),
	}, nil
}

func (t *winRTVpn) Name() (string, error) {
	if t.options.Name != "" {
		return t.options.Name, nil
	}
	return "WinRTVpn", nil
}

func (t *winRTVpn) Start() error {
	// Setup the global instance to receive C++ callbacks
	setGlobalWinRTVpn(t)
	return nil
}

func (t *winRTVpn) Close() error {
	t.mu.Lock()
	defer t.mu.Lock()
	if t.closed {
		return nil
	}
	t.closed = true
	close(t.packetChan)
	clearGlobalWinRTVpn(t)
	return nil
}

func (t *winRTVpn) UpdateRouteOptions(options Options) error {
	return nil
}

func (t *winRTVpn) Read(p []byte) (n int, err error) {
	packet, ok := <-t.packetChan
	if !ok {
		return 0, io.EOF
	}
	n = copy(p, packet)
	return n, nil
}

func (t *winRTVpn) Write(p []byte) (n int, err error) {
	if t.closed {
		return 0, io.EOF
	}
	// Call C++ DLL exported function VpnChannel_InjectPacket via purego
	if !injectPacketToWinRT(p) {
		return 0, errors.New("failed to inject packet to winrt")
	}
	return len(p), nil
}

// Global state to handle C callbacks
var (
	globalWinRTVpn   *winRTVpn
	globalWinRTVpnMu sync.RWMutex
)

func setGlobalWinRTVpn(t *winRTVpn) {
	globalWinRTVpnMu.Lock()
	defer globalWinRTVpnMu.Unlock()
	globalWinRTVpn = t
}

func clearGlobalWinRTVpn(t *winRTVpn) {
	globalWinRTVpnMu.Lock()
	defer globalWinRTVpnMu.Unlock()
	if globalWinRTVpn == t {
		globalWinRTVpn = nil
	}
}

// OnEncapsulate is called by the C exported function when OS sends an outbound packet
func OnEncapsulate(packet []byte) {
	globalWinRTVpnMu.RLock()
	t := globalWinRTVpn
	globalWinRTVpnMu.RUnlock()

	if t == nil || t.closed {
		return
	}

	select {
	case t.packetChan <- packet:
	default:
		// Drop packet if channel is full
	}
}
