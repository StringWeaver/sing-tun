#pragma once
#include "SingTun/VpnTask.g.h"
#include "VpnPlugin.h"

namespace winrt::SingTun::implementation
{
    struct VpnTask : VpnTaskT<VpnTask>
    {
        VpnTask() = default;

        void Run(winrt::Windows::ApplicationModel::Background::IBackgroundTaskInstance const& taskInstance);
    };
}
namespace winrt::SingTun::factory_implementation
{
    struct VpnTask : VpnTaskT<VpnTask, implementation::VpnTask>
    {
    };
}
