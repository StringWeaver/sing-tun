#include "pch.h"
#include "VpnPlugin.h"

namespace winrt::Maple_Task::implementation
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

            // 4. Load the Go core DLL
            m_goModule = LoadLibraryW(L"tun.dll");
            if (!m_goModule) {
                channel.TerminateConnection(L"Failed to load tun.dll");
                return;
            }

            // 5. Find the start function and call it
            // We assume tun.dll exports: int GoCore_Start()
            typedef int (*GoCoreStartFunc)();
            auto startFunc = (GoCoreStartFunc)GetProcAddress(m_goModule, "GoCore_Start");
            if (startFunc) {
                if (startFunc() != 0) {
                    channel.TerminateConnection(L"GoCore_Start failed");
                    return;
                }
            } else {
                // Not found, maybe it's not needed or different name
            }

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
        StopGoEngine();
    }

    void VpnPlugin::StopGoEngine()
    {
        if (m_goModule) {
            // Find the stop function and call it
            typedef void (*GoCoreStopFunc)();
            auto stopFunc = (GoCoreStopFunc)GetProcAddress(m_goModule, "GoCore_Stop");
            if (stopFunc) {
                stopFunc();
            }
            // Optional: FreeLibrary(m_goModule);
            // In many cases, it's safer to keep the Go DLL loaded or let it handle its own cleanup.
            m_goModule = nullptr;
        }
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
        if (!m_goModule) return;

        typedef void (*GoCoreEncapsulateFunc)(const uint8_t* data, size_t size);
        static auto encapFunc = (GoCoreEncapsulateFunc)GetProcAddress(m_goModule, "GoCore_OnEncapsulate");

        if (!encapFunc) {
            // Drop packets if no handler
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
            
            // Pass to Go core
            encapFunc(buffer.data(), static_cast<size_t>(buffer.Length()));
            
            // Append back to complete the cycle (or let system free it)
            // The WinRT doc says "The plugin should append the buffer to encapsulatedPackets or return it"
            // Wait, for Maple's legitimate hack, we just append it back to packets?
            // "packets.Append(packet);"
            packets.Append(packet);
        }
    }

    void VpnPlugin::Decapsulate(winrt::Windows::Networking::Vpn::VpnChannel const&, winrt::Windows::Networking::Vpn::VpnPacketBuffer const&, winrt::Windows::Networking::Vpn::VpnPacketBufferList const&, winrt::Windows::Networking::Vpn::VpnPacketBufferList const&)
    {
        // Inbound traffic is injected directly via VpnChannel_InjectPacket
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

extern "C" {
    __declspec(dllexport) bool VpnChannel_InjectPacket(const uint8_t* data, size_t size)
    {
        if (winrt::Maple_Task::implementation::VpnPlugin::s_instance) {
            return winrt::Maple_Task::implementation::VpnPlugin::s_instance->InjectReceivePacket(data, size);
        }
        return false;
    }
}
