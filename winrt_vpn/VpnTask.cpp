#include "pch.h"
#include "VpnTask.h"
#include "VpnPlugin.h"

namespace winrt::SingTun::implementation
{
    void VpnTask::Run(Windows::ApplicationModel::Background::IBackgroundTaskInstance const& taskInstance)
    {
        auto plugin = winrt::make<VpnPlugin>();
        Windows::Networking::Vpn::VpnChannel::ProcessEventAsync(plugin, taskInstance.TriggerDetails());
    }
}
