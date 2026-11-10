// SPDX-License-Identifier: GPL-3.0-or-later
//
// The player's camera over the park: dragged, turned, zoomed by the wheel, pushed by the keys and the screen's
// edges, as the control mode option has it, and free look flying about.
#pragma once

#include "config/game_options.hpp"

namespace rct3::scene::park
{

    struct ParkScene;
    struct ParkInput;

    /// Moves the camera by this frame's input over `elapsed` seconds. With the pointer `over_interface`, its wheel and
    /// the screen's edges leave the camera be, and no drag begins.
    void move_camera(ParkScene& scene, const ParkInput& input, bool over_interface, const config::GameOptions& options, float elapsed);

} // namespace rct3::scene::park
