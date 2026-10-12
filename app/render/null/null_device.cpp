// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/null/null_device.hpp"

#include "imgui.h"

#include <GLFW/glfw3.h>
#include <cassert>

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
        state_ = State::Off;
    }

    Extent NullDevice::extent() const
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
        assert(buffers_.contains(command.vertices.id) && "draw with an unknown vertex buffer");
        assert((!command.indices || buffers_.contains(command.indices.id)) && "draw with an unknown index buffer");
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
        last_stats_ = frame_stats_;
        state_ = State::Idle;
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
