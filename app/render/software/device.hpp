// SPDX-License-Identifier: GPL-3.0-or-later
//
// The software backend: draws on the CPU, with no graphics driver. The screen (at the render scale) is stretched
// into a picture the window's size, the developer windows are drawn over it, and the result is what a screenshot
// reads. With a window it is shown through the window's OpenGL context, any version, as one picture a frame;
// without a display (--headless) nothing is shown and nothing but the CPU is used. The shader programs are C++
// versions of app/shaders (render/software/shaders.hpp), some with less detail.
#pragma once

#include "core/types.hpp"
#include "render/device.hpp"
#include "render/software/raster.hpp"

#include <GL/glcorearb.h>
#include <memory>
#include <unordered_map>

// <windows.h>, which the OpenGL header brings in on Windows, renames DrawState to a function of its own.
#undef DrawState

namespace rct3::render::software
{

    class SoftwareDevice final : public Device
    {
    public:
        ~SoftwareDevice() override;

        [[nodiscard]] std::expected<void, std::string> init(const DeviceConfig& config) override;
        void shutdown() override;
        [[nodiscard]] Backend backend() const override
        {
            return Backend::Software;
        }
        [[nodiscard]] std::string_view device_name() const override
        {
            return device_name_;
        }
        [[nodiscard]] Extent screen_extent() const override
        {
            return screen_.extent;
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
        bool stream_screen() override;
        [[nodiscard]] bool take_streamed(ScreenPixels& into, StreamWait wait) override;
        void set_wireframe(bool /*on*/) override
        {
        }
        void end_frame() override;
        [[nodiscard]] std::vector<u32> sample_levels() const override
        {
            return { };
        }
        void set_samples(u32 /*samples*/) override
        {
        }
        [[nodiscard]] FrameStats stats() const override
        {
            return last_stats_;
        }

        void ui_new_frame() override
        {
        }
        void draw_ui() override;

    private:
        struct Pipeline
        {
            Program program;
            PipelineDesc desc; ///< Its label dropped and its attributes pointing into `attributes`
            std::vector<VertexAttribute> attributes;
        };

        enum class State : u8
        {
            Off,
            Idle,
            InFrame,
            InPass,
        };

        /// The OpenGL calls that show a frame in the window: OpenGL 1.1's, in every context.
        struct Present
        {
            void(APIENTRYP viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
            void(APIENTRYP raster_pos)(GLfloat, GLfloat) = nullptr;
            void(APIENTRYP pixel_zoom)(GLfloat, GLfloat) = nullptr;
            void(APIENTRYP draw_pixels)(GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
        };

        void resize();
        /// Where passes into the screen draw: the view, kept inside the screen, or all of it.
        [[nodiscard]] ScreenView screen_area() const;
        void flush();
        /// The texture a draw reads, marked as read.
        [[nodiscard]] std::optional<const Texture&> find_texture(TextureHandle handle);
        void compose_window();
        void draw_imgui();

        DeviceConfig config_;
        State state_ = State::Off;
        std::string device_name_;
        Present present_;
        bool shown_ = false; ///< The window has an OpenGL context to show frames with

        Texture screen_;
        Texture window_; ///< The stretched screen and the developer windows, the window's size

        std::unique_ptr<WorkerPool> pool_;
        Batch batch_;
        std::optional<Texture&> pass_target_;
        ScreenView pass_area_;

        u32 transient_id_ = 0; ///< Buffer id of the per-frame memory, which grows to fit a frame
        u32 next_id_ = 1;
        std::unordered_map<u32, std::vector<std::byte>> buffers_;
        std::unordered_map<u32, Pipeline> pipelines_;
        std::unordered_map<u32, Texture> textures_;

        bool ui_ready_ = false;
        bool ui_drawn_ = false;
        bool reading_window_ = false;
        bool streaming_window_ = false; ///< stream_screen asked for the window this frame
        bool streamed_waiting_ = false; ///< The window holds a streamed frame, not yet taken
        std::optional<ScreenPixels> reading_;
        std::optional<ScreenPixels> read_;

        FrameStats frame_stats_;
        FrameStats last_stats_;
    };

} // namespace rct3::render::software
