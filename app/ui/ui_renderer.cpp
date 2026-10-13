// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ui_renderer.hpp"

#include <array>
#include <cstddef>

namespace rct3::ui
{

    namespace
    {

        static_assert(sizeof(Vertex) == 20, "the pipeline below describes this layout");

        constexpr std::array<render::VertexAttribute, 3> kAttributes = { {
            { 0, render::VertexFormat::Float2, offsetof(Vertex, x) },
            { 1, render::VertexFormat::UByte4Norm, offsetof(Vertex, color) },
            { 2, render::VertexFormat::Float2, offsetof(Vertex, u) },
        } };

    } // namespace

    std::expected<UiRenderer, std::string> create_ui_renderer(render::Device& device)
    {
        UiRenderer renderer;
        const render::PipelineDesc desc {
            .shader = render::Shader::Textured,
            .layout = { sizeof(Vertex), kAttributes },
            .blend = render::BlendMode::Alpha,
            .depth_test = false,
            .depth_write = false,
            .label = "ui",
        };
        auto pipeline = device.create_pipeline(desc);
        if (!pipeline)
            return std::unexpected(pipeline.error());
        renderer.pipeline = *pipeline;

        constexpr std::array<std::byte, 4> kWhitePixel = { std::byte { 255 }, std::byte { 255 }, std::byte { 255 }, std::byte { 255 } };
        auto white = device.create_texture({ 1, 1, render::TextureFormat::Rgba8, kWhitePixel, "ui white" });
        if (!white)
        {
            destroy_ui_renderer(device, renderer);
            return std::unexpected(white.error());
        }
        renderer.white = *white;
        return renderer;
    }

    void render_draw_list(render::Device& device, const UiRenderer& renderer, const DrawList& list)
    {
        if (list.batches().empty())
            return;
        const auto vertices = device.allocate_transient(std::as_bytes(list.vertices()));
        const auto indices = device.allocate_transient(std::as_bytes(list.indices()));
        if (!vertices || !indices)
            return;

        const render::Extent extent = device.extent();
        // Top-left origin, y down: the top edge (y = 0) maps to the top of clip space.
        const glm::mat4 screen = glm::ortho(0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, -1.0f, 1.0f);

        render::DrawCommand command;
        command.pipeline = renderer.pipeline;
        command.vertices = *vertices;
        command.indices = *indices;
        command.index_type = render::IndexType::U32;
        command.transform = screen;
        // Skins and pictures are drawn at many sizes; nearest sampling would drop rows and columns.
        command.filter = render::Filter::Linear;
        for (const Batch& batch : list.batches())
        {
            command.texture = batch.texture == 0 ? renderer.white : render::TextureHandle { batch.texture };
            command.first = batch.first_index;
            command.count = batch.index_count;
            device.draw(command);
        }
    }

    void destroy_ui_renderer(render::Device& device, UiRenderer& renderer)
    {
        device.destroy_texture(renderer.white);
        device.destroy_pipeline(renderer.pipeline);
        renderer = { };
    }

} // namespace rct3::ui
