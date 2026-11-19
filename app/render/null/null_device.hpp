// SPDX-License-Identifier: GPL-3.0-or-later
//
// A device that draws nothing. It keeps the bookkeeping of a real one: handles are issued and checked, calls
// out of frame order are caught, and the counters are filled in. Useful without a GPU, and as the smallest
// example of what a backend has to implement.
#pragma once

#include "core/types.hpp"
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
        [[nodiscard]] Extent screen_extent() const override;

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
        void set_wireframe(bool /*on*/) override
        {
        }
        [[nodiscard]] std::vector<u32> sample_levels() const override
        {
            return { };
        }
        void set_samples(u32 /*samples*/) override
        {
        }
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
            usize size;
        };

        enum class State : u8
        {
            Off,
            Idle,
            InFrame,
            InPass,
        };

        DeviceConfig config_;
        State state_ = State::Off;
        bool ui_ready_ = false;
        u32 next_id_ = 1;
        std::unordered_map<u32, Buffer> buffers_;
        std::unordered_map<u32, Topology> pipelines_;
        std::unordered_map<u32, Extent> textures_;
        std::unordered_map<u32, u32> depths_; ///< Target id to the id of its readable depth
        // One id stands for all transient memory; its size is the high-water mark of offsets handed out.
        u32 transient_id_ = 0;
        u64 transient_used_ = 0;
        FrameStats frame_stats_;
        FrameStats last_stats_;
        std::optional<Extent> reading_;    ///< The screen read_screen asked for, until its frame ends
        std::optional<ScreenPixels> read_; ///< Its pixels, until taken
    };

} // namespace rct3::render::null
