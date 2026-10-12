// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real interface skins on the widget tree: a window class with a list area, tick boxes, a toggle and
// buttons. Confirm opens another window beside it, the cross or cancel closes one, the title bar drags it,
// pressing a window raises it. A first real window until the game's own windows exist; it has no text yet.
#pragma once

#include "render/device.hpp"
#include "ui/desktop.hpp"
#include "ui/draw_list.hpp"
#include "ui/skin_library.hpp"
#include "ui/ui_renderer.hpp"
#include "ui/ui_textures.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace rct3::scene
{

    struct WidgetDemo
    {
        ui::UiRenderer renderer;
        ui::UiTextures textures;
        ui::SkinLibrary skins;
        ui::Desktop desktop;
        ui::DrawList list;
    };

    /// Loads the skins of the overlay at `path`, uploads the images they use and opens the first window.
    [[nodiscard]] std::expected<WidgetDemo, std::string> create_widget_demo(render::Device& device, const std::filesystem::path& path);

    /// Lays out for the device's size and routes the pointer, given in framebuffer pixels. Returns whether
    /// the interface has the pointer.
    bool update_widget_demo(render::Device& device, WidgetDemo& demo, const ui::PointerState& pointer_pixels);

    /// Records the interface into the current pass.
    void draw_widget_demo(render::Device& device, WidgetDemo& demo);

    void destroy_widget_demo(render::Device& device, WidgetDemo& demo);

} // namespace rct3::scene
