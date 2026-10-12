// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Vulkan backend. Needs Vulkan 1.3 (dynamic rendering, synchronization2). Two frames in flight, one
// depth buffer, swapchain recreated when the window changes size. Buffers live in host-visible memory for
// now; a staging path and an allocator come with bigger meshes.
#pragma once

#include "render/device.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace rct3::render::vulkan
{

    class VulkanDevice final : public Device
    {
    public:
        ~VulkanDevice() override;

        [[nodiscard]] std::expected<void, std::string> init(const DeviceConfig& config) override;
        void shutdown() override;
        [[nodiscard]] Backend backend() const override
        {
            return Backend::Vulkan;
        }
        [[nodiscard]] std::string_view device_name() const override
        {
            return device_name_;
        }
        [[nodiscard]] Extent extent() const override
        {
            return { extent_.width, extent_.height };
        }

        [[nodiscard]] std::expected<BufferHandle, std::string> create_buffer(const BufferDesc& desc) override;
        void destroy_buffer(BufferHandle buffer) override;
        [[nodiscard]] std::expected<PipelineHandle, std::string> create_pipeline(const PipelineDesc& desc) override;
        void destroy_pipeline(PipelineHandle pipeline) override;

        void begin_frame() override;
        void begin_pass(const PassDesc& pass) override;
        void draw(const DrawCommand& command) override;
        void end_pass() override;
        void end_frame() override;
        [[nodiscard]] FrameStats stats() const override
        {
            return last_stats_;
        }

        void ui_new_frame() override;
        void draw_ui() override;

    private:
        static constexpr std::uint32_t kFramesInFlight = 2;

        struct Buffer
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
        };

        struct Pipeline
        {
            VkPipeline pipeline = VK_NULL_HANDLE;
            Topology topology = Topology::Triangles;
        };

        // Objects released once the frame slot that last used them has finished on the GPU.
        struct Garbage
        {
            std::vector<Buffer> buffers;
            std::vector<VkPipeline> pipelines;
        };

        struct Frame
        {
            VkCommandPool pool = VK_NULL_HANDLE;
            VkCommandBuffer commands = VK_NULL_HANDLE;
            VkSemaphore image_available = VK_NULL_HANDLE;
            VkFence in_flight = VK_NULL_HANDLE;
            Garbage garbage;
        };

        enum class State : std::uint8_t
        {
            Off,
            Idle,
            InFrame,
            InPass,
        };

        // Setup, in call order (vulkan_device.cpp)
        void create_instance();
        void pick_device();
        void create_device();
        void create_swapchain();
        void create_depth();
        void create_frames();
        void create_pipeline_layout();
        void init_ui();

        void destroy_swapchain();
        void recreate_swapchain();
        void release(Garbage& garbage);
        [[nodiscard]] Garbage& garbage_slot();

        // Resources (vulkan_resources.cpp)
        [[nodiscard]] std::uint32_t find_memory_type(std::uint32_t type_bits, VkMemoryPropertyFlags properties) const;

        DeviceConfig config_;
        State state_ = State::Off;

        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
        char device_name_[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = { };
        std::uint32_t queue_family_ = 0;
        VkDevice device_ = VK_NULL_HANDLE;
        VkQueue queue_ = VK_NULL_HANDLE;

        VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
        VkFormat color_format_ = VK_FORMAT_UNDEFINED;
        VkExtent2D extent_ { };
        std::vector<VkImage> images_;
        std::vector<VkImageView> views_;
        std::vector<VkSemaphore> render_finished_; // One per swapchain image

        static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
        VkImage depth_image_ = VK_NULL_HANDLE;
        VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
        VkImageView depth_view_ = VK_NULL_HANDLE;

        VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

        std::array<Frame, kFramesInFlight> frames_ { };
        std::uint32_t frame_index_ = 0;
        std::uint32_t image_index_ = 0;
        bool ui_ready_ = false;

        std::uint32_t next_id_ = 1;
        std::unordered_map<std::uint32_t, Buffer> buffers_;
        std::unordered_map<std::uint32_t, Pipeline> pipelines_;

        FrameStats frame_stats_;
        FrameStats last_stats_;
    };

} // namespace rct3::render::vulkan
