// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/null/null_device.hpp"

#include "imgui.h"

#include <GLFW/glfw3.h>
#include <cassert>
#include <utility>

namespace rct3::render::null
{

    NullDevice::~NullDevice()
    {
        shutdown();
    }

    std::expected<void, std::string> NullDevice::init(const DeviceConfig& config)
    {
        assert(state_ == State::Off);
        config_ = config;
        if (config.ui)
        {
            // Claim the texture protocol so ImGui hands its font atlas to us instead of expecting a legacy
            // backend to upload it; every request is then acknowledged without storing anything.
            ImGuiIO& io = ImGui::GetIO();
            io.BackendRendererName = "rct3_null";
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            ui_ready_ = true;
        }
        state_ = State::Idle;
        return { };
    }

    void NullDevice::shutdown()
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
            io.BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
            ui_ready_ = false;
        }
        buffers_.clear();
        pipelines_.clear();
        textures_.clear();
        transient_id_ = 0;
        state_ = State::Off;
    }

    Extent NullDevice::screen_extent() const
    {
        int width = 0;
        int height = 0;
        if (config_.window != nullptr)
            glfwGetFramebufferSize(config_.window, &width, &height);
        return { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
    }

    std::expected<BufferHandle, std::string> NullDevice::create_buffer(const BufferDesc& desc)
    {
        assert(state_ != State::Off);
        if (desc.data.empty())
            return std::unexpected("empty buffer");
        const std::uint32_t id = next_id_++;
        buffers_.emplace(id, Buffer { desc.usage, desc.data.size() });
        return BufferHandle { id };
    }

    void NullDevice::destroy_buffer(BufferHandle buffer)
    {
        buffers_.erase(buffer.id);
    }

    std::expected<TextureHandle, std::string> NullDevice::create_texture(const TextureDesc& desc)
    {
        assert(state_ != State::Off);
        if (desc.width == 0 || desc.height == 0)
            return std::unexpected("empty texture");
        if (desc.target ? !desc.pixels.empty() : desc.pixels.size() != texture_bytes(desc.width, desc.height, desc.format))
            return std::unexpected("pixel data does not match the texture size");
        const std::uint32_t id = next_id_++;
        textures_.emplace(id, Extent { desc.width, desc.height });
        if (desc.target && desc.readable_depth)
        {
            const std::uint32_t depth = next_id_++;
            textures_.emplace(depth, Extent { desc.width, desc.height });
            depths_.emplace(id, depth);
        }
        return TextureHandle { id };
    }

    void NullDevice::destroy_texture(TextureHandle texture)
    {
        textures_.erase(texture.id);
        if (const auto depth = depths_.find(texture.id); depth != depths_.end())
        {
            textures_.erase(depth->second);
            depths_.erase(depth);
        }
    }

    void NullDevice::update_texture([[maybe_unused]] TextureHandle texture, std::span<const std::byte>)
    {
        assert(state_ == State::InFrame);
        assert(textures_.contains(texture.id) && "update of an unknown texture");
    }

    TextureHandle NullDevice::depth_texture(TextureHandle target) const
    {
        const auto depth = depths_.find(target.id);
        return depth != depths_.end() ? TextureHandle { depth->second } : TextureHandle { };
    }

    bool NullDevice::copy_screen(TextureHandle target)
    {
        assert(state_ == State::InFrame);
        const auto found = textures_.find(target.id);
        const Extent screen = extent();
        return found != textures_.end() && depths_.contains(target.id) && found->second.width == screen.width && found->second.height == screen.height;
    }

    bool NullDevice::read_screen(TextureHandle from)
    {
        assert(state_ == State::InFrame);
        const auto target = from ? textures_.find(from.id) : textures_.end();
        if (from && target == textures_.end())
            return false;
        const Extent screen = from ? target->second : screen_extent();
        if (screen.width == 0 || screen.height == 0)
            return false;
        reading_ = screen;
        return true;
    }

    std::optional<ScreenPixels> NullDevice::take_screen()
    {
        return std::exchange(read_, std::nullopt);
    }

    std::expected<BufferSlice, std::string> NullDevice::allocate_transient(std::span<const std::byte> data)
    {
        assert(state_ == State::InFrame || state_ == State::InPass);
        if (transient_id_ == 0)
        {
            transient_id_ = next_id_++;
            buffers_.emplace(transient_id_, Buffer { BufferUsage::Vertex, 0 });
        }
        const std::uint64_t offset = (transient_used_ + 15) & ~std::uint64_t { 15 };
        transient_used_ = offset + data.size();
        frame_stats_.transient_bytes += data.size();
        return BufferSlice { BufferHandle { transient_id_ }, static_cast<std::uint32_t>(offset) };
    }

    std::expected<PipelineHandle, std::string> NullDevice::create_pipeline(const PipelineDesc& desc)
    {
        assert(state_ != State::Off);
        if (desc.layout.attributes.size() > kMaxVertexAttributes)
            return std::unexpected("too many vertex attributes");
        const std::uint32_t id = next_id_++;
        pipelines_.emplace(id, desc.topology);
        return PipelineHandle { id };
    }

    void NullDevice::destroy_pipeline(PipelineHandle pipeline)
    {
        pipelines_.erase(pipeline.id);
    }

    void NullDevice::begin_frame()
    {
        assert(state_ == State::Idle);
        frame_stats_ = { };
        transient_used_ = 0;
        state_ = State::InFrame;
    }

    void NullDevice::begin_pass(const PassDesc&)
    {
        assert(state_ == State::InFrame);
        ++frame_stats_.passes;
        state_ = State::InPass;
    }

    void NullDevice::draw(const DrawCommand& command)
    {
        assert(state_ == State::InPass);
        const auto pipeline = pipelines_.find(command.pipeline.id);
        assert(pipeline != pipelines_.end() && "draw with an unknown pipeline");
        assert(buffers_.contains(command.vertices.buffer.id) && "draw with an unknown vertex buffer");
        assert((!command.indices.buffer || buffers_.contains(command.indices.buffer.id)) && "draw with an unknown index buffer");
        assert((!command.texture || textures_.contains(command.texture.id)) && "draw with an unknown texture");
        if (pipeline == pipelines_.end())
            return;
        ++frame_stats_.draws;
        frame_stats_.primitives += primitive_count(pipeline->second, command.count);
    }

    void NullDevice::end_pass()
    {
        assert(state_ == State::InPass);
        state_ = State::InFrame;
    }

    void NullDevice::end_frame()
    {
        assert(state_ == State::InFrame);
        frame_stats_.buffers = static_cast<std::uint32_t>(buffers_.size());
        frame_stats_.pipelines = static_cast<std::uint32_t>(pipelines_.size());
        frame_stats_.textures = static_cast<std::uint32_t>(textures_.size());
        last_stats_ = frame_stats_;
        state_ = State::Idle;
        // Nothing is drawn, so the screen reads as black.
        if (const auto size = std::exchange(reading_, std::nullopt))
        {
            read_ = ScreenPixels { *size, std::vector<std::uint8_t>(std::size_t { size->width } * size->height * 4, 0) };
            for (std::size_t i = 3; i < read_->rgba.size(); i += 4)
                read_->rgba[i] = 255;
        }
    }

    void NullDevice::ui_new_frame()
    {
    }

    void NullDevice::draw_ui()
    {
        assert(state_ == State::InPass);
        ImGui::Render();
        ImDrawData* data = ImGui::GetDrawData();
        if (data->Textures == nullptr)
            return;
        for (ImTextureData* tex : *data->Textures)
        {
            if (tex->Status == ImTextureStatus_WantCreate)
            {
                tex->SetTexID(static_cast<ImTextureID>(1));
                tex->SetStatus(ImTextureStatus_OK);
            }
            else if (tex->Status == ImTextureStatus_WantUpdates)
            {
                tex->SetStatus(ImTextureStatus_OK);
            }
            else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
            {
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }

} // namespace rct3::render::null
