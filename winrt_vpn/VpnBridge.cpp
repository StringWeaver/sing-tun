#include "pch.h"
#include "VpnPlugin.h"

// --- Global callback pointer set by Go via VpnBridge_RegisterPlugin ---

typedef uintptr_t (*OnEncapsulateCallback)(const uint8_t* data, size_t size);
static OnEncapsulateCallback g_onEncapsulate = nullptr;

namespace winrt::SingTun::implementation
{
    VpnPlugin* VpnPlugin::s_instance = nullptr;

    VpnPlugin::~VpnPlugin()
    {
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }

    void VpnPlugin::Connect(winrt::Windows::Networking::Vpn::VpnChannel const& channel)
    {
        s_instance = this;
        m_channel = channel;

        try {
            using namespace winrt::Windows::Networking;
            using namespace winrt::Windows::Networking::Sockets;
            using namespace winrt::Windows::Networking::Vpn;

            // 1. Create a dummy socket to satisfy WinRT requirements
            DatagramSocket transport{};
            channel.AssociateTransport(transport, nullptr);
            const auto localhost = HostName{ L"127.0.0.1" };
            transport.BindEndpointAsync(localhost, L"").get();
            transport.ConnectAsync(localhost, transport.Information().LocalPort()).get();

            // 2. Configure routes (intercept everything or specific subnets)
            VpnRouteAssignment routeScope{};
            routeScope.ExcludeLocalSubnets(true);
            routeScope.Ipv4InclusionRoutes(std::vector{
                VpnRoute(HostName{ L"0.0.0.0" }, 1),
                VpnRoute(HostName{ L"128.0.0.0" }, 1)
            });
            // routeScope.Ipv6InclusionRoutes(...)

            VpnDomainNameAssignment dnsAssignment{}; // Configure DNS if needed

            // 3. Start the channel with the dummy transport
            channel.StartWithMainTransport(
                std::vector{ HostName{ L"192.168.3.1" } }, // Dummy IPv4
                std::vector<HostName>{},                   // Dummy IPv6
                nullptr,
                routeScope,
                dnsAssignment,
                1500,
                1512,
                false,
                transport
            );

            // Note: In the new architecture, we no longer LoadLibrary("libbox.dll")
            // here. The Go engine (libbox.dll) is already loaded by the frontend
            // App process, and the Go callback function pointer has been registered
            // via VpnBridge_RegisterPlugin before the VPN channel is activated.

        } catch (std::exception const& ex) {
            channel.TerminateConnection(winrt::to_hstring(ex.what()));
        } catch (winrt::hresult_error const& ex) {
            channel.TerminateConnection(ex.message());
        } catch (...) {
            channel.TerminateConnection(L"Unknown error in Connect");
        }
    }

    void VpnPlugin::Disconnect(winrt::Windows::Networking::Vpn::VpnChannel const& channel)
    {
        try {
            channel.Stop();
        } catch (...) {}
        // In the new architecture, we do NOT call Go's CloseService here.
        // The frontend App manages the engine lifecycle directly.
        // Just clean up our own state.
        m_channel = nullptr;
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }

    void VpnPlugin::GetKeepAlivePayload(winrt::Windows::Networking::Vpn::VpnChannel const&, winrt::Windows::Networking::Vpn::VpnPacketBuffer&)
    {
    }

    void VpnPlugin::Encapsulate(winrt::Windows::Networking::Vpn::VpnChannel const& channel, winrt::Windows::Networking::Vpn::VpnPacketBufferList const& packets, winrt::Windows::Networking::Vpn::VpnPacketBufferList const&)
    {
        // Outbound traffic from OS to VPN
        if (!g_onEncapsulate) {
            // No Go handler registered, drain and drop packets
            uint32_t packetCount = packets.Size();
            while (packetCount-- > 0) {
                packets.Append(packets.RemoveAtBegin());
            }
            return;
        }

        uint32_t packetCount = packets.Size();
        while (packetCount-- > 0) {
            auto packet = packets.RemoveAtBegin();
            auto buffer = packet.Buffer();

            // Pass each outbound IP packet to Go via the registered callback
            g_onEncapsulate(buffer.data(), static_cast<size_t>(buffer.Length()));

            // Return the packet buffer to the system pool
            packets.Append(packet);
        }
    }

    void VpnPlugin::Decapsulate(winrt::Windows::Networking::Vpn::VpnChannel const&, winrt::Windows::Networking::Vpn::VpnPacketBuffer const&, winrt::Windows::Networking::Vpn::VpnPacketBufferList const&, winrt::Windows::Networking::Vpn::VpnPacketBufferList const&)
    {
        // Inbound traffic is injected directly via VpnChannel_InjectPacket (called by Go purego)
    }

    bool VpnPlugin::InjectReceivePacket(const uint8_t* data, size_t size)
    {
        if (!m_channel) return false;
        try {
            auto outBuffer = m_channel.GetVpnReceivePacketBuffer();
            auto outBuf = outBuffer.Buffer();
            if (outBuf.Capacity() >= size) {
                if (memcpy_s(outBuf.data(), outBuf.Capacity(), data, size) == 0) {
                    outBuf.Length(static_cast<uint32_t>(size));
                    m_channel.AppendVpnReceivePacketBuffer(outBuffer);
                    m_channel.FlushVpnReceivePacketBuffers();
                    return true;
                }
            }
        } catch (...) {
            // Handle or log error
        }
        return false;
    }
}

// --- DLL entry point ---

extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

// --- Exported C functions for Go purego ---

extern "C" {

    __declspec(dllexport) void VpnBridge_InitCOM()
    {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            // Ignore if already initialized or other error
        }
    }

    __declspec(dllexport) void VpnBridge_RegisterPlugin(uintptr_t onEncapsulate)
    {
        // Store the Go callback function pointer.
        // This is called from Go via purego after syscall.NewCallback(goOnEncapsulate).
        g_onEncapsulate = reinterpret_cast<OnEncapsulateCallback>(onEncapsulate);
    }

    __declspec(dllexport) bool VpnChannel_InjectPacket(const uint8_t* data, size_t size)
    {
        if (winrt::SingTun::implementation::VpnPlugin::s_instance) {
            return winrt::SingTun::implementation::VpnPlugin::s_instance->InjectReceivePacket(data, size);
        }
        return false;
    }

}
