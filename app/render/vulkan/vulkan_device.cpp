// SPDX-License-Identifier: GPL-3.0-or-later
//
// Vulkan backend: setup, shutdown, swapchain and the frame loop. Resource creation is in vulkan_resources.cpp.
#include "render/vulkan/vulkan_device.hpp"

#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "logging/log.hpp"
#include "profiling/profiler.hpp"
#include "render/vulkan/vulkan_check.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <tuple>

namespace rct3::render::vulkan
{

    namespace
    {

        // What every shader finds in its push constants (app/shaders): the draw's transform, then the light of
        // the Lit shaders, or what the Water shader needs (app/shaders/water.slang). 128 bytes is all a device
        // must offer.
        struct PushBlock
        {
            glm::mat4 transform;
            glm::vec4 a;
            glm::vec4 b;
            glm::vec4 c;
            glm::vec4 d;
        };
        static_assert(sizeof(PushBlock) == 128);

        constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        PushBlock push_block(const DrawCommand& command, Shader shader)
        {
            const SceneLight& l = command.light;
            if (shader == Shader::Occlusion)
            {
                // app/shaders/occlusion.slang
                const OcclusionStep& o = command.occlusion;
                return { command.transform, glm::vec4(static_cast<float>(o.kind), static_cast<float>(o.frame % 65536), o.radius, o.strength), glm::vec4(o.history, 0.0f, o.depth_terms), { }, { } };
            }
            if (shader != Shader::Water)
            {
                // The push block is full, so the haze colour goes as one number: its channels in 8 bits each.
                const glm::uvec3 h { glm::round(glm::clamp(l.haze, 0.0f, 1.0f) * 255.0f) };
                const auto haze = static_cast<float>((h.r << 16) | (h.g << 8) | h.b);
                // sky.w: 1 with the variation, 2 with the relief map, plus 4 times the surface detail, plus 16 with
                // lamps, plus 32 for a ghost, plus the scroll as its fraction (lit.slang). sun_direction.w: 0 without a shadow, else 1 plus the older shadow's weight.
                const bool before = static_cast<bool>(l.shadow_map_before);
                const float shadow = l.shadow_map && l.shadow_view ? 1 + (before ? std::clamp(l.shadow_before, 0.0f, 1.0f) : 0.0f) : 0.0f;
                return { command.transform, glm::vec4(l.sun_direction, shadow), glm::vec4(l.sun, l.haze_rate), glm::vec4(l.sky, (command.extra_textures[0] ? 1.0f : 0.0f) + (command.extra_textures[1] ? 2.0f : 0.0f) + 4.0f * static_cast<float>(std::clamp(l.surface_detail, 0, 2)) + (l.lamps ? 16.0f : 0.0f) + (l.ghost ? 32.0f : 0.0f) + (command.scroll - std::floor(command.scroll))), glm::vec4(l.ground, haze) };
            }
            const WaterView& w = command.water;
            const bool shadow = l.shadow_map && l.shadow_view;
            const auto flags = static_cast<float>((w.reflection ? 1 : 0) | (w.refraction ? 2 : 0) | (w.waves ? 4 : 0) | (w.foam ? 8 : 0) | (w.ripples ? 16 : 0) | (w.underwater ? 32 : 0) | (shadow ? 64 : 0) | (w.shafts ? 128 : 0));
            return { command.transform, glm::vec4(w.eye, w.time), glm::vec4(l.sun_direction, w.near), glm::vec4(l.sun, w.far), glm::vec4(l.sky, flags) };
        }

        void check_imgui(VkResult result)
        {
            if (result != VK_SUCCESS)
                throw std::runtime_error(std::format("ImGui Vulkan backend error {}", static_cast<int>(result)));
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL on_debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
        {
            static const auto log = logging::logger("vulkan");
            if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                log.error("{}", data->pMessage);
            else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                log.warning("{}", data->pMessage);
            else
                log.debug("{}", data->pMessage);
            return VK_FALSE;
        }

        bool has_layer(const char* name)
        {
            std::uint32_t count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> layers(count);
            vkEnumerateInstanceLayerProperties(&count, layers.data());
            return std::ranges::any_of(layers, [name](const VkLayerProperties& l) { return std::strcmp(l.layerName, name) == 0; });
        }

        // The flip makes clip space Y point up, as the shared conventions in render/types.hpp promise.
        void set_viewport(VkCommandBuffer cmd, VkRect2D area)
        {
            const auto width = static_cast<float>(area.extent.width);
            const auto height = static_cast<float>(area.extent.height);
            const VkViewport viewport { static_cast<float>(area.offset.x), static_cast<float>(area.offset.y) + height, width, -height, 0, 1 };
            const VkRect2D scissor = area;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }

    } // namespace

    VulkanDevice::~VulkanDevice()
    {
        shutdown();
    }

    std::expected<void, std::string> VulkanDevice::init(const DeviceConfig& config)
    {
        assert(state_ == State::Off);
        if (config.window == nullptr)
            return std::unexpected("no window");
        config_ = config;
        try
        {
            create_instance();
            check(glfwCreateWindowSurface(instance_, config_.window, nullptr, &surface_), "glfwCreateWindowSurface");
            pick_device();
            create_device();
            create_swapchain();
            create_depth();
            create_frames();
            create_pipeline_layout();
            create_samplers();
            if (config_.ui)
                init_ui();
        }
        catch (const std::exception& e)
        {
            state_ = State::Idle; // So shutdown releases what was created
            shutdown();
            return std::unexpected(e.what());
        }
        state_ = State::Idle;
        return { };
    }

