// SPDX-License-Identifier: GPL-3.0-or-later
//
// Error handling and helpers shared by the Vulkan backend's sources. A failing Vulkan call after init is a driver or
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

    struct Barrier
    {
        VkImageLayout from;
        VkImageLayout to;
        VkPipelineStageFlags2 src_stage;
        VkAccessFlags2 src_access;
        VkPipelineStageFlags2 dst_stage;
        VkAccessFlags2 dst_access;
    };

    inline void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, const Barrier& b)
    {
        VkImageMemoryBarrier2 barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = b.src_stage;
        barrier.srcAccessMask = b.src_access;
        barrier.dstStageMask = b.dst_stage;
        barrier.dstAccessMask = b.dst_access;
        barrier.oldLayout = b.from;
        barrier.newLayout = b.to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = { aspect, 0, 1, 0, 1 };
        VkDependencyInfo dependency { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
    }

} // namespace rct3::render::vulkan
