// SPDX-License-Identifier: GPL-3.0-or-later
//
// The OpenGL backend. Needs OpenGL 4.5 core (direct state access, clip control), with the context GLFW made for
// the window. Draws with the same shader programs as Vulkan (app/shaders), which the build turns into OpenGL
// GLSL. Clip control puts the screen's first row where clip y is 1 and depth at 0..1, so targets, screen-space
// reads and the face winding come out as Vulkan's. Like Vulkan: one screen (colour and depth, at the render
// scale) stretched over the window as each frame ends, two frames of per-frame memory, each fenced.
#pragma once

#include "core/types.hpp"
#include "render/device.hpp"
#include "render/opengl/functions.hpp"
#include "render/shader_inputs.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rct3::render::opengl
{

    class OpenGlDevice final : public Device
    {
    public:
        ~OpenGlDevice() override;

        [[nodiscard]] std::expected<void, std::string> init(const DeviceConfig& config) override;
        void shutdown() override;
        [[nodiscard]] Backend backend() const override
        {
            return Backend::OpenGl;
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
        void set_wireframe(bool on) override
        {
            wireframe_ = on;
        }
        void end_frame() override;
        [[nodiscard]] std::vector<u32> sample_levels() const override;
        void set_samples(u32 samples) override;
        [[nodiscard]] FrameStats stats() const override
        {
            return last_stats_;
        }

        void ui_new_frame() override;
        void draw_ui() override;

    private:
        static constexpr u32 kFramesInFlight = 2;
        /// Texture units a draw binds: the texture, an unused one where Vulkan has the sampler, then the extras.
        static constexpr usize kUnits = 2 + kExtraTextures;
        static constexpr usize kShaderCount = static_cast<usize>(Shader::LensDrops) + 1;

        struct Buffer
        {
            GLuint buffer = 0;
            std::byte* mapped = nullptr; ///< Transient blocks only, mapped for their whole life
            usize size = 0;
        };

        struct Texture
        {
            GLuint texture = 0;
            Extent extent;
            TextureFormat format = TextureFormat::Rgba8;
            bool target = false;
            // Targets only: their own depth and the framebuffer that draws into both.
            GLuint depth = 0;
            GLuint framebuffer = 0;
            /// Readable depth only: the id its depth is drawn with under, an entry that owns nothing.
            u32 depth_id = 0;
            bool owned = true;
        };

        struct Pipeline
        {
            Shader shader = Shader::VertexColor;
            PipelineDesc desc; ///< Its label and attributes dropped; the vertex array keeps the attributes
            GLuint vertex_array = 0;
            // The buffers its vertex array draws from, so a draw sets only what differs. Only draws set them,
            // and they stay set; forgotten whenever any buffer goes, as its name may come back.
            GLuint vertices = 0;
            std::uintptr_t vertex_offset = 0;
            GLuint indices = 0;
        };

        struct Frame
        {
            GLsync done = nullptr; ///< Signalled once the GPU has finished the frame that last used the slot
            // Filled front to back each frame; a full block gets a successor, and the next use of the slot
            // merges them into one block big enough for the whole frame (as the Vulkan backend does).
            std::vector<u32> transient;
            usize transient_used = 0; ///< In the last block
            // The window as stream_screen asked for it: a pixel buffer the frame reads into, mapped while it lives.
            GLuint streamed = 0;
            const u8* streamed_memory = nullptr;
            Extent streamed_extent { };
            bool streamed_waiting = false; ///< Read by the frame, not yet taken
        };

        enum class State : u8
        {
            Off,
            Idle,
            InFrame,
            InPass,
        };

        // Setup (device.cpp)
        void create_screen();
        void destroy_screen();
        void create_multisample();
        void destroy_multisample();
        void reset_transient(Frame& frame);
        void apply(const Pipeline& pipeline, bool wire);
        /// Where passes into the screen draw: the view, kept inside the screen, or all of it.
        [[nodiscard]] ScreenView screen_area() const;

        // Resources (resources.cpp)
        [[nodiscard]] std::expected<Texture, std::string> make_texture(const TextureDesc& desc);
        void destroy(const Texture& texture);
        [[nodiscard]] u32 make_transient_block(usize size);
        [[nodiscard]] std::expected<GLuint, std::string> compile_program(const char* vertex, const char* fragment, i32 mode);
        [[nodiscard]] std::expected<GLuint, std::string> program(Shader shader);

        Functions gl;
        DeviceConfig config_;
        State state_ = State::Off;
        std::string device_name_;
        Extent window_;        ///< The window's framebuffer, in pixels
        f64 frame_pace_ = 0;   ///< Seconds between presents when the device paces frames itself, else 0
        f64 last_present_ = 0; ///< When the last paced frame was presented, in GLFW's time

        // The screen the passes draw into: a target the window's size times the render scale, not in textures_.
        Texture screen_;
        // With more than one sample, the screen's passes draw into these and are resolved into screen_ as each
        // ends, so later passes and copy_screen find the screen as without.
        u32 samples_ = 1;
        GLuint multi_framebuffer_ = 0;
        GLuint multi_color_ = 0;
        GLuint multi_depth_ = 0;
        bool pass_multi_ = false;     ///< The open pass draws into the multisampled screen
        bool pass_to_screen_ = false; ///< The open pass draws into the screen

        GLuint present_program_ = 0;
        GLuint empty_vertex_array_ = 0; ///< The core profile draws only with one bound
        GLuint constants_ = 0;          ///< Uniform block 0: the draw's ShaderConstants
        std::array<GLuint, kShaderCount> programs_ { };
        static constexpr usize kSamplerCount = 4; // Filter x AddressMode, at filter * 2 + address
        std::array<GLuint, kSamplerCount> samplers_ { };
        std::optional<const Pipeline&> applied_; ///< The pipeline whose state is set, none after anything else changed it
        bool applied_wire_ = false;
        /// What the open pass's draws last bound, so a draw binds only what differs: the park's draws come in runs
        /// sharing a picture and constants. Forgotten as each pass begins and as a texture goes.
        struct Bound
        {
            std::array<GLuint, kUnits> textures { };
            GLuint sampler = 0;
            ShaderConstants constants { };
            bool has_textures = false;
            bool has_constants = false;
        };
        Bound bound_;

        bool ui_ready_ = false;
        bool ui_drawn_ = false;
        bool wireframe_ = false;
        bool reading_window_ = false;         ///< read_screen asked for the window, read as the frame ends
        bool streaming_window_ = false;       ///< stream_screen asked for the window this frame
        std::optional<ScreenPixels> reading_; ///< Read this frame, handed over as it ends
        std::optional<ScreenPixels> read_;    ///< Until taken

        std::array<Frame, kFramesInFlight> frames_ { };
        u32 frame_index_ = 0;

        u32 next_id_ = 1;
        std::unordered_map<u32, Buffer> buffers_;
        std::unordered_map<u32, Pipeline> pipelines_;
        std::unordered_map<u32, Texture> textures_;

        FrameStats frame_stats_;
        FrameStats last_stats_;
    };

} // namespace rct3::render::opengl
