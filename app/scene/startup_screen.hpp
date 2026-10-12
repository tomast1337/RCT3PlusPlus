// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the game shows before the front end: the logo sequence, then the loading screen while data loads.
// Logos fill as much of the window as they can without changing shape, centred on white (their art is
// dark on transparent). The loading screen is one skin, centred on black at its own size times the
// interface scale. Placement and background colours are our reading of the art, not taken from the game.
#pragma once

#include "config/game_install.hpp"
#include "render/device.hpp"
#include "ui/draw_list.hpp"
#include "ui/skin_set.hpp"
#include "ui/startup_sequence.hpp"
#include "ui/ui_renderer.hpp"
#include "ui/ui_textures.hpp"

#include <expected>
#include <optional>
#include <string>

namespace rct3::scene
{

    struct StartupScreen
    {
        ui::UiRenderer renderer;
        ui::UiTextures logos;
        ui::UiTextures loading_textures;
        std::optional<ui::NamedSkin> loading;
        ui::StartupSequence sequence { { } };
        ui::DrawList list;
        std::optional<float> scale; ///< Chosen interface scale; nothing picks one from the window height
    };

    /// Loads the banner logos and the loading screen from the install.
    [[nodiscard]] std::expected<StartupScreen, std::string> create_startup_screen(render::Device& device, const config::GameInstall& install, const ui::StartupOptions& options);

    /// Records the current logo into the current pass. Call while `screen.sequence` is not finished.
    void draw_startup_logo(render::Device& device, StartupScreen& screen);

    /// Records the loading screen into the current pass.
    void draw_loading_screen(render::Device& device, StartupScreen& screen);

    void destroy_startup_screen(render::Device& device, StartupScreen& screen);

} // namespace rct3::scene
