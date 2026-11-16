// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/software/device.hpp"

#include "imgui.h"
#include "render/software/shaders.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <format>
#include <thread>
#include <utility>

namespace rct3::render::software
{

    namespace
    {

        /// Triangles a batch takes before it is drawn, within a pass; bounds the batch's memory (about 20 MB).
        constexpr std::size_t kBatchTriangles = 65536;
        /// The most threads that draw, the caller's among them: tiles are small, so more add little.
        constexpr std::uint32_t kMostThreads = 8;
        constexpr std::uint32_t kLargestSide = 16384;

        Texture make_target(Extent extent)
        {
            const std::size_t pixels = std::size_t { extent.width } * extent.height;
            return { .extent = extent, .texels = std::vector<std::uint32_t>(pixels, 0u), .depth = std::vector<float>(pixels, 1.0f), .target = true };
        }

        void copy_pixels(Texture& t, std::span<const std::byte> pixels)
        {
            if (t.format == TextureFormat::Rgba32Float)
            {
                t.floats.resize(std::size_t { t.extent.width } * t.extent.height);
                std::memcpy(t.floats.data(), pixels.data(), std::min(pixels.size(), t.floats.size() * sizeof(glm::vec4)));
                return;
            }
            t.texels.resize(std::size_t { t.extent.width } * t.extent.height);
            // Rgba8 bytes in memory order are the texel with red lowest, on the little-endian machines we build for.
            std::memcpy(t.texels.data(), pixels.data(), std::min(pixels.size(), t.texels.size() * sizeof(std::uint32_t)));
        }

    } // namespace

    SoftwareDevice::~SoftwareDevice()
    {
        shutdown();
    }

    std::expected<void, std::string> SoftwareDevice::init(const DeviceConfig& config)
    {
        assert(state_ == State::Off);
        if (config.window == nullptr)
            return std::unexpected("no window");
        config_ = config;
        // Without a display there is nothing to show; the frames are still drawn for screenshots.
        shown_ = glfwGetPlatform() != GLFW_PLATFORM_NULL;
        if (shown_)
        {
            glfwMakeContextCurrent(config_.window);
            if (glfwGetCurrentContext() != config_.window)
                return std::unexpected("the window has no OpenGL context to show frames with");
            present_ = {
                .viewport = reinterpret_cast<decltype(present_.viewport)>(glfwGetProcAddress("glViewport")),
                .raster_pos = reinterpret_cast<decltype(present_.raster_pos)>(glfwGetProcAddress("glRasterPos2f")),
                .pixel_zoom = reinterpret_cast<decltype(present_.pixel_zoom)>(glfwGetProcAddress("glPixelZoom")),
                .draw_pixels = reinterpret_cast<decltype(present_.draw_pixels)>(glfwGetProcAddress("glDrawPixels")),
            };
            if (present_.viewport == nullptr || present_.raster_pos == nullptr || present_.pixel_zoom == nullptr || present_.draw_pixels == nullptr)
                return std::unexpected("the window's OpenGL context cannot draw pixels");
            glfwSwapInterval(config_.vsync ? 1 : 0);
        }
        const std::uint32_t threads = std::clamp(std::thread::hardware_concurrency() / 2, 1u, kMostThreads);
        pool_ = std::make_unique<WorkerPool>(threads - 1);
        device_name_ = std::format("CPU, {} threads", threads);
        transient_id_ = next_id_++;
        buffers_.emplace(transient_id_, std::vector<std::byte> { });
        resize();
        if (config_.ui)
        {
            // ImGui hands its font atlas and other pictures over as it makes and changes them (draw_imgui).
            ImGuiIO& io = ImGui::GetIO();
            io.BackendRendererName = "rct3_software";
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
            ui_ready_ = true;
        }
        state_ = State::Idle;
        return { };
    }

