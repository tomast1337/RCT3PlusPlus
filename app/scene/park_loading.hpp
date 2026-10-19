// SPDX-License-Identifier: GPL-3.0-or-later
//
// The loading screen (ui/screens/loading_screen.hpp) while a park loads, drawn with the front end's fonts and
// strings and the game's logo. It lists the stages with the game's own words for its loading steps where one
// fits, marks the one under way, and its bar shows the share of the stages done: the park scene says when each
// begins, not how far into it loading is. Under them it shows the last thing the park's loading logged, and
// how many warnings it has logged so far: the log has the rest.
#pragma once

#include "config/game_install.hpp"
#include "render/device.hpp"
#include "scene/park_scene.hpp"
#include "ui/draw_list.hpp"
#include "ui/interface_assets.hpp"

#include <cstdint>
#include <string_view>

namespace rct3::scene
{

    /// The key of the string that names `stage`.
    [[nodiscard]] std::string_view park_load_text(ParkLoadStage stage);

    /// The stage's name in the log.
    [[nodiscard]] std::string_view park_load_name(ParkLoadStage stage);

    /// Adds the game's logo to `assets`' textures, for the loading screen; without it the screen has none.
    void add_loading_logo(render::Device& device, const config::GameInstall& install, ui::InterfaceAssets& assets);

    /// Records the loading screen with `stage` under way into the current pass, over the whole target. Its
    /// detail comes from the log's entries after `log_from` (logging::global().latest_sequence() when the
    /// load began).
    void draw_park_loading(render::Device& device, ui::InterfaceAssets& assets, ui::DrawList& list, ParkLoadStage stage, std::uint64_t log_from);

} // namespace rct3::scene
