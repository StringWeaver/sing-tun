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
	// Register the global instance to receive callbacks from goOnEncapsulate
	setGlobalWinRTVpn(t)

	// Initialize VpnBridge.dll via purego:
	//   1. Load the DLL
	//   2. Initialize COM apartment (VpnBridge_InitCOM)
	//   3. Register Go callback via syscall.NewCallback (VpnBridge_RegisterPlugin)
	//   4. Resolve VpnChannel_InjectPacket for inbound packet injection
	initVpnBridge()

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

// OnEncapsulate is called by goOnEncapsulate (the callback registered via
// syscall.NewCallback) when Windows VPN stack delivers an outbound IP packet
// via VpnBridge's VpnPlugin::Encapsulate.
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

// --- Go callback for C++ Encapsulate (registered via syscall.NewCallback) ---

// goOnEncapsulate is the Go function that C++ calls when the VPN stack
// delivers an outbound IP packet. It must return uintptr as required by
// syscall.NewCallback, even though the C++ side ignores the return value.
func goOnEncapsulate(data *byte, size uintptr) uintptr {
	if data == nil || size == 0 {
		return 0
	}
	packet := unsafe.Slice(data, int(size))
	packetCopy := make([]byte, len(packet))
	copy(packetCopy, packet)
	OnEncapsulate(packetCopy)
	return 0
}

// --- Purego: load VpnBridge.dll and register callbacks ---

var (
	vpnBridgeDLL               syscall.Handle
	vpnBridgeInitCOM           func()
	vpnBridgeRegisterPlugin    func(cb uintptr)
	vpnChannelInjectPacket     uintptr
	vpnBridgeOnce              sync.Once
	vpnBridgeInited            bool
	vpnBridgeInitErr           error
)

// initVpnBridge loads VpnBridge.dll via purego, initializes COM apartment,
// registers the Go encapsulate callback, and resolves the inject function.
//
// In the new architecture:
//   - libbox.dll is loaded by the frontend App (Dart FFI)
//   - VpnBridge.dll is loaded by Go via purego
//   - Go registers its callback function pointer via VpnBridge_RegisterPlugin
//   - No more LoadLibrary("libbox.dll") from C++ side
func initVpnBridge() {
	vpnBridgeOnce.Do(func() {
		// 1. Load VpnBridge.dll
		var err error
		vpnBridgeDLL, err = syscall.LoadLibrary("VpnBridge.dll")
		if err != nil {
			vpnBridgeInitErr = err
			return
		}

		// 2. Resolve exported functions via purego
		purego.RegisterLibFunc(&vpnBridgeInitCOM, uintptr(vpnBridgeDLL), "VpnBridge_InitCOM")
		purego.RegisterLibFunc(&vpnBridgeRegisterPlugin, uintptr(vpnBridgeDLL), "VpnBridge_RegisterPlugin")
		purego.RegisterLibFunc(&vpnChannelInjectPacket, uintptr(vpnBridgeDLL), "VpnChannel_InjectPacket")

		// 3. Initialize COM apartment (multi_threaded for Go goroutine compatibility)
		//    Must be called once; never uninit during process lifetime.
		vpnBridgeInitCOM()

		// 4. Register Go encapsulate callback via syscall.NewCallback
		cb := syscall.NewCallback(goOnEncapsulate)
		vpnBridgeRegisterPlugin(cb)

		vpnBridgeInited = true
	})
}

func injectPacketToWinRT(packet []byte) bool {
	if !vpnBridgeInited || vpnChannelInjectPacket == 0 {
		return false
	}

	var dataPtr *byte
	if len(packet) > 0 {
		dataPtr = &packet[0]
	}

	ret, _, _ := purego.SyscallN(vpnChannelInjectPacket, uintptr(unsafe.Pointer(dataPtr)), uintptr(len(packet)))
	return ret != 0
}