    void SoftwareDevice::shutdown()
    {
        if (state_ == State::Off)
            return;
        if (ui_ready_)
        {
            for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
            {
                if (tex->RefCount == 1)
                {
                    tex->SetTexID(ImTextureID_Invalid);
                    tex->SetStatus(ImTextureStatus_Destroyed);
                }
            }
            ImGuiIO& io = ImGui::GetIO();
            io.BackendRendererName = nullptr;
            io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset);
            ui_ready_ = false;
        }
        batch_.clear();
        pool_.reset();
        buffers_.clear();
        pipelines_.clear();
        textures_.clear();
        screen_ = { };
        window_ = { };
        reading_.reset();
        read_.reset();
        state_ = State::Off;
    }

    // The window's size, and the screen's at the render scale, as they are now.
    void SoftwareDevice::resize()
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(config_.window, &w, &h);
        const Extent window { static_cast<std::uint32_t>(std::max(w, 1)), static_cast<std::uint32_t>(std::max(h, 1)) };
        if (window.width != window_.extent.width || window.height != window_.extent.height)
            window_ = { .extent = window, .texels = std::vector<std::uint32_t>(std::size_t { window.width } * window.height, 0u) };
        const auto scaled = [&](std::uint32_t side) { return std::clamp(static_cast<std::uint32_t>(std::lround(static_cast<float>(side) * render_scale_)), 1u, kLargestSide); };
        if (const Extent screen { scaled(window.width), scaled(window.height) }; screen.width != screen_.extent.width || screen.height != screen_.extent.height)
            screen_ = make_target(screen);
    }

    std::expected<BufferHandle, std::string> SoftwareDevice::create_buffer(const BufferDesc& desc)
    {
        assert(state_ != State::Off);
        if (desc.data.empty())
            return std::unexpected("empty buffer");
        const std::uint32_t id = next_id_++;
        buffers_.emplace(id, std::vector<std::byte>(desc.data.begin(), desc.data.end()));
        return BufferHandle { id };
    }

    void SoftwareDevice::destroy_buffer(BufferHandle buffer)
    {
        // A draw's vertices are read as it is called, so nothing waiting to be drawn needs the buffer.
        if (buffer.id != transient_id_)
            buffers_.erase(buffer.id);
    }

    std::expected<TextureHandle, std::string> SoftwareDevice::create_texture(const TextureDesc& desc)
    {
        assert(state_ != State::Off);
        if (desc.width == 0 || desc.height == 0 || desc.width > kLargestSide || desc.height > kLargestSide)
            return std::unexpected(std::format("texture size {}x{} out of range", desc.width, desc.height));
        const std::uint32_t id = next_id_++;
        if (desc.target)
        {
            Texture& made = textures_.emplace(id, make_target({ desc.width, desc.height })).first->second;
            if (desc.readable_depth)
            {
                made.depth_id = next_id_++;
                textures_.emplace(made.depth_id, Texture { .extent = made.extent, .depth_of = made });
            }
            return TextureHandle { id };
        }
        if (desc.pixels.size() != texture_bytes(desc.width, desc.height, desc.format))
            return std::unexpected(std::format("texture of {}x{} has {} bytes of pixels", desc.width, desc.height, desc.pixels.size()));
        Texture t { .extent = { desc.width, desc.height }, .format = desc.format };
        copy_pixels(t, desc.pixels);
        textures_.emplace(id, std::move(t));
        return TextureHandle { id };
    }

    void SoftwareDevice::destroy_texture(TextureHandle texture)
    {
        const auto found = textures_.find(texture.id);
        if (found == textures_.end())
            return;
        // Triangles waiting to be drawn may read it.
        flush();
        if (found->second.depth_id != 0)
            textures_.erase(found->second.depth_id);
        textures_.erase(found);
    }

    void SoftwareDevice::update_texture(TextureHandle texture, std::span<const std::byte> pixels)
    {
        assert(state_ == State::InFrame);
        const auto found = textures_.find(texture.id);
        assert(found != textures_.end() && !found->second.target && "update of an unknown texture or a target");
        if (found == textures_.end() || found->second.target || found->second.depth_of)
            return;
        Texture& t = found->second;
        assert(pixels.size() == texture_bytes(t.extent.width, t.extent.height, t.format));
        copy_pixels(t, pixels);
    }

    TextureHandle SoftwareDevice::depth_texture(TextureHandle target) const
    {
        const auto found = textures_.find(target.id);
        return found != textures_.end() ? TextureHandle { found->second.depth_id } : TextureHandle { };
    }

    std::expected<PipelineHandle, std::string> SoftwareDevice::create_pipeline(const PipelineDesc& desc)
    {
        assert(state_ != State::Off);
        if (desc.layout.attributes.size() > kMaxVertexAttributes)
            return std::unexpected("too many vertex attributes");
        const std::uint32_t id = next_id_++;
        Pipeline& p = pipelines_.emplace(id, Pipeline { .program = program(desc.shader), .desc = desc, .attributes = { desc.layout.attributes.begin(), desc.layout.attributes.end() } }).first->second;
        p.desc.label = { };
        p.desc.layout.attributes = p.attributes;
        return PipelineHandle { id };
    }

    void SoftwareDevice::destroy_pipeline(PipelineHandle pipeline)
    {
        flush();
        pipelines_.erase(pipeline.id);
    }

    std::expected<BufferSlice, std::string> SoftwareDevice::allocate_transient(std::span<const std::byte> data)
    {
        assert(state_ == State::InFrame || state_ == State::InPass);
        std::vector<std::byte>& memory = buffers_.at(transient_id_);
        const std::size_t offset = (memory.size() + 15) & ~std::size_t { 15 };
        memory.resize(offset + data.size());
        std::memcpy(memory.data() + offset, data.data(), data.size());
        frame_stats_.transient_bytes += data.size();
        return BufferSlice { BufferHandle { transient_id_ }, static_cast<std::uint32_t>(offset) };
    }

    void SoftwareDevice::begin_frame()
    {
        assert(state_ == State::Idle);
        resize();
        buffers_.at(transient_id_).clear();
        frame_stats_ = { };
        ui_drawn_ = false;
        state_ = State::InFrame;
    }

    ScreenView SoftwareDevice::screen_area() const
    {
        const Extent screen = screen_.extent;
        if (!view())
            return { 0, 0, screen };
        const std::uint32_t x = std::min(view()->x, screen.width);
        const std::uint32_t y = std::min(view()->y, screen.height);
        return { x, y, { std::min(view()->size.width, screen.width - x), std::min(view()->size.height, screen.height - y) } };
    }

    void SoftwareDevice::begin_pass(const PassDesc& pass)
    {
        assert(state_ == State::InFrame);
        pass_target_ = screen_;
        pass_area_ = screen_area();
        if (pass.target)
        {
            const auto found = textures_.find(pass.target.id);
            assert(found != textures_.end() && found->second.target && "pass into a texture that is not a target");
            if (found != textures_.end() && found->second.target)
            {
                pass_target_ = found->second;
                pass_area_ = { 0, 0, found->second.extent };
            }
        }
        // As the OpenGL backend: clears cover the whole target, and a target's depth is cleared unless copied.
        if (pass.clear)
            std::ranges::fill(pass_target_->texels, pack({ pass.clear_color.r, pass.clear_color.g, pass.clear_color.b, pass.clear_color.a }));
        const auto depth_from = textures_.find(pass.depth_from.id);
        if (pass.target && pass.depth_from && depth_from != textures_.end() && depth_from->second.depth.size() == pass_target_->depth.size())
            std::ranges::copy(depth_from->second.depth, pass_target_->depth.begin());
        else if (pass.clear || pass.target)
            std::ranges::fill(pass_target_->depth, pass.clear_depth);
        ++frame_stats_.passes;
        state_ = State::InPass;
    }

    std::optional<const Texture&> SoftwareDevice::find_texture(TextureHandle handle)
    {
        const auto found = textures_.find(handle.id);
        if (!handle || found == textures_.end())
            return std::nullopt;
        found->second.colour_read = true;
        return found->second;
    }

    void SoftwareDevice::draw(const DrawCommand& command)
    {
        assert(state_ == State::InPass);
        const auto pipeline = pipelines_.find(command.pipeline.id);
        const auto vertices = buffers_.find(command.vertices.buffer.id);
        assert(pipeline != pipelines_.end() && "draw with an unknown pipeline");
        assert(vertices != buffers_.end() && "draw with an unknown vertex buffer");
        if (pipeline == pipelines_.end() || vertices == buffers_.end())
            return;
        const Pipeline& p = pipeline->second;
        ++frame_stats_.draws;
        frame_stats_.primitives += primitive_count(p.desc.topology, command.count);
        // shortcut: lines are not drawn; nothing in the game draws them yet.
        if (p.program.vertex == nullptr || p.desc.topology == Topology::Lines)
            return;
        const std::optional<const Texture&> texture = find_texture(command.texture);
        const bool textured = p.desc.shader != Shader::VertexColor && p.desc.shader != Shader::Shape;
        assert((!textured || texture) && "draw without the texture its shader reads");
        if (textured && !texture)
            return;
        DrawState state {
            .program = p.program,
            .blend = p.desc.blend,
            .depth_test = p.desc.depth_test,
            .depth_write = p.desc.depth_write,
            .colour_write = p.desc.colour_write,
            .shade = &*pass_target_ == &screen_ || pass_target_->colour_read,
            .fade = p.desc.fade,
            .constants = shader_constants(command, p.desc.shader),
            .texture = texture,
            .filter = command.filter,
            .address = command.address,
            .eye = eye_of(command.transform),
            .viewport = pass_area_,
            .scissor = pass_area_,
            .target = pass_target_->extent,
        };
        const std::array<TextureHandle, kExtraTextures> extras = bound_extra_textures(command, p.desc.shader);
        for (std::size_t i = 0; i < kExtraTextures; ++i)
            state.extras[i] = find_texture(extras[i]);
        std::span<const std::byte> indices;
        if (command.indices.buffer)
        {
            const auto found = buffers_.find(command.indices.buffer.id);
            assert(found != buffers_.end() && "draw with an unknown index buffer");
            if (found == buffers_.end() || command.indices.offset > found->second.size())
                return;
            indices = std::span<const std::byte>(found->second).subspan(command.indices.offset);
        }
        if (command.vertices.offset > vertices->second.size())
            return;
        add_triangles(batch_, state, { .vertices = std::span<const std::byte>(vertices->second).subspan(command.vertices.offset), .indices = indices, .index_type = command.index_type, .count = command.count, .first = command.first, .vertex_offset = command.vertex_offset, .layout = p.desc.layout }, p.desc.cull);
        if (batch_.triangles.size() >= kBatchTriangles)
            flush();
    }

    void SoftwareDevice::flush()
    {
        if (!batch_.empty() && pass_target_)
            draw_batch(batch_, *pass_target_, *pool_);
    }

    void SoftwareDevice::draw_ui()
    {
        assert(state_ == State::InPass);
        ImGui::Render();
        ui_drawn_ = ui_ready_;
    }

    void SoftwareDevice::end_pass()
    {
        assert(state_ == State::InPass);
        flush();
        pass_target_.reset();
        state_ = State::InFrame;
    }

    bool SoftwareDevice::copy_screen(TextureHandle target)
    {
        assert(state_ == State::InFrame);
        const auto found = textures_.find(target.id);
        if (found == textures_.end() || found->second.depth_id == 0 || found->second.extent.width != extent().width || found->second.extent.height != extent().height)
            return false;
        Texture& t = found->second;
        const ScreenView area = screen_area();
        for (std::uint32_t y = 0; y < t.extent.height; ++y)
        {
            const std::size_t from = std::size_t { area.y + y } * screen_.extent.width + area.x;
            const std::size_t to = std::size_t { y } * t.extent.width;
            std::copy_n(screen_.texels.begin() + static_cast<std::ptrdiff_t>(from), t.extent.width, t.texels.begin() + static_cast<std::ptrdiff_t>(to));
            std::copy_n(screen_.depth.begin() + static_cast<std::ptrdiff_t>(from), t.extent.width, t.depth.begin() + static_cast<std::ptrdiff_t>(to));
        }
        return true;
    }

    bool SoftwareDevice::read_screen(TextureHandle from)
    {
        assert(state_ == State::InFrame);
        if (reading_ || reading_window_)
            return false;
        if (!from)
        {
            reading_window_ = true; // Read as the frame ends, once the screen and the interface are in the window
            return true;
        }
        const auto found = textures_.find(from.id);
        if (found == textures_.end() || !found->second.target)
            return false;
        Texture& t = found->second;
        t.colour_read = true;
        ScreenPixels pixels { t.extent, std::vector<std::uint8_t>(t.texels.size() * 4) };
        std::memcpy(pixels.rgba.data(), t.texels.data(), pixels.rgba.size());
        for (std::size_t i = 3; i < pixels.rgba.size(); i += 4)
            pixels.rgba[i] = 255;
        reading_ = std::move(pixels);
        return true;
    }

    std::optional<ScreenPixels> SoftwareDevice::take_screen()
    {
        return std::exchange(read_, std::nullopt);
    }

    // The screen stretched over the window through the brightness curve (present.slang), opaque.
    void SoftwareDevice::compose_window()
    {
        const float power = std::exp2(1.0f - 2.0f * std::clamp(brightness_, 0.0f, 1.0f));
        std::array<std::uint32_t, 256> curve { };
        for (std::uint32_t i = 0; i < 256; ++i)
            curve[i] = static_cast<std::uint32_t>(std::lround(std::pow(static_cast<float>(i) / 255.0f, power) * 255.0f));
        const Extent w = window_.extent;
        const Extent s = screen_.extent;
        const bool same = w.width == s.width && w.height == s.height;
        const Filter filter = smooth_scaling_ ? Filter::Linear : Filter::Nearest;
        constexpr std::uint32_t kRows = 16; ///< Rows a job
        pool_->run((w.height + kRows - 1) / kRows, [&](std::uint32_t job) {
            for (std::uint32_t y = job * kRows; y < std::min(w.height, (job + 1) * kRows); ++y)
                for (std::uint32_t x = 0; x < w.width; ++x)
                {
                    const std::uint32_t c = same ? screen_.texels[std::size_t { y } * s.width + x] : pack(sample(screen_, { (static_cast<float>(x) + 0.5f) / static_cast<float>(w.width), (static_cast<float>(y) + 0.5f) / static_cast<float>(w.height) }, filter, AddressMode::Clamp));
                    window_.texels[std::size_t { y } * w.width + x] = curve[c & 255u] | (curve[(c >> 8) & 255u] << 8) | (curve[(c >> 16) & 255u] << 16) | 0xff000000u;
                }
        });
    }

    // ImGui's draw lists, with the Textured program, over the window.
    void SoftwareDevice::draw_imgui()
    {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        {
            if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates)
            {
                // The whole picture each time: it changes seldom, when the font atlas grows.
                std::uint32_t id = static_cast<std::uint32_t>(tex->TexID);
                if (tex->TexID == ImTextureID_Invalid)
                {
                    id = next_id_++;
                    tex->SetTexID(id);
                }
                Texture& t = textures_[id];
                t = Texture { .extent = { static_cast<std::uint32_t>(tex->Width), static_cast<std::uint32_t>(tex->Height) } };
                const auto* pixels = static_cast<const std::uint8_t*>(tex->GetPixels());
                t.texels.resize(std::size_t { t.extent.width } * t.extent.height);
                for (std::size_t i = 0; i < t.texels.size(); ++i)
                {
                    if (tex->Format == ImTextureFormat_Alpha8)
                        t.texels[i] = 0x00ffffffu | (std::uint32_t { pixels[i] } << 24);
                    else
                        std::memcpy(&t.texels[i], pixels + i * 4, 4);
                }
                tex->SetStatus(ImTextureStatus_OK);
            }
            else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
            {
                textures_.erase(static_cast<std::uint32_t>(tex->TexID));
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }
        const ImDrawData* data = ImGui::GetDrawData();
        if (data == nullptr || data->DisplaySize.x <= 0.0f || data->DisplaySize.y <= 0.0f)
            return;
        static constexpr std::array kAttributes {
            VertexAttribute { .location = 0, .format = VertexFormat::Float2, .offset = offsetof(ImDrawVert, pos) },
            VertexAttribute { .location = 1, .format = VertexFormat::UByte4Norm, .offset = offsetof(ImDrawVert, col) },
            VertexAttribute { .location = 2, .format = VertexFormat::Float2, .offset = offsetof(ImDrawVert, uv) },
        };
        const float width = static_cast<float>(window_.extent.width);
        const float height = static_cast<float>(window_.extent.height);
        const ImVec2 origin = data->DisplayPos;
        const ImVec2 scale = data->FramebufferScale;
        // ImGui's points to clip space, y up as every program has it.
        glm::mat4 transform(1.0f);
        transform[0][0] = 2.0f * scale.x / width;
        transform[1][1] = -2.0f * scale.y / height;
        transform[3][0] = -origin.x * transform[0][0] - 1.0f;
        transform[3][1] = -origin.y * transform[1][1] + 1.0f;
        DrawState state { .program = program(Shader::Textured), .blend = BlendMode::Alpha, .depth_test = false, .depth_write = false, .constants = { .transform = transform }, .filter = Filter::Linear, .viewport = { 0, 0, window_.extent }, .target = window_.extent };
        for (const ImDrawList* list : data->CmdLists)
        {
            const std::span<const std::byte> vertices = std::as_bytes(std::span(list->VtxBuffer.Data, static_cast<std::size_t>(list->VtxBuffer.Size)));
            const std::span<const std::byte> indices = std::as_bytes(std::span(list->IdxBuffer.Data, static_cast<std::size_t>(list->IdxBuffer.Size)));
            for (const ImDrawCmd& cmd : list->CmdBuffer)
            {
                if (cmd.UserCallback != nullptr)
                    continue; // Only ImGui's own state reset; nothing to reset here
                state.texture = find_texture({ static_cast<std::uint32_t>(cmd.GetTexID()) });
                const float x0 = std::clamp((cmd.ClipRect.x - origin.x) * scale.x, 0.0f, width);
                const float y0 = std::clamp((cmd.ClipRect.y - origin.y) * scale.y, 0.0f, height);
                const float x1 = std::clamp((cmd.ClipRect.z - origin.x) * scale.x, 0.0f, width);
                const float y1 = std::clamp((cmd.ClipRect.w - origin.y) * scale.y, 0.0f, height);
                if (!state.texture || x1 <= x0 || y1 <= y0)
                    continue;
                const auto left = static_cast<std::uint32_t>(x0);
                const auto top = static_cast<std::uint32_t>(y0);
                state.scissor = { left, top, { static_cast<std::uint32_t>(x1) - left, static_cast<std::uint32_t>(y1) - top } };
                add_triangles(batch_, state, { .vertices = vertices, .indices = indices, .index_type = sizeof(ImDrawIdx) == 2 ? IndexType::U16 : IndexType::U32, .count = cmd.ElemCount, .first = cmd.IdxOffset, .vertex_offset = static_cast<std::int32_t>(cmd.VtxOffset), .layout = { .stride = sizeof(ImDrawVert), .attributes = kAttributes } }, CullMode::None);
            }
        }
        if (!batch_.empty())
            draw_batch(batch_, window_, *pool_);
    }

    void SoftwareDevice::end_frame()
    {
        assert(state_ == State::InFrame);
        compose_window();
        if (ui_drawn_)
            draw_imgui();
        if (std::exchange(reading_window_, false))
        {
            ScreenPixels pixels { window_.extent, std::vector<std::uint8_t>(window_.texels.size() * 4) };
            std::memcpy(pixels.rgba.data(), window_.texels.data(), pixels.rgba.size());
            reading_ = std::move(pixels);
        }
        if (reading_)
            read_ = std::exchange(reading_, std::nullopt);
        if (shown_)
        {
            // From the window's top-left corner, rows downwards.
            present_.viewport(0, 0, static_cast<GLsizei>(window_.extent.width), static_cast<GLsizei>(window_.extent.height));
            present_.raster_pos(-1.0f, 1.0f);
            present_.pixel_zoom(1.0f, -1.0f);
            present_.draw_pixels(static_cast<GLsizei>(window_.extent.width), static_cast<GLsizei>(window_.extent.height), GL_RGBA, GL_UNSIGNED_BYTE, window_.texels.data());
            glfwSwapBuffers(config_.window);
        }
        frame_stats_.buffers = static_cast<std::uint32_t>(buffers_.size());
        frame_stats_.pipelines = static_cast<std::uint32_t>(pipelines_.size());
        frame_stats_.textures = static_cast<std::uint32_t>(textures_.size());
        last_stats_ = frame_stats_;
        state_ = State::Idle;
    }

} // namespace rct3::render::software
