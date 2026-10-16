// SPDX-License-Identifier: GPL-3.0-or-later
//
// A device that draws nothing. It keeps the bookkeeping of a real one: handles are issued and checked, calls
// out of frame order are caught, and the counters are filled in. Useful without a GPU, and as the smallest
// example of what a backend has to implement.
#pragma once

#include "render/device.hpp"

#include <unordered_map>

namespace rct3::render::null
{

    class NullDevice final : public Device
    {
    public:
        ~NullDevice() override;

        [[nodiscard]] std::expected<void, std::string> init(const DeviceConfig& config) override;
        void shutdown() override;
        [[nodiscard]] Backend backend() const override
        {
            return Backend::Null;
        }
        [[nodiscard]] std::string_view device_name() const override
        {
            return "null device";
        }
        [[nodiscard]] Extent extent() const override;

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
        void end_frame() override;
        [[nodiscard]] FrameStats stats() const override
        {
            return last_stats_;
        }

        void ui_new_frame() override;
        void draw_ui() override;

    private:
        struct Buffer
        {
            BufferUsage usage;
            std::size_t size;
        };

        enum class State : std::uint8_t
        {
            Off,
            Idle,
            InFrame,
            InPass,
        };

        DeviceConfig config_;
        State state_ = State::Off;
        bool ui_ready_ = false;
        std::uint32_t next_id_ = 1;
        std::unordered_map<std::uint32_t, Buffer> buffers_;
        std::unordered_map<std::uint32_t, Topology> pipelines_;
        std::unordered_map<std::uint32_t, Extent> textures_;
        std::unordered_map<std::uint32_t, std::uint32_t> depths_; ///< Target id to the id of its readable depth
        // One id stands for all transient memory; its size is the high-water mark of offsets handed out.
        std::uint32_t transient_id_ = 0;
        std::uint64_t transient_used_ = 0;
        FrameStats frame_stats_;
        FrameStats last_stats_;
    };

} // namespace rct3::render::null
