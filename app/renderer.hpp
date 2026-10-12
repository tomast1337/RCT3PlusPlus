// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal Vulkan renderer: window surface, swapchain, one triangle pipeline and the Dear ImGui backends.
// Needs Vulkan 1.3 (dynamic rendering, synchronization2). Resizing recreates the swapchain.
#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace rct3::app
{

    class Renderer
    {
    public:
        /// Throws std::runtime_error when no suitable device or a Vulkan call fails.
        explicit Renderer(GLFWwindow* window);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        /// Starts an ImGui frame. Build the UI after this and before `draw_frame`.
        void begin_frame();

        /// Finishes the ImGui frame, draws the triangle and the UI, and presents.
        void draw_frame();

        [[nodiscard]] const char* device_name() const
        {
            return device_name_;
        }

    private:
        static constexpr std::uint32_t kFramesInFlight = 2;

        struct Frame
        {
            VkCommandPool pool = VK_NULL_HANDLE;
            VkCommandBuffer commands = VK_NULL_HANDLE;
            VkSemaphore image_available = VK_NULL_HANDLE;
            VkFence in_flight = VK_NULL_HANDLE;
        };

        void create_instance();
        void pick_device();
        void create_device();
        void create_swapchain();
        void destroy_swapchain();
        void recreate_swapchain();
        void create_frames();
        void create_pipeline();
        void init_imgui();
        void record(VkCommandBuffer cmd, std::uint32_t image_index);

        GLFWwindow* window_ = nullptr;

        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
        char device_name_[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = { };
        std::uint32_t queue_family_ = 0;
        VkDevice device_ = VK_NULL_HANDLE;
        VkQueue queue_ = VK_NULL_HANDLE;

        VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        VkExtent2D extent_ { };
        std::vector<VkImage> images_;
        std::vector<VkImageView> views_;
        std::vector<VkSemaphore> render_finished_; // One per swapchain image

        VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;

        std::array<Frame, kFramesInFlight> frames_ { };
        std::uint32_t frame_index_ = 0;
        bool imgui_ready_ = false;
    };

} // namespace rct3::app
