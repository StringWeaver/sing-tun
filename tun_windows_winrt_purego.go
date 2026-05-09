//go:build windows && with_winrt_vpn
// +build windows,with_winrt_vpn

package tun

import (
	"syscall"
	"unsafe"

	"github.com/ebitengine/purego"
)

var (
	vpnChannelInjectPacket uintptr
)

func init() {
	// The Go DLL is loaded into the C++ WinRT component process.
	// We can get the module handle of the host executable/DLL using GetModuleHandle(NULL)
	// or specifically load "VpnPlugin.dll". 
	// Since VpnPlugin.dll is already loaded (it's loading us), we can just use GetModuleHandle.
	
	kernel32, err := syscall.LoadDLL("kernel32.dll")
	if err == nil {
		getModuleHandle, err := kernel32.FindProc("GetModuleHandleW")
		if err == nil {
			// Get handle to VpnPlugin.dll
			name, _ := syscall.UTF16PtrFromString("VpnPlugin.dll")
			ret, _, _ := getModuleHandle.Call(uintptr(unsafe.Pointer(name)))
			if ret != 0 {
				purego.RegisterLibFunc(&vpnChannelInjectPacket, ret, "VpnChannel_InjectPacket")
			} else {
				// Fallback to GetModuleHandle(NULL)
				ret, _, _ = getModuleHandle.Call(0)
				if ret != 0 {
					purego.RegisterLibFunc(&vpnChannelInjectPacket, ret, "VpnChannel_InjectPacket")
				}
			}
		}
	}
}

func injectPacketToWinRT(packet []byte) bool {
	if vpnChannelInjectPacket == 0 {
		return false
	}
	
	var dataPtr *byte
	if len(packet) > 0 {
		dataPtr = &packet[0]
	}
	
	ret, _, _ := purego.SyscallN(vpnChannelInjectPacket, uintptr(unsafe.Pointer(dataPtr)), uintptr(len(packet)))
	return ret != 0
}
