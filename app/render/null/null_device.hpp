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
        FrameStats frame_stats_;
        FrameStats last_stats_;
    };

} // namespace rct3::render::null
