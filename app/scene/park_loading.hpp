// SPDX-License-Identifier: GPL-3.0-or-later
//
// The loading screen (ui/screens/loading_screen.hpp) while a park loads, drawn with the front end's skins,
// fonts and strings. It names the stage under way with the game's own words for its loading steps where one
// fits, and its bar shows the share of the stages done: the park scene says when each begins, not how far
// into it loading is.
#pragma once

#include "render/device.hpp"
#include "scene/park_scene.hpp"
#include "ui/draw_list.hpp"
#include "ui/interface_assets.hpp"

#include <string_view>

namespace rct3::scene
{

    /// The key of the string that names `stage`.
    [[nodiscard]] std::string_view park_load_text(ParkLoadStage stage);

    /// Records the loading screen with `stage` under way into the current pass, over the whole target.
    void draw_park_loading(render::Device& device, ui::InterfaceAssets& assets, ui::DrawList& list, ParkLoadStage stage);

} // namespace rct3::scene
