package main

import "C"
import (
	"log"
	"unsafe"
	
	"github.com/sagernet/sing-tun"
)

//export GoCore_Start
func GoCore_Start() int {
	log.Println("GoCore_Start called by C++ WinRT Plugin")
	
	// Create and start the WinRT VPN TUN
	tunInterface, err := tun.NewWinRTVpn(tun.Options{
		Name: "WinRTVpn",
	})
	if err != nil {
		log.Printf("Failed to create WinRT VPN: %v", err)
		return -1
	}
	
	if err := tunInterface.Start(); err != nil {
		log.Printf("Failed to start WinRT VPN: %v", err)
		return -2
	}
	
	// Start a dummy read loop
	go func() {
		buf := make([]byte, 65535)
		for {
			n, err := tunInterface.Read(buf)
			if err != nil {
				log.Printf("WinRT VPN Read error: %v", err)
				break
			}
			log.Printf("Go received %d bytes from WinRT VPN", n)
			
			// Echo back
			tunInterface.Write(buf[:n])
		}
	}()
	
	return 0
}

//export GoCore_Stop
func GoCore_Stop() {
	log.Println("GoCore_Stop called")
	// Stop the engine...
}

//export GoCore_OnEncapsulate
func GoCore_OnEncapsulate(data *C.uint8_t, size C.size_t) {
	// Convert C pointer to Go slice without copy
	// Go 1.20+: unsafe.Slice
	packet := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(size))
	
	// Copy to a new slice to avoid C memory being freed
	packetCopy := make([]byte, len(packet))
	copy(packetCopy, packet)
	
	// Pass to tun implementation
	tun.OnEncapsulate(packetCopy)
}

func main() {
	// Need main function for c-shared build mode
}