    void VulkanDevice::shutdown()
    {
        if (state_ == State::Off)
            return;
        if (device_ != VK_NULL_HANDLE)
            vkDeviceWaitIdle(device_);
        if (ui_ready_)
        {
            ImGui_ImplVulkan_Shutdown();
            ui_ready_ = false;
        }
        if (device_ != VK_NULL_HANDLE)
        {
            // Garbage first: freeing a texture's descriptor set needs the pool still alive.
            for (Frame& f : frames_)
                release(f.garbage);
            for (auto& [_, b] : buffers_) // Includes the transient blocks
                destroy(b);
            destroy(reading_);
            for (auto& [_, t] : textures_)
                destroy(t);
            for (auto& [_, p] : pipelines_)
            {
                vkDestroyPipeline(device_, p.pipeline, nullptr);
                vkDestroyPipeline(device_, p.wire, nullptr);
                vkDestroyPipeline(device_, p.multi, nullptr);
                vkDestroyPipeline(device_, p.multi_wire, nullptr);
            }
            for (VkSampler s : samplers_)
                vkDestroySampler(device_, s, nullptr);
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr); // Frees every set
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            vkDestroyDescriptorSetLayout(device_, texture_set_layout_, nullptr);
            vkDestroyDescriptorSetLayout(device_, sampler_set_layout_, nullptr);
            vkDestroyCommandPool(device_, upload_pool_, nullptr);
            for (Frame& f : frames_)
            {
                vkDestroyFence(device_, f.in_flight, nullptr);
                if (f.timings != VK_NULL_HANDLE)
                    vkDestroyQueryPool(device_, f.timings, nullptr);
                vkDestroySemaphore(device_, f.image_available, nullptr);
                vkDestroyCommandPool(device_, f.pool, nullptr);
            }
            destroy_swapchain();
            vkDestroyDevice(device_, nullptr);
        }
        if (instance_ != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            if (messenger_ != VK_NULL_HANDLE)
            {
                auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
                if (destroy)
                    destroy(instance_, messenger_, nullptr);
            }
            vkDestroyInstance(instance_, nullptr);
        }

        buffers_.clear();
        textures_.clear();
        pipelines_.clear();
        samplers_ = { };
        sampler_sets_ = { };
        descriptor_pool_ = VK_NULL_HANDLE;
        pipeline_layout_ = VK_NULL_HANDLE;
        texture_set_layout_ = VK_NULL_HANDLE;
        sampler_set_layout_ = VK_NULL_HANDLE;
        upload_pool_ = VK_NULL_HANDLE;
        frames_ = { };
        frame_index_ = 0;
        device_ = VK_NULL_HANDLE;
        queue_ = VK_NULL_HANDLE;
        gpu_ = VK_NULL_HANDLE;
        surface_ = VK_NULL_HANDLE;
        messenger_ = VK_NULL_HANDLE;
        instance_ = VK_NULL_HANDLE;
        state_ = State::Off;
    }

