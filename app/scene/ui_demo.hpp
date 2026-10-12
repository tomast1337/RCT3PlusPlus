// SPDX-License-Identifier: GPL-3.0-or-later
//
// A placeholder 2D interface that exercises the UI draw path until real skins are loaded: a stretchable
// window built from a generated skin texture, a gradient title bar, a clipped scrolling list and a row of
// three-cell buttons in different tints. Rebuilt from scratch every frame, as the game's interface is.
#pragma once

#include "render/device.hpp"
#include "ui/draw_list.hpp"
#include "ui/ui_renderer.hpp"

#include <expected>
#include <string>

namespace rct3::scene
{

    struct UiDemo
    {
        ui::UiRenderer renderer;
        render::TextureHandle skins;
        ui::DrawList list;
    };

    [[nodiscard]] std::expected<UiDemo, std::string> create_ui_demo(render::Device& device);

    /// Rebuilds the demo's draw list and records it into the current pass. `seconds` drives the animation.
    void draw_ui_demo(render::Device& device, UiDemo& demo, double seconds);

    void destroy_ui_demo(render::Device& device, UiDemo& demo);

} // namespace rct3::scene
