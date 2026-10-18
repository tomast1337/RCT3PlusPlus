// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Vulkan backend. Needs Vulkan 1.3 (dynamic rendering, synchronization2). Two frames in flight, one
// depth buffer, swapchain recreated when the window changes size. Buffers live in host-visible memory for
// now; a staging path and an allocator come with bigger meshes. Textures are uploaded through a staging
// buffer and the call waits for the copy, which suits loading but not streaming.
#pragma once

#include "render/device.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

struct ImGui_ImplVulkan_PipelineInfo;

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
        [[nodiscard]] Extent screen_extent() const override
        {
            return { extent_.width, extent_.height };
        }

        [[nodiscard]] std::expected<BufferHandle, std::string> create_buffer(const BufferDesc& desc) override;
        void destroy_buffer(BufferHandle buffer) override;
        [[nodiscard]] std::expected<TextureHandle, std::string> create_texture(const TextureDesc& desc) override;
        void destroy_texture(TextureHandle texture) override;
        void update_texture(TextureHandle texture, std::span<const std::byte> pixels) override;
        [[nodiscard]] TextureHandle depth_texture(TextureHandle target) const override;
        [[nodiscard]] std::expected<PipelineHandle, std::string> create_pipeline(const PipelineDesc& desc) override;
        void destroy_pipeline(PipelineHandle pipeline) override;
        [[nodiscard]] std::expected<BufferSlice, std::string> allocate_transient(std::span<const std::byte> data) override;

        void begin_frame() override;
        void begin_pass(const PassDesc& pass) override;
        void draw(const DrawCommand& command) override;
        void end_pass() override;
        bool copy_screen(TextureHandle target) override;
        bool read_screen(TextureHandle from) override;
        [[nodiscard]] std::optional<ScreenPixels> take_screen() override;
        void set_wireframe(bool on) override
        {
            wireframe_ = on;
        }
        void end_frame() override;
        [[nodiscard]] std::vector<std::uint32_t> sample_levels() const override;
        void set_samples(std::uint32_t samples) override;
        [[nodiscard]] FrameStats stats() const override
        {
            return last_stats_;
        }

        void ui_new_frame() override;
        void draw_ui() override;

    private:
        /// The part of the screen passes into it draw to: the view, kept inside the screen, or all of it.
        [[nodiscard]] VkRect2D screen_area() const;
        static constexpr std::uint32_t kFramesInFlight = 2;

        struct Buffer
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
        };

        struct Texture
        {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkDescriptorSet set = VK_NULL_HANDLE;
            VkExtent2D extent { };
            TextureFormat format = TextureFormat::Rgba8;
            bool target = false;
            // Targets only: their own depth buffer.
            VkImage depth_image = VK_NULL_HANDLE;
            VkDeviceMemory depth_memory = VK_NULL_HANDLE;
            VkImageView depth_view = VK_NULL_HANDLE;
            // Readable depth only: the id under which the depth is drawn with, an entry that owns nothing but
            // its own view and set.
            std::uint32_t depth_id = 0;
        };

        // One block of per-frame memory, mapped for its whole life and registered in buffers_ so draws find it.
        struct TransientBlock
        {
            std::uint32_t id = 0;
            std::byte* mapped = nullptr;
            VkDeviceSize size = 0;
        };

        struct Pipeline
        {
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipeline wire = VK_NULL_HANDLE; ///< The same, outlining triangles; none without line fill
            Topology topology = Topology::Triangles;
            Shader shader = Shader::VertexColor;
            /// How it was asked for, its label dropped and its attributes kept in `attributes`, to make it again
            /// for the screen drawn with more samples.
            PipelineDesc desc;
            std::vector<VertexAttribute> attributes;
            VkPipeline multi = VK_NULL_HANDLE; ///< For the screen drawn with `multi_samples`; made when first drawn
            VkPipeline multi_wire = VK_NULL_HANDLE;
            std::uint32_t multi_samples = 0;
        };

        // Objects released once the frame slot that last used them has finished on the GPU.
        struct Garbage
        {
            std::vector<Buffer> buffers;
            std::vector<Texture> textures;
            std::vector<VkPipeline> pipelines;
        };

        struct Frame
        {
            VkCommandPool pool = VK_NULL_HANDLE;
            VkCommandBuffer commands = VK_NULL_HANDLE;
            VkSemaphore image_available = VK_NULL_HANDLE;
            VkFence in_flight = VK_NULL_HANDLE;
            Garbage garbage;
            // Filled front to back each frame; a full block gets a successor, and the next use of the slot
            // merges them into one block big enough for the whole frame.
            std::vector<TransientBlock> transient;
            VkDeviceSize transient_used = 0; ///< In the last block
            /// When each pass began and ended on the GPU, read once the frame is done; none where the queue
            /// keeps no time.
            VkQueryPool timings = VK_NULL_HANDLE;
            std::uint32_t stamps = 0; ///< Written this frame, two a pass
        };
        static constexpr std::uint32_t kMostStamps = 64;
        float stamp_ms_ = 0; ///< Milliseconds a timestamp tick lasts

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
        void create_samplers();
        void init_ui();

        void destroy_swapchain();
        void recreate_swapchain();
        void release(Garbage& garbage);
        [[nodiscard]] Garbage& garbage_slot();
        void reset_transient(Frame& frame);

        // Resources (vulkan_resources.cpp)
        [[nodiscard]] std::uint32_t find_memory_type(std::uint32_t type_bits, VkMemoryPropertyFlags properties) const;
        [[nodiscard]] Buffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void destroy(const Buffer& buffer);
        void destroy(const Texture& texture);
        [[nodiscard]] TransientBlock make_transient_block(VkDeviceSize size);
        void submit_once(const std::function<void(VkCommandBuffer)>& record);
        void upload(const Buffer& staging, const Texture& texture, std::uint32_t width, std::uint32_t height);
        void make_depth_readable(Texture& texture, std::uint32_t id);
        /// `desc` as a pipeline drawing with `samples` per pixel, and the same outlining triangles where it can.
        [[nodiscard]] std::pair<VkPipeline, VkPipeline> build_pipeline(const PipelineDesc& desc, VkSampleCountFlagBits samples);
        void create_multisample();
        [[nodiscard]] ImGui_ImplVulkan_PipelineInfo ui_pipeline_info() const;
        void destroy_multisample();

        DeviceConfig config_;
        State state_ = State::Off;
        const Texture* pass_target_ = nullptr; // The target the open pass draws into; null for the screen

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

        // What read_screen asked for: the screen copied into reading_ by the frame, read as the frame ends.
        Buffer reading_;
        VkExtent2D reading_extent_ { };
        std::optional<ScreenPixels> read_; ///< Until taken

        static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
        VkImage depth_image_ = VK_NULL_HANDLE;
        VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
        VkImageView depth_view_ = VK_NULL_HANDLE;

        // With more than one sample, the screen's passes draw into these and resolve into the swapchain image and
        // depth_view_ as each ends, so later passes and copy_screen find the screen as without.
        std::uint32_t samples_ = 1;
        std::uint32_t pass_samples_ = 1; ///< Of the open pass
        VkImage multi_color_image_ = VK_NULL_HANDLE;
        VkDeviceMemory multi_color_memory_ = VK_NULL_HANDLE;
        VkImageView multi_color_view_ = VK_NULL_HANDLE;
        VkImage multi_depth_image_ = VK_NULL_HANDLE;
        VkDeviceMemory multi_depth_memory_ = VK_NULL_HANDLE;
        VkImageView multi_depth_view_ = VK_NULL_HANDLE;

        // Set 0 is the texture, set 1 the sampler (see app/shaders/textured.frag), sets 2 on the extra textures
        // when the device can bind that many sets.
        static constexpr std::uint32_t kSetCount = 2 + kExtraTextures;
        std::uint32_t set_count_ = 4;
        VkDescriptorSetLayout texture_set_layout_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout sampler_set_layout_ = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
        VkCommandPool upload_pool_ = VK_NULL_HANDLE;
        static constexpr std::size_t kSamplerCount = 4; // Filter x AddressMode
        std::array<VkSampler, kSamplerCount> samplers_ { };
        std::array<VkDescriptorSet, kSamplerCount> sampler_sets_ { };

        std::array<Frame, kFramesInFlight> frames_ { };
        std::uint32_t frame_index_ = 0;
        std::uint32_t image_index_ = 0;
        bool ui_ready_ = false;
        bool line_fill_ = false; ///< The GPU can outline triangles
        bool wireframe_ = false;

        std::uint32_t next_id_ = 1;
        std::unordered_map<std::uint32_t, Buffer> buffers_;
        std::unordered_map<std::uint32_t, Pipeline> pipelines_;
        std::unordered_map<std::uint32_t, Texture> textures_;

        FrameStats frame_stats_;
        FrameStats last_stats_;
    };

} // namespace rct3::render::vulkan
