#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Networking.Vpn.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Storage.Streams.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace winrt::SingTun::implementation
{
    struct VpnPlugin : winrt::implements<VpnPlugin, winrt::Windows::Networking::Vpn::IVpnPlugIn>
    {
        VpnPlugin() = default;
        ~VpnPlugin();

        void Connect(winrt::Windows::Networking::Vpn::VpnChannel const& channel);
        void Disconnect(winrt::Windows::Networking::Vpn::VpnChannel const& channel);
        void GetKeepAlivePayload(winrt::Windows::Networking::Vpn::VpnChannel const& channel, winrt::Windows::Networking::Vpn::VpnPacketBuffer& keepAlivePacket);
        void Encapsulate(winrt::Windows::Networking::Vpn::VpnChannel const& channel, winrt::Windows::Networking::Vpn::VpnPacketBufferList const& packets, winrt::Windows::Networking::Vpn::VpnPacketBufferList const& encapulatedPackets);
        void Decapsulate(winrt::Windows::Networking::Vpn::VpnChannel const& channel, winrt::Windows::Networking::Vpn::VpnPacketBuffer const& encapBuffer, winrt::Windows::Networking::Vpn::VpnPacketBufferList const& decapsulatedPackets, winrt::Windows::Networking::Vpn::VpnPacketBufferList const& controlPackets);

        // API for Go via C-exports
        bool InjectReceivePacket(const uint8_t* data, size_t size);

        static VpnPlugin* s_instance;

    private:
        winrt::Windows::Networking::Vpn::VpnChannel m_channel{ nullptr };
        HMODULE m_goModule{ nullptr };
        void* m_goContext{ nullptr };

        void StopGoEngine();
    };
}

extern "C" {
    // These functions are exported from the C++ DLL for Go to call via purego.
    // e.g. Go side uses: purego.RegisterLibFunc(&VpnChannel_InjectPacket, vpnPluginDll, "VpnChannel_InjectPacket")
    __declspec(dllexport) bool VpnChannel_InjectPacket(const uint8_t* data, size_t size);
}
