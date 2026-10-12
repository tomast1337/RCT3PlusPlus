// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real interface skins, fonts and strings on the widget tree: a window class with a wrapped help text,
// labelled tick boxes, a toggle and buttons. Confirm opens another window beside it, the cross or cancel
// closes one, the title bar drags it, pressing a window raises it. A first real window until the game's own
// windows exist.
#pragma once

#include "config/game_install.hpp"
#include "render/device.hpp"
#include "ui/desktop.hpp"
#include "ui/draw_list.hpp"
#include "ui/font_set.hpp"
#include "ui/skin_library.hpp"
#include "ui/string_table.hpp"
#include "ui/ui_renderer.hpp"
#include "ui/ui_textures.hpp"

#include <expected>
#include <memory>
#include <string>

namespace rct3::scene
{

    struct WidgetDemo
    {
        ui::UiRenderer renderer;
        ui::UiTextures textures;
        ui::SkinLibrary skins;
        ui::FontSet fonts;
        /// On the heap: windows keep a reference, and the demo moves.
        std::unique_ptr<ui::StringTable> strings = std::make_unique<ui::StringTable>();
        ui::Desktop desktop;
        ui::DrawList list;
    };

    /// Loads skins and fonts from the install's main overlay and English strings, uploads the images and
    /// opens the first window. Missing strings leave the text empty.
    [[nodiscard]] std::expected<WidgetDemo, std::string> create_widget_demo(render::Device& device, const config::GameInstall& install);

    /// Lays out for the device's size and routes the pointer, given in framebuffer pixels. Returns whether
    /// the interface has the pointer.
    bool update_widget_demo(render::Device& device, WidgetDemo& demo, const ui::PointerState& pointer_pixels);

    /// Records the interface into the current pass.
    void draw_widget_demo(render::Device& device, WidgetDemo& demo);

    void destroy_widget_demo(render::Device& device, WidgetDemo& demo);

} // namespace rct3::scene
