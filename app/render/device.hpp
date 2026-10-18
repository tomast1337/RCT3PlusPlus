// SPDX-License-Identifier: GPL-3.0-or-later
//
// The render device interface. Every backend (Vulkan today; OpenGL, a software rasteriser or anything else
// later) implements it, and the rest of the program only talks to this interface, so backends can be swapped
// at startup without touching game code.
//
// Lifecycle:
//
//   auto device = make_device(backend);        // render/backend.hpp; nothing is created yet
//   device->init(config);                      // instance, device, swapchain, UI renderer
//   ... create_buffer / create_texture / create_pipeline ...
//   per frame:
//     device->ui_new_frame();                  // then the platform's ImGui new frame, ImGui::NewFrame, UI code
//     device->begin_frame();                   // waits for a free frame slot, acquires the target
//     device->allocate_transient(bytes);       // per-frame vertex/index data, any time inside the frame
//     device->update_texture(texture, pixels); // between passes
//     device->begin_pass({ ... });             // one or more passes
//       device->draw({ ... });
//       device->draw_ui();                     // in the last pass, so the UI is on top
//     device->end_pass();
//     device->copy_screen(target);             // between passes
//     device->read_screen();                   // between passes, for a screenshot
//     device->end_frame();                     // submits and presents
//   device->take_screen();                     // what read_screen asked for
//   device->shutdown();                        // also done by the destructor
//
// Calls out of this order are programming errors. Resources may be destroyed at any time, also while a frame
// that uses them is in flight: the backend defers the release until the GPU is done with them.
#pragma once

#include "render/types.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace rct3::render
{

    enum class Backend : std::uint8_t
    {
        Vulkan,
        Null, ///< Draws nothing; for running without a GPU and for checking callers
    };

    struct DeviceConfig
    {
        /// The window to present to. Created by the platform layer with the backend's window hints.
        GLFWwindow* window = nullptr;
        /// Synchronise presentation to the display refresh.
        bool vsync = true;
        /// Enable the API's validation layer when available.
        bool validation = false;
        /// Set up the Dear ImGui renderer. The ImGui context must exist before init and outlive shutdown.
        bool ui = true;
    };

    /// A part of the screen, in pixels from its top-left corner.
    struct ScreenView
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        Extent size;
    };

    class Device
    {
    public:
        Device() = default;
        virtual ~Device() = default;

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        // Lifecycle -----------------------------------------------------------------------------------------

        /// Creates everything the device needs. An error means no usable device: unsupported API version,
        /// no GPU, driver failure. On error the device is left shut down and `init` may be retried.
        [[nodiscard]] virtual std::expected<void, std::string> init(const DeviceConfig& config) = 0;

        /// Waits for the GPU and releases everything, including resources the caller did not destroy.
        /// Safe to call when not initialised.
        virtual void shutdown() = 0;

        [[nodiscard]] virtual Backend backend() const = 0;

        /// Human-readable adapter name, for logs.
        [[nodiscard]] virtual std::string_view device_name() const = 0;

        /// Size of the screen in pixels. Follows the window; may change between frames.
        [[nodiscard]] virtual Extent screen_extent() const = 0;

        /// Size the scene draws at: the view's when one is set, else the screen's.
        [[nodiscard]] Extent extent() const
        {
            return view_ ? view_->size : screen_extent();
        }

        /// Passes into the screen draw into `view` only, which then acts as the whole screen for drawing and
        /// copy_screen; the interface (draw_ui) still covers the whole screen. None for the whole screen.
        void set_view(std::optional<ScreenView> view)
        {
            view_ = view;
        }

        [[nodiscard]] const std::optional<ScreenView>& view() const
        {
            return view_;
        }

        // Resources -----------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::expected<BufferHandle, std::string> create_buffer(const BufferDesc& desc) = 0;
        virtual void destroy_buffer(BufferHandle buffer) = 0;

        [[nodiscard]] virtual std::expected<TextureHandle, std::string> create_texture(const TextureDesc& desc) = 0;
        virtual void destroy_texture(TextureHandle texture) = 0;
        /// Replaces every pixel of `texture` (not a target) with `pixels`, laid out as for create_texture. Only
        /// between passes of a frame; draws of the frame after the call see the new pixels.
        virtual void update_texture(TextureHandle texture, std::span<const std::byte> pixels) = 0;
        /// The depth of a target made with `readable_depth`, as a texture to draw with; none for other textures.
        /// It goes with the target.
        [[nodiscard]] virtual TextureHandle depth_texture(TextureHandle target) const = 0;

        [[nodiscard]] virtual std::expected<PipelineHandle, std::string> create_pipeline(const PipelineDesc& desc) = 0;
        virtual void destroy_pipeline(PipelineHandle pipeline) = 0;

        /// Copies `data` into memory that lives until the end of the current frame and returns where it went.
        /// Usable as vertex or index data by draws of this frame. Only between begin_frame and end_frame.
        /// Slices start at multiples of 16 bytes.
        [[nodiscard]] virtual std::expected<BufferSlice, std::string> allocate_transient(std::span<const std::byte> data) = 0;

        // Frame ---------------------------------------------------------------------------------------------

        virtual void begin_frame() = 0;
        virtual void begin_pass(const PassDesc& pass) = 0;
        virtual void draw(const DrawCommand& command) = 0;
        virtual void end_pass() = 0;
        /// Copies what the screen holds so far, colour and depth, into `target`: a target of the screen's size made
        /// with `readable_depth`. Only between passes of a frame. False when the device cannot, or `target` does
        /// not fit; it is then left as it was.
        virtual bool copy_screen(TextureHandle target) = 0;
        /// Asks for what the whole screen holds so far, the interface too, whatever the view; or, given `from`,
        /// what that target holds. Only between passes of a frame. The pixels arrive as the frame ends: take
        /// them with take_screen. False when the device cannot, or `from` is not a target.
        virtual bool read_screen(TextureHandle from = { }) = 0;
        virtual void end_frame() = 0;
        /// The pixels read_screen asked for, once their frame ended; then none until asked again.
        [[nodiscard]] virtual std::optional<ScreenPixels> take_screen() = 0;
        /// For debugging: while on, draws of triangles outline them instead of filling them, where the device can.
        virtual void set_wireframe(bool on) = 0;

        /// The samples per pixel above 1 the screen can be drawn with to smooth edges, fewest first; none when
        /// the device cannot.
        [[nodiscard]] virtual std::vector<std::uint32_t> sample_levels() const = 0;
        /// Draws the screen's passes with `samples` per pixel from the next frame: 1 for none, else one of
        /// sample_levels. Passes into targets keep one. Only between frames.
        virtual void set_samples(std::uint32_t samples) = 0;

        /// Counters of the last finished frame.
        [[nodiscard]] virtual FrameStats stats() const = 0;

        // UI ------------------------------------------------------------------------------------------------

        /// The renderer half of starting an ImGui frame. Call before the platform's new frame and
        /// ImGui::NewFrame.
        virtual void ui_new_frame() = 0;

        /// Finishes the ImGui frame and draws it into the current pass.
        virtual void draw_ui() = 0;

    private:
        std::optional<ScreenView> view_;
    };

} // namespace rct3::render