    void VulkanDevice::create_instance()
    {
        std::uint32_t glfw_count = 0;
        const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_count);
        if (glfw_extensions == nullptr)
            throw std::runtime_error("GLFW found no Vulkan support");
        std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_count);

        std::vector<const char*> layers;
        const bool validate = config_.validation && has_layer("VK_LAYER_KHRONOS_validation");
        if (validate)
        {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkApplicationInfo app { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "rct3++";
        app.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo info { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        info.pApplicationInfo = &app;
        info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.data();
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");

        if (validate)
        {
            VkDebugUtilsMessengerCreateInfoEXT messenger { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messenger.pfnUserCallback = on_debug_message;
            auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
            if (create)
                create(instance_, &messenger, nullptr, &messenger_);
        }
    }

    void VulkanDevice::pick_device()
    {
        std::uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        int best_score = -1;
        for (VkPhysicalDevice device : devices)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            if (props.apiVersion < VK_API_VERSION_1_3)
                continue;

            // A queue family that does both graphics and presenting.
            std::uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());
            int family = -1;
            for (std::uint32_t i = 0; i < family_count; ++i)
            {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present);
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
                {
                    family = static_cast<int>(i);
                    break;
                }
            }
            if (family < 0)
                continue;

            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > best_score)
            {
                best_score = score;
                gpu_ = device;
                queue_family_ = static_cast<std::uint32_t>(family);
                std::memcpy(device_name_, props.deviceName, sizeof device_name_);
            }
        }
        if (gpu_ == VK_NULL_HANDLE)
            throw std::runtime_error("no Vulkan 1.3 device that can draw to the window");
    }

    void VulkanDevice::create_device()
    {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queue.queueFamilyIndex = queue_family_;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;

        VkPhysicalDeviceVulkan13Features features13 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;

        VkPhysicalDeviceFeatures supported { };
        vkGetPhysicalDeviceFeatures(gpu_, &supported);
        line_fill_ = supported.fillModeNonSolid == VK_TRUE;
        VkPhysicalDeviceFeatures enabled { };
        enabled.fillModeNonSolid = supported.fillModeNonSolid;

        // The ImGui backend asks for the dynamic rendering extension by name even on Vulkan 1.3.
        const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME };
        VkDeviceCreateInfo info { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        info.pNext = &features13;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue;
        info.enabledExtensionCount = 2;
        info.ppEnabledExtensionNames = extensions;
        info.pEnabledFeatures = &enabled;
        check(vkCreateDevice(gpu_, &info, nullptr, &device_), "vkCreateDevice");
        vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    }

    void VulkanDevice::create_swapchain()
    {
        VkSurfaceCapabilitiesKHR caps;
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps), "surface capabilities");

        std::uint32_t format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &format_count, formats.data());
        // UNORM keeps ImGui colours as authored; an sRGB target would wash the UI out.
        VkSurfaceFormatKHR chosen = formats.at(0);
        for (const auto& f : formats)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                chosen = f;
        }
        color_format_ = chosen.format;

        if (caps.currentExtent.width != 0xFFFFFFFFu)
        {
            extent_ = caps.currentExtent;
        }
        else
        {
            int w = 0, h = 0;
            glfwGetFramebufferSize(config_.window, &w, &h);
            extent_.width = std::clamp(static_cast<std::uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent_.height = std::clamp(static_cast<std::uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        std::uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0)
            image_count = std::min(image_count, caps.maxImageCount);

        // FIFO is always available. Without vsync prefer MAILBOX (no tearing), then IMMEDIATE.
        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
        if (!config_.vsync)
        {
            std::uint32_t mode_count = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &mode_count, nullptr);
            std::vector<VkPresentModeKHR> modes(mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &mode_count, modes.data());
            for (VkPresentModeKHR wanted : { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR })
            {
                if (std::ranges::contains(modes, wanted))
                {
                    present_mode = wanted;
                    break;
                }
            }
        }

        VkSwapchainKHR old = swapchain_;
        VkSwapchainCreateInfoKHR info { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        info.surface = surface_;
        info.minImageCount = image_count;
        info.imageFormat = chosen.format;
        info.imageColorSpace = chosen.colorSpace;
        info.imageExtent = extent_;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = present_mode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = old;
        check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");
        if (old != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(device_, old, nullptr);

        std::uint32_t count = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
        images_.resize(count);
        vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());

        views_.resize(count);
        render_finished_.resize(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            VkImageViewCreateInfo view { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            view.image = images_[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = color_format_;
            view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            check(vkCreateImageView(device_, &view, nullptr, &views_[i]), "vkCreateImageView");

            VkSemaphoreCreateInfo semaphore { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            check(vkCreateSemaphore(device_, &semaphore, nullptr, &render_finished_[i]), "vkCreateSemaphore");
        }
    }

    // One depth buffer serves both frames in flight: the barrier at the start of each frame orders its depth
    // writes after the previous frame's.
    void VulkanDevice::create_depth()
    {
        VkImageCreateInfo image { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = kDepthFormat;
        image.extent = { extent_.width, extent_.height, 1 };
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // For copy_screen
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device_, &image, nullptr, &depth_image_), "vkCreateImage");

        VkMemoryRequirements needs;
        vkGetImageMemoryRequirements(device_, depth_image_, &needs);
        VkMemoryAllocateInfo alloc { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        alloc.allocationSize = needs.size;
        alloc.memoryTypeIndex = find_memory_type(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_, &alloc, nullptr, &depth_memory_), "vkAllocateMemory");
        check(vkBindImageMemory(device_, depth_image_, depth_memory_, 0), "vkBindImageMemory");

        VkImageViewCreateInfo view { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view.image = depth_image_;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = kDepthFormat;
        view.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        check(vkCreateImageView(device_, &view, nullptr, &depth_view_), "vkCreateImageView");
        create_multisample();
    }

    // Like the depth buffer, the multisampled images serve both frames in flight.
    void VulkanDevice::create_multisample()
    {
        if (samples_ <= 1)
            return;
        const auto make = [&](VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
            VkImageCreateInfo info { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            info.imageType = VK_IMAGE_TYPE_2D;
            info.format = format;
            info.extent = { extent_.width, extent_.height, 1 };
            info.mipLevels = 1;
            info.arrayLayers = 1;
            info.samples = static_cast<VkSampleCountFlagBits>(samples_);
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = usage;
            info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            check(vkCreateImage(device_, &info, nullptr, &image), "vkCreateImage");
            VkMemoryRequirements needs;
            vkGetImageMemoryRequirements(device_, image, &needs);
            VkMemoryAllocateInfo alloc { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            alloc.allocationSize = needs.size;
            alloc.memoryTypeIndex = find_memory_type(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(device_, &alloc, nullptr, &memory), "vkAllocateMemory");
            check(vkBindImageMemory(device_, image, memory, 0), "vkBindImageMemory");
            VkImageViewCreateInfo v { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            v.image = image;
            v.viewType = VK_IMAGE_VIEW_TYPE_2D;
            v.format = format;
            v.subresourceRange = { aspect, 0, 1, 0, 1 };
            check(vkCreateImageView(device_, &v, nullptr, &view), "vkCreateImageView");
        };
        make(color_format_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, multi_color_image_, multi_color_memory_, multi_color_view_);
        make(kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, multi_depth_image_, multi_depth_memory_, multi_depth_view_);
    }

    void VulkanDevice::destroy_multisample()
    {
        vkDestroyImageView(device_, multi_color_view_, nullptr);
        vkDestroyImage(device_, multi_color_image_, nullptr);
        vkFreeMemory(device_, multi_color_memory_, nullptr);
        vkDestroyImageView(device_, multi_depth_view_, nullptr);
        vkDestroyImage(device_, multi_depth_image_, nullptr);
        vkFreeMemory(device_, multi_depth_memory_, nullptr);
        multi_color_view_ = multi_depth_view_ = VK_NULL_HANDLE;
        multi_color_image_ = multi_depth_image_ = VK_NULL_HANDLE;
        multi_color_memory_ = multi_depth_memory_ = VK_NULL_HANDLE;
    }

    std::vector<std::uint32_t> VulkanDevice::sample_levels() const
    {
        if (gpu_ == VK_NULL_HANDLE)
            return { };
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu_, &props);
        const VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
        std::vector<std::uint32_t> levels;
        for (const std::uint32_t n : { 2u, 4u, 8u })
            if ((counts & n) != 0)
                levels.push_back(n);
        return levels;
    }

    void VulkanDevice::set_samples(std::uint32_t samples)
    {
        assert(state_ == State::Idle);
        if (samples != 1 && !std::ranges::contains(sample_levels(), samples))
            samples = 1;
        if (samples == samples_)
            return;
        vkDeviceWaitIdle(device_);
        destroy_multisample();
        samples_ = samples;
        create_multisample();
        if (ui_ready_)
        {
            ImGui_ImplVulkan_PipelineInfo info = ui_pipeline_info();
            ImGui_ImplVulkan_CreateMainPipeline(&info);
        }
    }

    ImGui_ImplVulkan_PipelineInfo VulkanDevice::ui_pipeline_info() const
    {
        ImGui_ImplVulkan_PipelineInfo info { };
        info.MSAASamples = static_cast<VkSampleCountFlagBits>(samples_);
        info.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &color_format_;
        info.PipelineRenderingCreateInfo.depthAttachmentFormat = kDepthFormat;
        return info;
    }

    // Destroys the swapchain and everything sized to it.
    void VulkanDevice::destroy_swapchain()
    {
        destroy_multisample();
        vkDestroyImageView(device_, depth_view_, nullptr);
        vkDestroyImage(device_, depth_image_, nullptr);
        vkFreeMemory(device_, depth_memory_, nullptr);
        depth_view_ = VK_NULL_HANDLE;
        depth_image_ = VK_NULL_HANDLE;
        depth_memory_ = VK_NULL_HANDLE;

        for (VkImageView v : views_)
            vkDestroyImageView(device_, v, nullptr);
        for (VkSemaphore s : render_finished_)
            vkDestroySemaphore(device_, s, nullptr);
        views_.clear();
        render_finished_.clear();
        images_.clear();
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    void VulkanDevice::recreate_swapchain()
    {
        // A minimised window has no framebuffer; wait until it is back.
        int w = 0, h = 0;
        glfwGetFramebufferSize(config_.window, &w, &h);
        while (w == 0 || h == 0)
        {
            glfwWaitEvents();
            glfwGetFramebufferSize(config_.window, &w, &h);
        }
        vkDeviceWaitIdle(device_);
        destroy_swapchain();
        create_swapchain();
        create_depth();
    }

    void VulkanDevice::create_frames()
    {
        for (Frame& f : frames_)
        {
            VkCommandPoolCreateInfo pool { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool.queueFamilyIndex = queue_family_;
            check(vkCreateCommandPool(device_, &pool, nullptr, &f.pool), "vkCreateCommandPool");

            VkCommandBufferAllocateInfo alloc { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            alloc.commandPool = f.pool;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device_, &alloc, &f.commands), "vkAllocateCommandBuffers");

            VkSemaphoreCreateInfo semaphore { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            check(vkCreateSemaphore(device_, &semaphore, nullptr, &f.image_available), "vkCreateSemaphore");

            VkFenceCreateInfo fence { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(vkCreateFence(device_, &fence, nullptr, &f.in_flight), "vkCreateFence");
        }
        // The passes' GPU times for the profiler, where the queue keeps time.
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(gpu_, &props);
            std::uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &family_count, families.data());
            if (props.limits.timestampPeriod > 0 && queue_family_ < family_count && families[queue_family_].timestampValidBits > 0)
            {
                stamp_ms_ = props.limits.timestampPeriod * 1e-6f;
                for (Frame& f : frames_)
                {
                    VkQueryPoolCreateInfo query { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
                    query.queryType = VK_QUERY_TYPE_TIMESTAMP;
                    query.queryCount = kMostStamps;
                    check(vkCreateQueryPool(device_, &query, nullptr, &f.timings), "vkCreateQueryPool");
                }
            }
        }

        VkCommandPoolCreateInfo pool { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = queue_family_;
        check(vkCreateCommandPool(device_, &pool, nullptr, &upload_pool_), "vkCreateCommandPool");
    }

    // All pipelines share one layout: a push constant block with the draw's transform and light, then a texture set
    // and a sampler set that only textured shaders read. Also creates the pool those sets come from.
    void VulkanDevice::create_pipeline_layout()
    {
        const auto make_set_layout = [&](VkDescriptorType type) {
            VkDescriptorSetLayoutBinding binding { };
            binding.descriptorType = type;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            info.bindingCount = 1;
            info.pBindings = &binding;
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            check(vkCreateDescriptorSetLayout(device_, &info, nullptr, &layout), "vkCreateDescriptorSetLayout");
            return layout;
        };
        texture_set_layout_ = make_set_layout(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        sampler_set_layout_ = make_set_layout(VK_DESCRIPTOR_TYPE_SAMPLER);

        // Texture count is a guess at what the UI and a park need at once; raise it when it runs out.
        constexpr std::uint32_t kMaxTextures = 4096;
        const VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures },
            { VK_DESCRIPTOR_TYPE_SAMPLER, kSamplerCount },
        };
        VkDescriptorPoolCreateInfo pool { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets = kMaxTextures + kSamplerCount;
        pool.poolSizeCount = 2;
        pool.pPoolSizes = sizes;
        check(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptor_pool_), "vkCreateDescriptorPool");

        VkPushConstantRange push { };
        push.stageFlags = kPushStages;
        push.size = sizeof(PushBlock);

        // The extra textures take a set each, which only devices that bind enough sets get; the least Vulkan
        // promises is four, enough for the Lit shaders' shadow. Without eight the Water shader is not offered.
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu_, &props);
        set_count_ = props.limits.maxBoundDescriptorSets >= kSetCount ? kSetCount : 4;
        std::array<VkDescriptorSetLayout, kSetCount> sets { };
        sets.fill(texture_set_layout_);
        sets[1] = sampler_set_layout_;
        VkPipelineLayoutCreateInfo layout { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layout.setLayoutCount = set_count_;
        layout.pSetLayouts = sets.data();
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipeline_layout_), "vkCreatePipelineLayout");
    }

    // One sampler per Filter x AddressMode pair, at index filter * 2 + address.
    void VulkanDevice::create_samplers()
    {
        for (std::size_t i = 0; i < kSamplerCount; ++i)
        {
            const VkFilter filter = i / 2 == 0 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            const VkSamplerAddressMode address = i % 2 == 0 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
            VkSamplerCreateInfo info { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            info.magFilter = filter;
            info.minFilter = filter;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.addressModeU = address;
            info.addressModeV = address;
            info.addressModeW = address;
            info.maxLod = VK_LOD_CLAMP_NONE;
            check(vkCreateSampler(device_, &info, nullptr, &samplers_[i]), "vkCreateSampler");

            VkDescriptorSetAllocateInfo alloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            alloc.descriptorPool = descriptor_pool_;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &sampler_set_layout_;
            check(vkAllocateDescriptorSets(device_, &alloc, &sampler_sets_[i]), "vkAllocateDescriptorSets");

            VkDescriptorImageInfo image { };
            image.sampler = samplers_[i];
            VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = sampler_sets_[i];
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            write.pImageInfo = &image;
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    }

    void VulkanDevice::init_ui()
    {
        ImGui_ImplVulkan_InitInfo info { };
        info.ApiVersion = VK_API_VERSION_1_3;
        info.Instance = instance_;
        info.PhysicalDevice = gpu_;
        info.Device = device_;
        info.QueueFamily = queue_family_;
        info.Queue = queue_;
        info.DescriptorPoolSize = 16;
        info.MinImageCount = 2;
        info.ImageCount = static_cast<std::uint32_t>(images_.size());
        info.UseDynamicRendering = true;
        info.PipelineInfoMain = ui_pipeline_info();
        info.CheckVkResultFn = check_imgui;
        if (!ImGui_ImplVulkan_Init(&info))
            throw std::runtime_error("ImGui Vulkan backend init failed");
        ui_ready_ = true;
    }

    void VulkanDevice::release(Garbage& garbage)
    {
        for (const Buffer& b : garbage.buffers)
            destroy(b);
        for (const Texture& t : garbage.textures)
            destroy(t);
        for (VkPipeline p : garbage.pipelines)
            vkDestroyPipeline(device_, p, nullptr);
        garbage.buffers.clear();
        garbage.textures.clear();
        garbage.pipelines.clear();
    }

    // Recording a frame puts released objects in its own slot. Between frames they go to the slot of the
    // frame submitted last, which is the newest one that can still use them.
    VulkanDevice::Garbage& VulkanDevice::garbage_slot()
    {
        if (state_ == State::InFrame || state_ == State::InPass)
            return frames_[frame_index_].garbage;
        return frames_[(frame_index_ + kFramesInFlight - 1) % kFramesInFlight].garbage;
    }

    // Records commands with `record` and runs them at once, waiting for the GPU. For setting up resources.
    void VulkanDevice::submit_once(const std::function<void(VkCommandBuffer)>& record)
    {
        VkCommandBufferAllocateInfo alloc { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        alloc.commandPool = upload_pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(device_, &alloc, &cmd), "vkAllocateCommandBuffers");
        try
        {
            VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");
            record(cmd);
            check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

            VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cmd;
            check(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
            check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle");
        }
        catch (...)
        {
            vkFreeCommandBuffers(device_, upload_pool_, 1, &cmd);
            throw;
        }
        vkFreeCommandBuffers(device_, upload_pool_, 1, &cmd);
    }

    // Copies staging into the whole image and leaves it ready for sampling. Waits for the GPU.
    void VulkanDevice::upload(const Buffer& staging, const Texture& texture, std::uint32_t width, std::uint32_t height)
    {
        submit_once([&](VkCommandBuffer cmd) {
            image_barrier(cmd, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT });
            VkBufferImageCopy region { };
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageExtent = { width, height, 1 };
            vkCmdCopyBufferToImage(cmd, staging.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            image_barrier(cmd, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT });
        });
    }

    // Clears a target's depth to the far plane, leaves it ready for sampling, and registers it as texture `id`.
    void VulkanDevice::make_depth_readable(Texture& texture, std::uint32_t id)
    {
        submit_once([&](VkCommandBuffer cmd) {
            image_barrier(cmd, texture.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT });
            const VkClearDepthStencilValue far { 1.0f, 0 };
            const VkImageSubresourceRange range { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            vkCmdClearDepthStencilImage(cmd, texture.depth_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &far, 1, &range);
            image_barrier(cmd, texture.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT });
        });
        Texture depth;
        VkDescriptorSetAllocateInfo set { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        set.descriptorPool = descriptor_pool_;
        set.descriptorSetCount = 1;
        set.pSetLayouts = &texture_set_layout_;
        check(vkAllocateDescriptorSets(device_, &set, &depth.set), "vkAllocateDescriptorSets");
        VkDescriptorImageInfo info { };
        info.imageView = texture.depth_view; // Owned by the target
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = depth.set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &info;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        depth.extent = texture.extent;
        textures_.emplace(id, depth);
        texture.depth_id = id;
    }

    void VulkanDevice::ui_new_frame()
    {
        if (ui_ready_)
            ImGui_ImplVulkan_NewFrame();
    }

    void VulkanDevice::begin_frame()
    {
        assert(state_ == State::Idle);
        Frame& frame = frames_[frame_index_];
        check(vkWaitForFences(device_, 1, &frame.in_flight, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        if (frame.timings != VK_NULL_HANDLE && frame.stamps > 0 && profiling::enabled())
        {
            std::array<std::uint64_t, kMostStamps> stamps { };
            if (vkGetQueryPoolResults(device_, frame.timings, 0, frame.stamps, sizeof stamps, stamps.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            {
                for (std::uint32_t k = 0; k + 1 < frame.stamps; k += 2)
                    profiling::record(std::format("GPU/Pass {}", k / 2), static_cast<double>(stamps[k + 1] - stamps[k]) * stamp_ms_);
                profiling::record("GPU", static_cast<double>(stamps[frame.stamps - 1] - stamps[0]) * stamp_ms_);
            }
        }
        release(frame.garbage);
        reset_transient(frame);

        for (;;)
        {
            const VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.image_available, VK_NULL_HANDLE, &image_index_);
            if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                recreate_swapchain();
                continue;
            }
            if (result != VK_SUBOPTIMAL_KHR)
                check(result, "vkAcquireNextImageKHR");
            break;
        }

        // Only reset once we are sure to submit, otherwise the next wait would never return.
        check(vkResetFences(device_, 1, &frame.in_flight), "vkResetFences");
        check(vkResetCommandBuffer(frame.commands, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(frame.commands, &begin), "vkBeginCommandBuffer");
        frame.stamps = 0;
        if (frame.timings != VK_NULL_HANDLE)
            vkCmdResetQueryPool(frame.commands, frame.timings, 0, kMostStamps);

        image_barrier(frame.commands, images_[image_index_], VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT });
        constexpr VkPipelineStageFlags2 kDepthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        // The depth buffer is also written by the resolve of a multisampled depth, as colour output.
        constexpr VkPipelineStageFlags2 kDepthWriters = kDepthStages | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        constexpr VkAccessFlags2 kDepthWrites = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        image_barrier(frame.commands, depth_image_, VK_IMAGE_ASPECT_DEPTH_BIT, { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthWriters, kDepthWrites, kDepthWriters, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | kDepthWrites });
        if (samples_ > 1)
        {
            image_barrier(frame.commands, multi_color_image_, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT });
            image_barrier(frame.commands, multi_depth_image_, VK_IMAGE_ASPECT_DEPTH_BIT, { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, kDepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT });
        }

        frame_stats_ = { };
        state_ = State::InFrame;
    }

    void VulkanDevice::begin_pass(const PassDesc& pass)
    {
        assert(state_ == State::InFrame);
        const VkCommandBuffer cmd = frames_[frame_index_].commands;
        pass_target_ = nullptr;
        if (pass.target)
        {
            const auto found = textures_.find(pass.target.id);
            assert(found != textures_.end() && found->second.depth_view != VK_NULL_HANDLE && "pass into a texture that is not a target");
            if (found != textures_.end() && found->second.depth_view != VK_NULL_HANDLE)
                pass_target_ = &found->second;
        }
        if (pass_target_ != nullptr)
        {
            // Drawing waits for the reads of what it last held; its depth starts afresh each time.
            image_barrier(cmd, pass_target_->image, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT });
            constexpr VkPipelineStageFlags2 kDepthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            image_barrier(cmd, pass_target_->depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, kDepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT });
        }
        const VkExtent2D area = pass_target_ != nullptr ? pass_target_->extent : extent_;

        VkRenderingAttachmentInfo color { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        color.imageView = pass_target_ != nullptr ? pass_target_->view : views_[image_index_];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = pass.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = { { pass.clear_color.r, pass.clear_color.g, pass.clear_color.b, pass.clear_color.a } };

        VkRenderingAttachmentInfo depth { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        depth.imageView = pass_target_ != nullptr ? pass_target_->depth_view : depth_view_;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = pass.clear || pass_target_ != nullptr ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil = { pass.clear_depth, 0 };

        // The screen drawn with more samples: into the multisampled images, kept for later passes, and resolved
        // into the screen's own as the pass ends. Depth takes its first sample; every device can do that.
        pass_samples_ = pass_target_ == nullptr ? samples_ : 1;
        if (pass_samples_ > 1)
        {
            color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            color.resolveImageView = color.imageView;
            color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.imageView = multi_color_view_;
            depth.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
            depth.resolveImageView = depth.imageView;
            depth.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth.imageView = multi_depth_view_;
        }

        VkRenderingInfo rendering { VK_STRUCTURE_TYPE_RENDERING_INFO };
        rendering.renderArea = { { 0, 0 }, area };
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = &depth;
        if (Frame& frame = frames_[frame_index_]; frame.timings != VK_NULL_HANDLE && frame.stamps + 2 <= kMostStamps)
            vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.timings, frame.stamps++);
        vkCmdBeginRendering(cmd, &rendering);
        set_viewport(cmd, pass_target_ != nullptr ? VkRect2D { { 0, 0 }, area } : screen_area());

        ++frame_stats_.passes;
        state_ = State::InPass;
    }

    void VulkanDevice::draw(const DrawCommand& command)
    {
        assert(state_ == State::InPass);
        const auto pipeline = pipelines_.find(command.pipeline.id);
        const auto vertices = buffers_.find(command.vertices.buffer.id);
        assert(pipeline != pipelines_.end() && "draw with an unknown pipeline");
        assert(vertices != buffers_.end() && "draw with an unknown vertex buffer");
        if (pipeline == pipelines_.end() || vertices == buffers_.end())
            return;

        Pipeline& p = pipeline->second;
        if (pass_samples_ > 1 && p.multi_samples != pass_samples_)
        {
            // First drawn on a screen with this many samples: made for it, the old one let go.
            for (const VkPipeline old : { p.multi, p.multi_wire })
                if (old != VK_NULL_HANDLE)
                    garbage_slot().pipelines.push_back(old);
            p.multi = p.multi_wire = VK_NULL_HANDLE;
            PipelineDesc desc = p.desc;
            desc.layout.attributes = p.attributes;
            try
            {
                std::tie(p.multi, p.multi_wire) = build_pipeline(desc, static_cast<VkSampleCountFlagBits>(pass_samples_));
            }
            catch (const std::exception& e)
            {
                static const auto log = logging::logger("vulkan");
                log.error("pipeline with {} samples: {}", pass_samples_, e.what());
                return;
            }
            p.multi_samples = pass_samples_;
        }
        const VkPipeline fill = pass_samples_ > 1 ? p.multi : p.pipeline;
        const VkPipeline wire = pass_samples_ > 1 ? p.multi_wire : p.wire;
        const VkCommandBuffer cmd = frames_[frame_index_].commands;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe_ && wire ? wire : fill);
        const PushBlock block = push_block(command, p.shader);
        vkCmdPushConstants(cmd, pipeline_layout_, kPushStages, 0, sizeof block, &block);
        if (command.texture)
        {
            const auto texture = textures_.find(command.texture.id);
            assert(texture != textures_.end() && "draw with an unknown texture");
            if (texture == textures_.end())
                return;
            const std::size_t sampler = static_cast<std::size_t>(command.filter) * 2 + static_cast<std::size_t>(command.address);
            std::array<VkDescriptorSet, kSetCount> sets { };
            sets.fill(texture->second.set);
            sets[1] = sampler_sets_[sampler];
            // The Water shader reads the extra textures, the Lit ones the shadow; a missing one reads as the first.
            const Shader shader = pipeline->second.shader;
            const bool lit = lit_shader(shader);
            const std::uint32_t count = shader == Shader::Water ? set_count_ : lit ? 8 : shader == Shader::Occlusion || shader == Shader::Shafts ? 5 : 2;
            std::array<TextureHandle, kExtraTextures> extras = command.extra_textures;
            if (lit)
                extras = { command.light.shadow_map, command.light.shadow_view, command.extra_textures[0], command.light.shadow_map_before, command.light.lamps, command.extra_textures[1] };
            else if (shader == Shader::Water)
            {
                extras[4] = command.light.shadow_map;
                extras[5] = command.light.shadow_view;
            }
            for (std::uint32_t i = 2; i < count; ++i)
            {
                const TextureHandle extra = extras[i - 2];
                if (const auto found = textures_.find(extra.id); extra && found != textures_.end())
                    sets[i] = found->second.set;
            }
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, count, sets.data(), 0, nullptr);
        }
        const VkDeviceSize offset = command.vertices.offset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertices->second.buffer, &offset);

        if (command.indices.buffer)
        {
            const auto indices = buffers_.find(command.indices.buffer.id);
            assert(indices != buffers_.end() && "draw with an unknown index buffer");
            if (indices == buffers_.end())
                return;
            vkCmdBindIndexBuffer(cmd, indices->second.buffer, command.indices.offset, command.index_type == IndexType::U16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, command.count, 1, command.first, command.vertex_offset, 0);
        }
        else
        {
            vkCmdDraw(cmd, command.count, 1, command.first, 0);
        }
        ++frame_stats_.draws;
        frame_stats_.primitives += primitive_count(pipeline->second.topology, command.count);
    }

    VkRect2D VulkanDevice::screen_area() const
    {
        if (!view())
            return { { 0, 0 }, extent_ };
        // Kept inside the screen, which may have shrunk since the view was set.
        const std::uint32_t x = std::min(view()->x, extent_.width);
        const std::uint32_t y = std::min(view()->y, extent_.height);
        return { { static_cast<std::int32_t>(x), static_cast<std::int32_t>(y) }, { std::min(view()->size.width, extent_.width - x), std::min(view()->size.height, extent_.height - y) } };
    }

    void VulkanDevice::draw_ui()
    {
        assert(state_ == State::InPass);
        ImGui::Render();
        if (!ui_ready_)
            return;
        const VkCommandBuffer cmd = frames_[frame_index_].commands;
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        set_viewport(cmd, screen_area()); // ImGui leaves its own viewport behind
    }

    void VulkanDevice::end_pass()
    {
        assert(state_ == State::InPass);
        const VkCommandBuffer cmd = frames_[frame_index_].commands;
        vkCmdEndRendering(cmd);
        if (Frame& frame = frames_[frame_index_]; frame.timings != VK_NULL_HANDLE && frame.stamps % 2 == 1)
            vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.timings, frame.stamps++);
        if (pass_target_ != nullptr)
            image_barrier(cmd, pass_target_->image, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT });
        if (pass_target_ != nullptr && pass_target_->depth_id != 0)
            image_barrier(cmd, pass_target_->depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, { VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT });
        pass_target_ = nullptr;
        pass_samples_ = 1;
        state_ = State::InFrame;
    }

    bool VulkanDevice::copy_screen(TextureHandle target)
    {
        assert(state_ == State::InFrame);
        const auto found = textures_.find(target.id);
        if (found == textures_.end() || found->second.depth_id == 0 || found->second.extent.width != extent().width || found->second.extent.height != extent().height)
            return false;
        const Texture& t = found->second;
        const VkCommandBuffer cmd = frames_[frame_index_].commands;
        const VkImage screen = images_[image_index_];
        constexpr VkPipelineStageFlags2 kDepthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        constexpr VkAccessFlags2 kDepthAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        constexpr VkAccessFlags2 kColorAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        // Earlier reads of the target finish before it is written; the screen goes back to drawing afterwards.
        image_barrier(cmd, screen, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT });
        // Depth is written by drawing, or by the resolve of a multisampled depth as colour output.
        image_barrier(cmd, depth_image_, VK_IMAGE_ASPECT_DEPTH_BIT, { VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kDepthStages | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT });
        image_barrier(cmd, t.image, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT });
        image_barrier(cmd, t.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT });
        VkImageCopy region { };
        region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.dstSubresource = region.srcSubresource;
        region.srcOffset = { static_cast<std::int32_t>(screen_area().offset.x), static_cast<std::int32_t>(screen_area().offset.y), 0 };
        region.extent = { extent().width, extent().height, 1 };
        vkCmdCopyImage(cmd, screen, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vkCmdCopyImage(cmd, depth_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.depth_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        image_barrier(cmd, screen, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess });
        image_barrier(cmd, depth_image_, VK_IMAGE_ASPECT_DEPTH_BIT, { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, kDepthStages, kDepthAccess });
        image_barrier(cmd, t.image, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT });
        image_barrier(cmd, t.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT });
        return true;
    }

    bool VulkanDevice::read_screen(TextureHandle from)
    {
        assert(state_ == State::InFrame);
        const bool bgra = color_format_ == VK_FORMAT_B8G8R8A8_UNORM || color_format_ == VK_FORMAT_B8G8R8A8_SRGB;
        const bool rgba = color_format_ == VK_FORMAT_R8G8B8A8_UNORM || color_format_ == VK_FORMAT_R8G8B8A8_SRGB;
        if ((!bgra && !rgba) || reading_.buffer != VK_NULL_HANDLE)
            return false;
        const Texture* target = nullptr;
        if (from)
        {
            const auto found = textures_.find(from.id);
            if (found == textures_.end() || !found->second.target)
                return false;
            target = &found->second;
        }
        const VkExtent2D size = target != nullptr ? target->extent : extent_;
        try
        {
            reading_ = make_buffer(VkDeviceSize { size.width } * size.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        }
        catch (const std::exception& e)
        {
            logging::logger("vulkan").warning("screen read: {}", e.what());
            return false;
        }
        reading_extent_ = size;
        const VkCommandBuffer cmd = frames_[frame_index_].commands;
        VkBufferImageCopy region { };
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { size.width, size.height, 1 };
        if (target != nullptr)
        {
            // A target's pass left it ready for sampling; it goes back to that.
            image_barrier(cmd, target->image, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT });
            vkCmdCopyImageToBuffer(cmd, target->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, reading_.buffer, 1, &region);
            image_barrier(cmd, target->image, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT });
            return true;
        }
        const VkImage screen = images_[image_index_];
        image_barrier(cmd, screen, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT });
        vkCmdCopyImageToBuffer(cmd, screen, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, reading_.buffer, 1, &region);
        image_barrier(cmd, screen, VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT });
        return true;
    }

    std::optional<ScreenPixels> VulkanDevice::take_screen()
    {
        return std::exchange(read_, std::nullopt);
    }

    void VulkanDevice::end_frame()
    {
        assert(state_ == State::InFrame);
        Frame& frame = frames_[frame_index_];
        image_barrier(frame.commands, images_[image_index_], VK_IMAGE_ASPECT_COLOR_BIT, { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0 });
        check(vkEndCommandBuffer(frame.commands), "vkEndCommandBuffer");

        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frame.image_available;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &frame.commands;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished_[image_index_];
        check(vkQueueSubmit(queue_, 1, &submit, frame.in_flight), "vkQueueSubmit");
        if (reading_.buffer != VK_NULL_HANDLE)
        {
            // shortcut: waits for the frame on the spot, a hitch per screenshot; read a frame later when shots
            // come often (photos, video).
            check(vkWaitForFences(device_, 1, &frame.in_flight, VK_TRUE, UINT64_MAX), "vkWaitForFences");
            ScreenPixels pixels { { reading_extent_.width, reading_extent_.height }, std::vector<std::uint8_t>(std::size_t { reading_extent_.width } * reading_extent_.height * 4) };
            void* mapped = nullptr;
            check(vkMapMemory(device_, reading_.memory, 0, pixels.rgba.size(), 0, &mapped), "vkMapMemory");
            std::memcpy(pixels.rgba.data(), mapped, pixels.rgba.size());
            vkUnmapMemory(device_, reading_.memory);
            destroy(reading_);
            reading_ = { };
            const bool bgra = color_format_ == VK_FORMAT_B8G8R8A8_UNORM || color_format_ == VK_FORMAT_B8G8R8A8_SRGB;
            for (std::size_t i = 0; i < pixels.rgba.size(); i += 4)
            {
                if (bgra)
                    std::swap(pixels.rgba[i], pixels.rgba[i + 2]);
                pixels.rgba[i + 3] = 255;
            }
            read_ = std::move(pixels);
        }

        VkPresentInfoKHR present { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_finished_[image_index_];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &image_index_;
        const VkResult result = vkQueuePresentKHR(queue_, &present);

        last_stats_ = frame_stats_;
        frame_index_ = (frame_index_ + 1) % kFramesInFlight;
        state_ = State::Idle;

        // Not every platform reports a resize through the present result, so compare sizes as well.
        int w = 0, h = 0;
        glfwGetFramebufferSize(config_.window, &w, &h);
        const bool resized = static_cast<std::uint32_t>(w) != extent_.width || static_cast<std::uint32_t>(h) != extent_.height;
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || resized)
            recreate_swapchain();
        else
            check(result, "vkQueuePresentKHR");
    }

} // namespace rct3::render::vulkan
