//go:build windows && with_winrt_vpn

package tun

import (
	"errors"
	"io"
	"sync"
	"sync/atomic"
	"syscall"
	"unsafe"

	"github.com/ebitengine/purego"
)

var (
	ErrWinRTVpnClosed = errors.New("winrt vpn closed")
)

type winRTVpn struct {
	options Options

	// buffer for reading packets from C++ callback
	packetChan chan []byte

	closed atomic.Bool
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
	// Register the global instance to receive callbacks from BoxWinRT_OnEncapsulate
	setGlobalWinRTVpn(t)

	// Initialize purego to resolve VpnChannel_InjectPacket from VpnPlugin.dll
	// This must be done after the global instance is set, because the C++ side
	// may call BoxWinRT_OnEncapsulate immediately after the engine starts.
	initPuregoInject()

	return nil
}

func (t *winRTVpn) Close() error {
	if !t.closed.CompareAndSwap(false, true) {
		return nil
	}
	clearGlobalWinRTVpn(t)
	close(t.packetChan)
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
	if t.closed.Load() {
		return 0, io.EOF
	}
	// Call C++ DLL exported function VpnChannel_InjectPacket via purego
	if !injectPacketToWinRT(p) {
		return 0, errors.New("failed to inject packet to winrt")
	}
	return len(p), nil
}

// OnEncapsulate is called by libbox's //export BoxWinRT_OnEncapsulate
// when Windows VPN stack delivers an outbound IP packet via VpnPlugin::Encapsulate.
func OnEncapsulate(packet []byte) {
	globalWinRTVpnMu.RLock()
	t := globalWinRTVpn
	globalWinRTVpnMu.RUnlock()

	if t == nil || t.closed.Load() {
		return
	}

	select {
	case t.packetChan <- packet:
	default:
		// Drop packet if channel is full
	}
}

// --- Global state for callbacks ---

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

// --- Purego: call VpnPlugin.dll exported functions without CGO ---

var (
	vpnChannelInjectPacket uintptr
	puregoOnce             sync.Once
	puregoInited           bool
)

// initPuregoInject resolves the VpnChannel_InjectPacket function from
// VpnPlugin.dll using purego. This is called lazily from winRTVpn.Start()
// rather than from init(), because:
//   - In the new architecture, libbox.dll is loaded by VpnPlugin.dll via
//     LoadLibrary, so VpnPlugin.dll is guaranteed to already be in-process.
//   - The Go DLL's init() runs during LoadLibrary before the caller gets
//     the handle back, so VpnPlugin.dll is available but deferring to
//     Start() gives a cleaner lifecycle.
func initPuregoInject() {
	puregoOnce.Do(func() {
		kernel32, err := syscall.LoadDLL("kernel32.dll")
		if err != nil {
			return
		}
		getModuleHandle, err := kernel32.FindProc("GetModuleHandleW")
		if err != nil {
			return
		}

		// Try VpnPlugin.dll by name first
		name, _ := syscall.UTF16PtrFromString("VpnPlugin.dll")
		ret, _, _ := getModuleHandle.Call(uintptr(unsafe.Pointer(name)))
		if ret != 0 {
			purego.RegisterLibFunc(&vpnChannelInjectPacket, ret, "VpnChannel_InjectPacket")
			puregoInited = true
			return
		}

		// Fallback: try the host executable module
		ret, _, _ = getModuleHandle.Call(0)
		if ret != 0 {
			purego.RegisterLibFunc(&vpnChannelInjectPacket, ret, "VpnChannel_InjectPacket")
			puregoInited = true
		}
	})
}

func injectPacketToWinRT(packet []byte) bool {
	if !puregoInited || vpnChannelInjectPacket == 0 {
		return false
	}

	var dataPtr *byte
	if len(packet) > 0 {
		dataPtr = &packet[0]
	}

	ret, _, _ := purego.SyscallN(vpnChannelInjectPacket, uintptr(unsafe.Pointer(dataPtr)), uintptr(len(packet)))
	return ret != 0
}
