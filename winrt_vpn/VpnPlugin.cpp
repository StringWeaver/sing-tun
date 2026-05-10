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

            // 4. Load the sing-box core DLL (libbox.dll)
            m_goModule = LoadLibraryW(L"libbox.dll");
            if (!m_goModule) {
                channel.TerminateConnection(L"Failed to load libbox.dll");
                return;
            }

            // 5. Resolve exported functions from libbox.dll
            m_boxWinRT_OnEncapsulate = (BoxWinRTEncapsulateFunc)GetProcAddress(m_goModule, "BoxWinRT_OnEncapsulate");
            if (!m_boxWinRT_OnEncapsulate) {
                channel.TerminateConnection(L"Failed to find BoxWinRT_OnEncapsulate in libbox.dll");
                return;
            }

            // 6. Call libbox Setup to initialize the engine
            //    libbox exports: void Setup(SetupOptions* options)
            //    For now we use a simplified setup; the full integration will
            //    pass proper base/working/temp paths from the frontend config.
            typedef int (*BoxSetupFunc)(const char* basePath, const char* workingPath, const char* tempPath);
            auto setupFunc = (BoxSetupFunc)GetProcAddress(m_goModule, "BoxWinRT_Setup");
            // Setup is optional at this stage; the engine may be configured later

            // 7. Call libbox StartOrReloadService with config content
            //    The config should be provided by the frontend app through
            //    some IPC mechanism (file, named pipe, etc.)
            //    For now we just log that the plugin is ready.
            //    TODO: Implement config passing from frontend

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
            // Call libbox CloseService to shut down the engine
            typedef void (*BoxCloseFunc)();
            auto closeFunc = (BoxCloseFunc)GetProcAddress(m_goModule, "BoxWinRT_Close");
            if (closeFunc) {
                closeFunc();
            }
            // Note: Do NOT FreeLibrary the Go DLL. Go runtime expects to stay
            // loaded for the lifetime of the process. FreeLibrary can cause
            // crashes due to leftover goroutines and finalizers.
            m_goModule = nullptr;
            m_boxWinRT_OnEncapsulate = nullptr;
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
        if (!m_boxWinRT_OnEncapsulate) {
            // No handler registered, drain and drop packets
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

            // Pass each outbound IP packet to the Go core via libbox export
            m_boxWinRT_OnEncapsulate(buffer.data(), static_cast<size_t>(buffer.Length()));

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

extern "C" {
    __declspec(dllexport) bool VpnChannel_InjectPacket(const uint8_t* data, size_t size)
    {
        if (winrt::Maple_Task::implementation::VpnPlugin::s_instance) {
            return winrt::Maple_Task::implementation::VpnPlugin::s_instance->InjectReceivePacket(data, size);
        }
        return false;
    }
}
