// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws a ui::DrawList with the render device: the frame's vertices and indices go into per-frame memory,
// then one draw per batch. Alpha blended, no depth, linearly sampled, in screen pixels with the origin at the
// top-left. Quads without a texture sample a 1 x 1 white texture so all quad batches share one pipeline;
// shape batches (ui/shape.hpp) draw from the list's shape vertices with the shape pipeline.
#pragma once

#include "render/device.hpp"
#include "ui/draw_list.hpp"

#include <expected>
#include <string>

namespace rct3::ui
{

    struct UiRenderer
    {
        render::PipelineHandle pipeline;
        render::PipelineHandle shapes;
        render::TextureHandle white;
    };

    [[nodiscard]] std::expected<UiRenderer, std::string> create_ui_renderer(render::Device& device);

    /// A DrawList TextureId for a device texture. The list stores the handle's id as is.
    [[nodiscard]] inline TextureId texture_id(render::TextureHandle texture)
    {
        return texture.id;
    }

    /// Records the list's draws into the current pass.
    void render_draw_list(render::Device& device, const UiRenderer& renderer, const DrawList& list);

    void destroy_ui_renderer(render::Device& device, UiRenderer& renderer);

} // namespace rct3::ui
