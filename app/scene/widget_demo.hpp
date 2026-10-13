// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real interface skins, fonts and strings on the widget tree: a menu bar and a toolbar along the top, and
// a window class with pages of text, a list and controls. Confirm opens another window beside it, the cross
// or cancel closes one, the title bar drags it, pressing a window raises it. The menus open and close
// windows, the latter all at once after a question, and pick the interface scale. A first real window until the game's own windows exist.
#pragma once

#include "config/game_install.hpp"
#include "render/device.hpp"
#include "ui/desktop.hpp"
#include "ui/draw_list.hpp"
#include "ui/font_set.hpp"
#include "ui/interface_assets.hpp"
#include "ui/widgets/menu_bar.hpp"
#include "ui/widgets/question_box.hpp"
#include "ui/widgets/toolbar.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace rct3::scene
{

    struct WidgetDemo
    {
        ui::InterfaceAssets assets;
        const ui::FontSet* current_fonts = nullptr; ///< The set for the width of the last update
        ui::Desktop desktop;
        ui::MenuBar* menu_bar = nullptr; ///< In the desktop's tree, which lives as long as the demo
        ui::Toolbar* toolbar = nullptr;
        ui::QuestionBox* question = nullptr; ///< While it is shown
        ui::DrawList list;
        std::optional<float> scale; ///< Chosen interface scale; nothing picks one from the window height
    };

    /// Where `./nob fetch` puts the fallback font, from the repository root.
    inline const std::filesystem::path kDefaultFallbackFont = "downloads/unifont-15.0.06.ttf";

    /// Loads skins from the install's main overlay and the strings of `language`, uploads the images and
    /// opens the first window. Missing strings leave the text empty. Fonts come in sets for the width of the
    /// interface, each font with `fallback_font` for the characters it lacks; without that file the game
    /// fonts are used alone.
    [[nodiscard]] std::expected<WidgetDemo, std::string> create_widget_demo(render::Device& device, const config::GameInstall& install, const std::string& language, const std::filesystem::path& fallback_font);

    /// Lays out for the device's size and routes the pointer, given in framebuffer pixels, and the keys.
    /// Returns whether the interface has the pointer. `elapsed` is the frame time in seconds.
    bool update_widget_demo(render::Device& device, WidgetDemo& demo, const ui::PointerState& pointer_pixels, const ui::KeyboardState& keyboard, float elapsed);

    /// Records the interface into the current pass.
    void draw_widget_demo(render::Device& device, WidgetDemo& demo);

    void destroy_widget_demo(render::Device& device, WidgetDemo& demo);

} // namespace rct3::scene
