// SPDX-License-Identifier: GPL-3.0-or-later
//
// Error handling shared by the Vulkan backend's sources. A failing Vulkan call after init is a driver or
// device loss problem, not an expected condition, so it throws; `init` and the create functions turn that
// into an error value at the API boundary.
#pragma once

#include <stdexcept>
#include <string>
#include <vulkan/vulkan.h>

namespace rct3::render::vulkan
{

    inline void check(VkResult result, const char* what)
    {
        if (result != VK_SUCCESS)
            throw std::runtime_error(std::string(what) + " failed (VkResult " + std::to_string(result) + ")");
    }

} // namespace rct3::render::vulkan
